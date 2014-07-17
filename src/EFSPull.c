#define _GNU_SOURCE
#include "EarthFS.h"
#include "async.h"
#include "http/HTTPMessage.h"

#define URI_MAX 1024
#define READER_COUNT 4
#define QUEUE_SIZE 10

struct EFSPull {
	int64_t pullID;
	EFSSessionRef session;
	str_t *host;
	str_t *username;
	str_t *password;
	str_t *cookie;
	str_t *query;

	cothread_t stop;
	cothread_t blocked_reader;
	cothread_t blocked_writer;

	async_mutex_t *connlock;
	HTTPConnectionRef conn;
	HTTPMessageRef msg;

	// Lock omitted due to cooperative multitasking.
	// async_mutex_t *queuelock;
	EFSSubmissionRef queue[QUEUE_SIZE];
	bool_t filled[QUEUE_SIZE];
	index_t cur;
	count_t count;
};

static err_t reconnect(EFSPullRef const pull);
static err_t import(EFSPullRef const pull, strarg_t const URI, index_t const pos, HTTPConnectionRef *const conn);

EFSPullRef EFSRepoCreatePull(EFSRepoRef const repo, int64_t const pullID, int64_t const userID, strarg_t const host, strarg_t const username, strarg_t const password, strarg_t const cookie, strarg_t const query) {
	EFSPullRef pull = calloc(1, sizeof(struct EFSPull));
	if(!pull) return NULL;
	pull->pullID = pullID;
	pull->session = EFSRepoCreateSessionInternal(repo, userID);
	pull->username = strdup(username);
	pull->password = strdup(password);
	pull->cookie = cookie ? strdup(cookie) : NULL;
	pull->host = strdup(host);
	pull->query = strdup(query);
	return pull;
}
void EFSPullFree(EFSPullRef *const pullptr) {
	EFSPullRef pull = *pullptr;
	if(!pull) return;
	pull->pullID = -1;
	EFSSessionFree(&pull->session);
	FREE(&pull->host);
	FREE(&pull->username);
	FREE(&pull->password);
	FREE(&pull->query);
	FREE(pullptr); pull = NULL;
}

