#define _GNU_SOURCE
#include <assert.h>
#include "EarthFS.h"
#include "async/async.h"
#include "http/HTTPMessage.h"

#define READER_COUNT 64
#define QUEUE_SIZE 64 // TODO: Find a way to lower these without sacrificing performance, and perhaps automatically adjust them somehow.

struct EFSPull {
	uint64_t pullID;
	EFSSessionRef session;
	str_t *host;
	str_t *username;
	str_t *password;
	str_t *cookie;
	str_t *query;

	async_mutex_t connlock[1];
	HTTPConnectionRef conn;

	async_mutex_t mutex[1];
	async_cond_t cond[1];
	bool stop;
	count_t tasks;
	EFSSubmissionRef queue[QUEUE_SIZE];
	bool filled[QUEUE_SIZE];
	index_t cur;
	count_t count;
};

static int reconnect(EFSPullRef const pull);
static int import(EFSPullRef const pull, strarg_t const URI, index_t const pos, HTTPConnectionRef *const conn);

EFSPullRef EFSRepoCreatePull(EFSRepoRef const repo, uint64_t const pullID, uint64_t const userID, strarg_t const host, strarg_t const username, strarg_t const password, strarg_t const cookie, strarg_t const query) {
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

	EFSPullStop(pull);

	pull->pullID = 0;
	EFSSessionFree(&pull->session);
	FREE(&pull->host);
	FREE(&pull->username);
	FREE(&pull->password);
	FREE(&pull->cookie);
	FREE(&pull->query);

	assert_zeroed(pull, 1);
	FREE(pullptr); pull = NULL;
}

static void reader(EFSPullRef const pull) {
	HTTPConnectionRef conn = NULL;
	int rc;

	for(;;) {
		if(pull->stop) goto stop;

		str_t URI[URI_MAX];

		async_mutex_lock(pull->connlock);

		rc = HTTPConnectionReadBodyLine(pull->conn, URI, sizeof(URI));
		if(rc < 0) {
			for(;;) {
				if(reconnect(pull) >= 0) break;
				if(pull->stop) break;
				async_sleep(1000 * 5);
			}
			async_mutex_unlock(pull->connlock);
			continue;
		}

		async_mutex_lock(pull->mutex);
		while(pull->count + 1 > QUEUE_SIZE) {
			async_cond_wait(pull->cond, pull->mutex);
			if(pull->stop) {
				async_mutex_unlock(pull->mutex);
				async_mutex_unlock(pull->connlock);
				goto stop;
			}
		}
		index_t pos = (pull->cur + pull->count) % QUEUE_SIZE;
		pull->count += 1;
		async_mutex_unlock(pull->mutex);

		async_mutex_unlock(pull->connlock);

		for(;;) {
			if(import(pull, URI, pos, &conn) >= 0) break;
			if(pull->stop) goto stop;
			async_sleep(1000 * 5);
		}

	}

stop:
	HTTPConnectionFree(&conn);
	async_mutex_lock(pull->mutex);
	assertf(pull->stop, "Reader ended early");
	assert(pull->tasks > 0);
	pull->tasks--;
	async_cond_broadcast(pull->cond);
	async_mutex_unlock(pull->mutex);
}
static void writer(EFSPullRef const pull) {
	EFSSubmissionRef queue[QUEUE_SIZE];
	count_t count = 0;
	count_t skipped = 0;
	double time = uv_now(loop) / 1000.0;
	for(;;) {
		if(pull->stop) goto stop;

		async_mutex_lock(pull->mutex);
		while(0 == count || (count < QUEUE_SIZE && pull->count > 0)) {
			index_t const pos = pull->cur;
			while(!pull->filled[pos]) {
				async_cond_wait(pull->cond, pull->mutex);
				if(pull->stop) {
					async_mutex_unlock(pull->mutex);
					goto stop;
				}
				if(!count) time = uv_now(loop) / 1000.0;
			}
			assert(pull->filled[pos]);
			// Skip any bubbles in the queue.
			if(pull->queue[pos]) queue[count++] = pull->queue[pos];
			else skipped++;
			pull->queue[pos] = NULL;
			pull->filled[pos] = false;
			pull->cur = (pull->cur + 1) % QUEUE_SIZE;
			pull->count--;
			async_cond_broadcast(pull->cond);
		}
		async_mutex_unlock(pull->mutex);
		assert(count <= QUEUE_SIZE);

		for(;;) {
			int rc = EFSSubmissionBatchStore(queue, count);
			if(rc >= 0) break;
			fprintf(stderr, "Submission error %s / %s (%d)\n", uv_strerror(rc), db_strerror(rc), rc);
			async_sleep(1000 * 5);
		}
		for(index_t i = 0; i < count; ++i) {
			EFSSubmissionFree(&queue[i]);
		}

		double const now = uv_now(loop) / 1000.0;
		fprintf(stderr, "Pulled %f files per second\n", count / (now - time));
		time = now;
		count = 0;
		skipped = 0;

	}

stop:
	for(index_t i = 0; i < count; ++i) {
		EFSSubmissionFree(&queue[i]);
	}
	assert_zeroed(queue, QUEUE_SIZE);

	async_mutex_lock(pull->mutex);
	assertf(pull->stop, "Writer ended early");
	assert(pull->tasks > 0);
	pull->tasks--;
	async_cond_broadcast(pull->cond);
	async_mutex_unlock(pull->mutex);
}
int EFSPullStart(EFSPullRef const pull) {
	if(!pull) return 0;
	assert(0 == pull->tasks);
	async_mutex_init(pull->connlock, 0);
	async_mutex_init(pull->mutex, 0);
	async_cond_init(pull->cond, 0);
	for(index_t i = 0; i < READER_COUNT; ++i) {
		pull->tasks++;
		async_spawn(STACK_DEFAULT, (void (*)())reader, pull);
	}
	pull->tasks++;
	async_spawn(STACK_DEFAULT, (void (*)())writer, pull);
	// TODO: It'd be even better to have one writer shared between all pulls...

	return 0;
}
void EFSPullStop(EFSPullRef const pull) {
	if(!pull) return;
	if(!pull->connlock) return;

	async_mutex_lock(pull->mutex);
	pull->stop = true;
	async_cond_broadcast(pull->cond);
	while(pull->tasks > 0) {
		async_cond_wait(pull->cond, pull->mutex);
	}
	async_mutex_unlock(pull->mutex);

	async_mutex_destroy(pull->connlock);
	HTTPConnectionFree(&pull->conn);

	async_mutex_destroy(pull->mutex);
	async_cond_destroy(pull->cond);
	pull->stop = false;

	for(index_t i = 0; i < QUEUE_SIZE; ++i) {
		EFSSubmissionFree(&pull->queue[i]);
		pull->filled[i] = false;
	}
	pull->cur = 0;
	pull->count = 0;
}

