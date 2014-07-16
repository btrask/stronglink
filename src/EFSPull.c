#define _GNU_SOURCE
#include "EarthFS.h"
#include "async.h"
#include "http/HTTPMessage.h"

#define URI_MAX 1024
#define CONNECTION_COUNT 4

typedef enum {
	t_run,
	t_wait,
	t_stop,
	t_done,
} thread_state;

struct EFSPull {
	int64_t pullID;
	EFSSessionRef session;
	str_t *host;
	str_t *username;
	str_t *password;
	str_t *cookie;
	str_t *query;

	async_mutex_t *lock;
	HTTPConnectionRef conn;
	HTTPMessageRef msg;

	cothread_t threads[CONNECTION_COUNT];
	thread_state states[CONNECTION_COUNT];
	index_t next;
	cothread_t master;
};

static err_t reconnect(EFSPullRef const pull);
static err_t import(EFSPullRef const pull, index_t const thread, strarg_t const URI, HTTPConnectionRef *const conn);

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
static index_t arg_thread;
static void pull_thread(void) {
	EFSPullRef const pull = arg_pull;
	index_t const thread = arg_thread;
	HTTPConnectionRef conn = NULL;

	for(;;) {
		if(t_stop == pull->states[thread]) break;
		assertf(t_run == pull->states[thread], "Pull thread running in invalid state %d", pull->states[thread]);

		str_t URI[URI_MAX+1];

		async_mutex_lock(pull->lock);
		if(HTTPMessageReadLine(pull->msg, URI, URI_MAX) < 0) {
			for(;;) {
				if(reconnect(pull) >= 0) break;
				if(t_stop == pull->states[thread]) break;
				async_sleep(1000 * 5);
			}
			async_mutex_unlock(pull->lock);
			continue;
		}
		async_mutex_unlock(pull->lock);

		for(;;) {
			if(import(pull, thread, URI, &conn) >= 0) break;
			if(t_stop == pull->states[thread]) break;
			async_sleep(1000 * 5);
		}
	}

	HTTPConnectionFree(&conn);
	pull->threads[thread] = NULL;
	pull->states[thread] = t_done;
	if(pull->master) async_wakeup(pull->master);
	co_terminate();
}
err_t EFSPullStart(EFSPullRef const pull) {
	if(!pull) return 0;
	assertf(!pull->lock, "Pull already running");
	pull->lock = async_mutex_create();
	if(!pull->lock) return -1;
	pull->next = 0;
	for(index_t i = 0; i < CONNECTION_COUNT; ++i) {
		arg_pull = pull;
		arg_thread = i;
		pull->threads[i] = co_create(STACK_DEFAULT, pull_thread);
		assertf(pull->threads[i], "co_create() failed"); // TODO: Handle failure.
		pull->states[i] = t_run;
		async_wakeup(pull->threads[i]);
	}
	return 0;
}
void EFSPullStop(EFSPullRef const pull) {
	if(!pull) return;
	if(!pull->lock) return;
	count_t wait = 0;

	for(index_t i = 0; i < CONNECTION_COUNT; ++i) {
		switch(pull->states[i]) {
			case t_run:
				pull->states[i] = t_stop;
				wait++;
				break;
			case t_wait:
				pull->states[i] = t_stop;
				async_wakeup(pull->threads[i]);
				switch(pull->states[i]) {
					case t_run:
					case t_wait:
						assertf(0, "Thread entered invalid state %d instead of stopping", pull->states[i]);
						break;
					case t_stop:
						wait++;
						break;
					case t_done:
						break;
				}
				break;
			case t_stop:
				assertf(0, "Thread already stopped");
				break;
			case t_done:
				break;
		}
	}

	pull->master = co_active();
	while(wait) {
		co_switch(yield);
		--wait;
	}
	pull->master = NULL;

	for(index_t i = 0; i < CONNECTION_COUNT; ++i) {
		assertf(t_done == pull->states[i], "Pull thread ended in invalid state %d", pull->states[i]);
	}

	async_mutex_free(pull->lock); pull->lock = NULL;
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
static err_t import(EFSPullRef const pull, index_t const thread, strarg_t const URI, HTTPConnectionRef *const conn) {
	if(!pull) return 0;
	if(!URI) return 0;

	str_t algo[EFS_ALGO_SIZE];
	str_t hash[EFS_HASH_SIZE];
	if(!EFSParseURI(URI, algo, hash)) return 0;

	// TODO: Have a public API for testing whether a file exists without actually loading the file info with EFSSessionCopyFileInfo(). Even once we have a smarter fast-forward algorithm, this will still be useful.
	EFSSessionRef const session = pull->session;
	EFSRepoRef const repo = EFSSessionGetRepo(session);
	sqlite3 *db = EFSRepoDBConnect(repo);
	sqlite3_stmt *test = QUERY(db,
		"SELECT file_id FROM file_uris\n"
		"WHERE uri = ? LIMIT 1");
	sqlite3_bind_text(test, 1, URI, -1, SQLITE_STATIC);
	bool_t exists = SQLITE_ROW == sqlite3_step(test);
	sqlite3_finalize(test); test = NULL;
	EFSRepoDBClose(repo, &db);
	if(exists) return 0;

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
	EFSSubmissionRef sub = NULL;
	EFSSubmissionRef meta = NULL;
	if(EFSSubmissionCreatePair(session, headers->content_type, (ssize_t (*)())HTTPMessageGetBuffer, msg, NULL, &sub, &meta) < 0) {
		fprintf(stderr, "Pull import submission error\n");
		goto fail;
	}
	// TODO: Call EFSSubmissionWrite() in a loop so we can also check whether our thread was stopped.

	if(t_stop == pull->states[thread]) goto fail2;
	assertf(t_run == pull->states[thread], "Pull thread in invalid state %d", pull->states[thread]);

	if(thread != pull->next) {
		pull->states[thread] = t_wait;
		co_switch(yield);
		if(t_stop == pull->states[thread]) goto fail2;
		assertf(t_run == pull->states[thread], "Pull thread in invalid state %d", pull->states[thread]);
	}

	for(;;) {
		sqlite3 *db = EFSRepoDBConnect(EFSSessionGetRepo(pull->session));
		EXEC(QUERY(db, "SAVEPOINT store"));
		err_t err = 0;
		if(err >= 0) err = EFSSubmissionStore(sub, db);
		if(err >= 0) err = EFSSubmissionStore(meta, db);
		if(err < 0) EXEC(QUERY(db, "ROLLBACK TO store"));
		EXEC(QUERY(db, "RELEASE store"));
		EFSRepoDBClose(EFSSessionGetRepo(pull->session), &db);
		if(err >= 0) break;
		async_sleep(1000 * 5);
	}

	assertf(thread == pull->next, "Pull submission order error");
	pull->next = (pull->next + 1) % CONNECTION_COUNT;
	if(t_wait == pull->states[pull->next]) {
		pull->states[pull->next] = t_run;
		async_wakeup(pull->threads[pull->next]);
	}

	EFSSubmissionFree(&sub);
	EFSSubmissionFree(&meta);

	HTTPMessageDrain(msg);
	HTTPMessageFree(&msg);

	return 0;

fail2:
	EFSSubmissionFree(&sub);
	EFSSubmissionFree(&meta);
fail:
	HTTPMessageFree(&msg);
	HTTPConnectionFree(conn);
	return -1;
}