static EFSPullRef arg_pull;
static void reader(void) {
	EFSPullRef const pull = arg_pull;
	HTTPConnectionRef conn = NULL;

	for(;;) {
		if(pull->stop) break;

		str_t URI[URI_MAX+1];

		async_mutex_lock(pull->connlock);

		if(HTTPMessageReadLine(pull->msg, URI, URI_MAX) < 0) {
			for(;;) {
				if(reconnect(pull) >= 0) break;
				if(pull->stop) break;
				async_sleep(1000 * 5);
			}
			async_mutex_unlock(pull->connlock);
			continue;
		}

		assertf(!pull->blocked_reader, "Reader already waiting");
		if(pull->count + 2 >= QUEUE_SIZE) {
			pull->blocked_reader = co_active();
			co_switch(yield);
			pull->blocked_reader = NULL;
			if(pull->stop) continue;
			assertf(pull->count < QUEUE_SIZE, "Reader didn't wait long enough");
			// We still might not be unblocked, but the writer did its job.
		}
		index_t pos = (pull->cur + pull->count) % QUEUE_SIZE;
		pull->count += 2;

		async_mutex_unlock(pull->connlock);

		for(;;) {
			if(import(pull, URI, pos, &conn) >= 0) break;
			if(pull->stop) break;
			async_sleep(1000 * 5);
		}

	}

	HTTPConnectionFree(&conn);
	assertf(pull->stop, "Reader ended early");
	async_wakeup(pull->stop);
	co_terminate();
}
static void writer(void) {
	EFSPullRef const pull = arg_pull;
	for(;;) {
		if(pull->stop) break;

		if(!pull->count || !pull->filled[pull->cur]) {
			pull->blocked_writer = co_active();
			co_switch(yield);
			pull->blocked_writer = NULL;
			if(pull->stop) continue;
			assertf(pull->filled[pull->cur], "Writer woke up early");
			continue;
		}

		count_t written = 0;
		for(;;) {
			sqlite3 *db = EFSRepoDBConnect(EFSSessionGetRepo(pull->session));
			EXEC(QUERY(db, "SAVEPOINT store"));
			err_t err = 0;
			index_t i;
			for(i = 0; i < pull->count; ++i) {
				index_t const pos = (pull->cur + i) % QUEUE_SIZE;
				if(!pull->filled[pos]) break;
				if(!pull->queue[pos]) continue; // Empty submissions enqueued for various reasons.
				err = EFSSubmissionStore(pull->queue[pos], db);
				if(err < 0) break;
			}
			written = i;
			if(err < 0) EXEC(QUERY(db, "ROLLBACK TO store"));
			EXEC(QUERY(db, "RELEASE store"));
			EFSRepoDBClose(EFSSessionGetRepo(pull->session), &db);
			if(err >= 0) break;
			async_sleep(1000 * 5);
		}
		for(index_t i = 0; i < written; ++i) {
			index_t const pos = (pull->cur + i) % QUEUE_SIZE;
			EFSSubmissionFree(&pull->queue[pos]);
			pull->filled[pos] = false;
		}
		pull->cur = (pull->cur + written) % QUEUE_SIZE;
		pull->count -= written;

		if(pull->blocked_reader) async_wakeup(pull->blocked_reader);

	}

	assertf(pull->stop, "Writer ended early");
	async_wakeup(pull->stop);
	co_terminate();
}
err_t EFSPullStart(EFSPullRef const pull) {
	if(!pull) return 0;
	assertf(!pull->connlock, "Pull already running");
	pull->connlock = async_mutex_create();
	if(!pull->connlock) return -1;
	for(index_t i = 0; i < READER_COUNT; ++i) {
		arg_pull = pull;
		async_wakeup(co_create(STACK_DEFAULT, reader));
	}
	arg_pull = pull;
	async_wakeup(co_create(STACK_DEFAULT, writer));
	// TODO: It'd be even better to have one writer shared between all pulls...
	return 0;
}
void EFSPullStop(EFSPullRef const pull) {
	if(!pull) return;
	if(!pull->connlock) return;

	pull->stop = co_active();
	if(pull->blocked_reader) async_wakeup(pull->blocked_reader);
	if(pull->blocked_writer) async_wakeup(pull->blocked_writer);

	count_t wait = READER_COUNT + 1;
	while(wait) {
		co_switch(yield);
		wait--;
	}

	pull->stop = NULL;
	async_mutex_free(pull->connlock); pull->connlock = NULL;
}

static err_t auth(EFSPullRef const pull);

static err_t reconnect(EFSPullRef const pull) {
	HTTPMessageFree(&pull->msg);
	HTTPConnectionFree(&pull->conn);

	pull->conn = HTTPConnectionCreateOutgoing(pull->host);
	pull->msg = HTTPMessageCreate(pull->conn);
	if(!pull->conn || !pull->msg) return -1;
	HTTPMessageWriteRequest(pull->msg, HTTP_GET, "/efs/query?count=all", pull->host);
	// TODO: Pagination...
	if(pull->cookie) HTTPMessageWriteHeader(pull->msg, "Cookie", pull->cookie);
	HTTPMessageBeginBody(pull->msg);
	HTTPMessageEnd(pull->msg);
	uint16_t const status = HTTPMessageGetResponseStatus(pull->msg);
	if(403 == status) {
		auth(pull);
		return -1;
	}
	if(status < 200 || status >= 300) {
		fprintf(stderr, "Pull connection error %d\n", status);
		return -1;
	}
	return 0;
}

typedef struct {
	strarg_t set_cookie;
} EFSAuthHeaders;
static HeaderField const EFSAuthFields[] = {
	{"set-cookie", 100},
};
static err_t auth(EFSPullRef const pull) {
	if(!pull) return 0;
	FREE(&pull->cookie);

	HTTPConnectionRef conn = HTTPConnectionCreateOutgoing(pull->host);
	HTTPMessageRef msg = HTTPMessageCreate(conn);
	HTTPMessageWriteRequest(msg, HTTP_POST, "/efs/auth", pull->host);
	HTTPMessageWriteContentLength(msg, 0);
	HTTPMessageBeginBody(msg);
	// TODO: Send credentials.
	HTTPMessageEnd(msg);

	uint16_t const status = HTTPMessageGetResponseStatus(msg);
	EFSAuthHeaders const *const headers = HTTPMessageGetHeaders(msg, EFSAuthFields, numberof(EFSAuthFields));

	fprintf(stderr, "Session cookie %s\n", headers->set_cookie);
	// TODO: Parse and store.

	HTTPMessageFree(&msg);
	HTTPConnectionFree(&conn);

	return 0;
}