static int auth(EFSPullRef const pull);

static int reconnect(EFSPullRef const pull) {
	int rc;
	HTTPConnectionFree(&pull->conn);

	if(!pull->cookie) {
		rc = auth(pull);
		if(rc < 0) return rc;
	}

	rc = HTTPConnectionCreateOutgoing(pull->host, &pull->conn);
	if(rc < 0) {
		fprintf(stderr, "Pull couldn't connect to %s (%s)\n", pull->host, uv_strerror(rc));
		return rc;
	}
	HTTPConnectionWriteRequest(pull->conn, HTTP_GET, "/efs/query?count=all", pull->host);
	// TODO: Pagination...
	// TODO: More careful error handling.
	assert(pull->cookie);
	HTTPConnectionWriteHeader(pull->conn, "Cookie", pull->cookie);
	HTTPConnectionBeginBody(pull->conn);
	rc = HTTPConnectionEnd(pull->conn);
	if(rc < 0) {
		fprintf(stderr, "Pull couldn't connect to %s (%s)\n", pull->host, uv_strerror(rc));
		return rc;
	}
	int const status = HTTPConnectionReadResponseStatus(pull->conn);
	if(status < 0) {
		fprintf(stderr, "Pull connection error %s\n", uv_strerror(status));
		return status;
	}
	if(403 == status) {
		fprintf(stderr, "Pull connection authentication failed\n");
		FREE(&pull->cookie);
		return UV_EACCES;
	}
	if(status < 200 || status >= 300) {
		fprintf(stderr, "Pull connection error %d\n", status);
		return UV_EPROTO;
	}

	rc = HTTPConnectionReadHeaders(pull->conn, NULL, NULL, 0);
	if(rc < 0) {
		fprintf(stderr, "Pull connection error %s\n", uv_strerror(rc));
		return rc;
	}

	return 0;
}

static int auth(EFSPullRef const pull) {
	if(!pull) return 0;
	FREE(&pull->cookie);

	HTTPConnectionRef conn;
	int rc = HTTPConnectionCreateOutgoing(pull->host, &conn);
	// TODO: if(rc < 0) ...
	HTTPConnectionWriteRequest(conn, HTTP_POST, "/efs/auth", pull->host);
	HTTPConnectionWriteContentLength(conn, 0);
	HTTPConnectionBeginBody(conn);
	// TODO: Send credentials.
	HTTPConnectionEnd(conn);

	int const status = HTTPConnectionReadResponseStatus(conn);
	if(status < 0) return status;

	static str_t const fields[][FIELD_MAX] = {
		"set-cookie",
	};
	str_t headers[numberof(fields)][VALUE_MAX];
	rc = HTTPConnectionReadHeaders(conn, headers, fields, numberof(fields));
	if(rc < 0) return rc;

	HTTPConnectionFree(&conn);

	strarg_t const cookie = headers[0];

	if(!prefix("s=", cookie)) return -1;

	strarg_t x = cookie+2;
	while('\0' != *x && ';' != *x) x++;

	FREE(&pull->cookie);
	pull->cookie = strndup(cookie, x-cookie);
	if(!pull->cookie) return UV_ENOMEM;
	fprintf(stderr, "Cookie for %s: %s\n", pull->host, pull->cookie);
	// TODO: Update database?

	return 0;
}


static int import(EFSPullRef const pull, strarg_t const URI, index_t const pos, HTTPConnectionRef *const conn) {
	if(!pull) return 0;
	if(!pull->cookie) goto fail;

	// TODO: Even if there's nothing to do, we have to enqueue something to fill up our reserved slots. I guess it's better than doing a lot of work inside the connection lock, but there's got to be a better way.
	EFSSubmissionRef sub = NULL;

	if(!URI) goto enqueue;

	str_t algo[EFS_ALGO_SIZE];
	str_t hash[EFS_HASH_SIZE];
	if(!EFSParseURI(URI, algo, hash)) goto enqueue;

	if(EFSSessionGetFileInfo(pull->session, URI, NULL) >= 0) goto enqueue;

	// TODO: We're logging out of order when we do it like this...
//	fprintf(stderr, "Pulling %s\n", URI);

	int rc = 0;
	if(!*conn) {
		rc = HTTPConnectionCreateOutgoing(pull->host, conn);
		if(rc < 0) {
			fprintf(stderr, "Pull import connection error %s\n", uv_strerror(rc));
			goto fail;
		}
	}

	str_t *path;
	if(asprintf(&path, "/efs/file/%s/%s", algo, hash) < 0) {
		fprintf(stderr, "asprintf() error\n");
		goto fail;
	}
	rc = 0;
	rc = rc < 0 ? rc : HTTPConnectionWriteRequest(*conn, HTTP_GET, path, pull->host);
	FREE(&path);

	assert(pull->cookie);
	rc = rc < 0 ? rc : HTTPConnectionWriteHeader(*conn, "Cookie", pull->cookie);
	rc = rc < 0 ? rc : HTTPConnectionBeginBody(*conn);
	rc = rc < 0 ? rc : HTTPConnectionEnd(*conn);
	if(rc < 0) {
		fprintf(stderr, "Pull import request error %s\n", uv_strerror(rc));
		goto fail;
	}
	int const status = HTTPConnectionReadResponseStatus(*conn);
	if(status < 0) {
		fprintf(stderr, "Pull import response error %s\n", uv_strerror(status));
		goto fail;
	}
	if(status < 200 || status >= 300) {
		fprintf(stderr, "Pull import status error %d\n", status);
		goto fail;
	}

	static str_t const fields[][FIELD_MAX] = {
		"content-type",
		"content-length",
	};
	str_t headers[numberof(fields)][VALUE_MAX];
	rc = HTTPConnectionReadHeaders(*conn, headers, fields, numberof(fields));
	if(rc < 0) {
		fprintf(stderr, "Pull import headers error %s\n", uv_strerror(rc));
		goto fail;
	}

	sub = EFSSubmissionCreate(pull->session, headers[0]);
	if(!sub) {
		fprintf(stderr, "Pull submission error\n");
		goto fail;
	}
	for(;;) {
		if(pull->stop) goto fail;
		uv_buf_t buf[1] = {};
		rc = HTTPConnectionReadBody(*conn, buf);
		if(rc < 0) {
			fprintf(stderr, "Pull download error %s\n", uv_strerror(rc));
			goto fail;
		}
		if(0 == buf->len) break;
		rc = EFSSubmissionWrite(sub, (byte_t *)buf->base, buf->len);
		if(rc < 0) {
			fprintf(stderr, "Pull write error\n");
			goto fail;
		}
	}
	rc = 0;
	rc = rc < 0 ? rc : EFSSubmissionEnd(sub);
	rc = rc < 0 ? rc : EFSSubmissionAddFile(sub);
	if(rc < 0) {
		fprintf(stderr, "Pull submission error\n");
		goto fail;
	}

enqueue:
	async_mutex_lock(pull->mutex);
	pull->queue[pos] = sub; sub = NULL;
	pull->filled[pos] = true;
	async_cond_broadcast(pull->cond);
	async_mutex_unlock(pull->mutex);
	return 0;

fail:
	EFSSubmissionFree(&sub);
	HTTPConnectionFree(conn);
	return -1;
}