typedef struct {
	strarg_t content_type;
	strarg_t content_length;
} EFSImportHeaders;
static HeaderField const EFSImportFields[] = {
	{"content-type", 100},
	{"content-length", 100},
};
static err_t import(EFSPullRef const pull, strarg_t const URI, index_t const pos, HTTPConnectionRef *const conn) {
	if(!pull) return 0;

	// TODO: Even if there's nothing to do, we have to enqueue something to fill up our reserved slots. I guess it's better than doing a lot of work inside the connection lock, but there's got to be a better way.
	EFSSubmissionRef sub = NULL;
	EFSSubmissionRef meta = NULL;

	if(!URI) goto enqueue;

	str_t algo[EFS_ALGO_SIZE];
	str_t hash[EFS_HASH_SIZE];
	if(!EFSParseURI(URI, algo, hash)) goto enqueue;

	// TODO: Have a public API for testing whether a file exists without actually loading the file info with EFSSessionCopyFileInfo(). Even once we have a smarter fast-forward algorithm, this will still be useful.
	EFSSessionRef const session = pull->session;
	EFSRepoRef const repo = EFSSessionGetRepo(session);

	// TODO: Figure out how to make this fast.
	sqlite3 *db = EFSRepoDBConnect(repo);
//	EXEC(QUERY(db, "PRAGMA read_uncommitted=1"));
	sqlite3_stmt *test = QUERY(db,
		"SELECT file_id FROM file_uris\n"
		"WHERE uri = ? LIMIT 1");
	sqlite3_bind_text(test, 1, URI, -1, SQLITE_STATIC);
	bool_t exists = SQLITE_ROW == STEP(test);
	sqlite3_finalize(test); test = NULL;
//	EXEC(QUERY(db, "PRAGMA read_uncommitted=0"));
	EFSRepoDBClose(repo, &db);
	if(exists) goto enqueue;

	fprintf(stderr, "Pulling %s\n", URI);

	if(!*conn) *conn = HTTPConnectionCreateOutgoing(pull->host);
	HTTPMessageRef msg = HTTPMessageCreate(*conn);
	if(!*conn || !msg) {
		fprintf(stderr, "Pull import connection error\n");
		goto fail;
	}

	str_t *path;
	asprintf(&path, "/efs/file/%s/%s", algo, hash); // TODO: Error checking
	HTTPMessageWriteRequest(msg, HTTP_GET, path, pull->host);
	FREE(&path);

	HTTPMessageWriteHeader(msg, "Cookie", pull->cookie);
	HTTPMessageBeginBody(msg);
	if(HTTPMessageEnd(msg) < 0) {
		fprintf(stderr, "Pull import request error\n");
		goto fail;
	}
	uint16_t const status = HTTPMessageGetResponseStatus(msg);
	if(status < 200 || status >= 300) {
		fprintf(stderr, "Pull import response error %d\n", status);
		goto fail;
	}

	EFSImportHeaders const *const headers = HTTPMessageGetHeaders(msg, EFSImportFields, numberof(EFSImportFields));
	if(EFSSubmissionCreatePair(session, headers->content_type, (ssize_t (*)())HTTPMessageGetBuffer, msg, NULL, &sub, &meta) < 0) {
		fprintf(stderr, "Pull import submission error\n");
		goto fail;
	}

	if(pull->stop) goto fail2;
	// TODO: Call EFSSubmissionWrite() in a loop so we can also check whether our thread was stopped. There really is no point in checking after the submission has been fully read.

	HTTPMessageDrain(msg);
	HTTPMessageFree(&msg);

enqueue:
	pull->queue[(pos+0) % QUEUE_SIZE] = sub; sub = NULL;
	pull->queue[(pos+1) % QUEUE_SIZE] = meta; meta = NULL;
	pull->filled[(pos+0) % QUEUE_SIZE] = true;
	pull->filled[(pos+1) % QUEUE_SIZE] = true;
	if(pos == pull->cur) {
		if(pull->blocked_writer) async_wakeup(pull->blocked_writer);
	}

	return 0;

fail2:
	EFSSubmissionFree(&sub);
	EFSSubmissionFree(&meta);
fail:
	HTTPMessageFree(&msg);
	HTTPConnectionFree(conn);
	return -1;
}

