// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include "StrongLink.h"
#include "db/db_schema.h"
#include "http/HTTPConnection.h"
#include "http/HTTPHeaders.h"
#include "http/QueryString.h"

#define READER_COUNT 64
#define QUEUE_SIZE 64 // TODO: Find a way to lower these without sacrificing performance, and perhaps automatically adjust them somehow.

struct SLNPull {
	uint64_t pullID;
	SLNSessionRef session;
	str_t *host;
	str_t *cookie;
	str_t *query;

	async_mutex_t connlock[1];
	HTTPConnectionRef conn;

	async_mutex_t mutex[1];
	async_cond_t cond[1];
	bool stop;
	size_t tasks;
	SLNSubmissionRef queue[QUEUE_SIZE];
	bool filled[QUEUE_SIZE];
	size_t cur;
	size_t count;
};

static int reconnect(SLNPullRef const pull);
static int import(SLNPullRef const pull, strarg_t const URI, size_t const pos, HTTPConnectionRef *const conn);

SLNPullRef SLNRepoCreatePull(SLNRepoRef const repo, uint64_t const pullID, uint64_t const userID, strarg_t const host, strarg_t const sessionid, strarg_t const query) {
	SLNPullRef pull = calloc(1, sizeof(struct SLNPull));
	if(!pull) return NULL;

	SLNSessionCacheRef const cache = SLNRepoGetSessionCache(repo);
	pull->pullID = pullID;
	pull->session = SLNSessionCreateInternal(cache, 0, NULL, NULL, userID, SLN_RDWR, NULL); // TODO: How to create this properly?
	pull->host = strdup(host);
	pull->cookie = aasprintf("s=%s", sessionid ? sessionid : "");
	pull->query = strdup(query);
	if(!pull->session || !pull->host || !pull->cookie || !pull->query) {
		SLNPullFree(&pull);
		return NULL;
	}

	async_mutex_init(pull->connlock, 0);
	async_mutex_init(pull->mutex, 0);
	async_cond_init(pull->cond, 0);
	pull->stop = true;

	return pull;
}
void SLNPullFree(SLNPullRef *const pullptr) {
	SLNPullRef pull = *pullptr;
	if(!pull) return;

	SLNPullStop(pull);

	pull->pullID = 0;
	SLNSessionRelease(&pull->session);
	FREE(&pull->host);
	FREE(&pull->cookie);
	FREE(&pull->query);

	async_mutex_destroy(pull->connlock);
	async_mutex_destroy(pull->mutex);
	async_cond_destroy(pull->cond);
	pull->stop = false;

	assert_zeroed(pull, 1);
	FREE(pullptr); pull = NULL;
}

static void reader(SLNPullRef const pull) {
	HTTPConnectionRef conn = NULL;
	int rc;

	for(;;) {
		if(pull->stop) goto stop;

		str_t URI[URI_MAX];

		async_mutex_lock(pull->connlock);

		rc = HTTPConnectionReadBodyLine(pull->conn, URI, sizeof(URI));
		if(rc < 0) {
			for(;;) {
				if(pull->stop) break;
				if(reconnect(pull) >= 0) break;
				if(pull->stop) break;
				async_sleep(1000 * 5);
			}
			async_mutex_unlock(pull->connlock);
			continue;
		}
		if('#' == URI[0]) { // Comment line.
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
		size_t pos = (pull->cur + pull->count) % QUEUE_SIZE;
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
static void writer(SLNPullRef const pull) {
	SLNSubmissionRef queue[QUEUE_SIZE];
	size_t count = 0;
	size_t skipped = 0;
	double time = uv_now(async_loop) / 1000.0;
	for(;;) {
		if(pull->stop) goto stop;

		async_mutex_lock(pull->mutex);
		while(0 == count || (count < QUEUE_SIZE && pull->count > 0)) {
			size_t const pos = pull->cur;
			while(!pull->filled[pos]) {
				async_cond_wait(pull->cond, pull->mutex);
				if(pull->stop) {
					async_mutex_unlock(pull->mutex);
					goto stop;
				}
				if(!count) time = uv_now(async_loop) / 1000.0;
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
			int rc = SLNSubmissionStoreBatch(queue, count);
			if(rc >= 0) break;
			fprintf(stderr, "Submission error %s / %s (%d)\n", uv_strerror(rc), db_strerror(rc), rc);
			async_sleep(1000 * 5);
		}
		for(size_t i = 0; i < count; ++i) {
			SLNSubmissionFree(&queue[i]);
		}

		double const now = uv_now(async_loop) / 1000.0;
		fprintf(stderr, "Pulled %f files per second\n", count / (now - time));
		time = now;
		count = 0;
		skipped = 0;

	}

stop:
	for(size_t i = 0; i < count; ++i) {
		SLNSubmissionFree(&queue[i]);
	}
	assert_zeroed(queue, count);

	async_mutex_lock(pull->mutex);
	assertf(pull->stop, "Writer ended early");
	assert(pull->tasks > 0);
	pull->tasks--;
	async_cond_broadcast(pull->cond);
	async_mutex_unlock(pull->mutex);
}
int SLNPullStart(SLNPullRef const pull) {
	if(!pull) return 0;
	if(!pull->stop) return 0;
	assert(0 == pull->tasks);
	pull->stop = false;
	for(size_t i = 0; i < READER_COUNT; ++i) {
		pull->tasks++;
		async_spawn(STACK_DEFAULT, (void (*)())reader, pull);
	}
	pull->tasks++;
	async_spawn(STACK_DEFAULT, (void (*)())writer, pull);
	// TODO: It'd be even better to have one writer shared between all pulls...

	return 0;
}
void SLNPullStop(SLNPullRef const pull) {
	if(!pull) return;
	if(pull->stop) return;

	async_mutex_lock(pull->mutex);
	pull->stop = true;
	async_cond_broadcast(pull->cond);
	while(pull->tasks > 0) {
		async_cond_wait(pull->cond, pull->mutex);
	}
	async_mutex_unlock(pull->mutex);

	HTTPConnectionFree(&pull->conn);

	for(size_t i = 0; i < QUEUE_SIZE; ++i) {
		SLNSubmissionFree(&pull->queue[i]);
		pull->filled[i] = false;
	}
	pull->cur = 0;
	pull->count = 0;
}

static int reconnect(SLNPullRef const pull) {
	int rc;
	HTTPConnectionFree(&pull->conn);

	rc = HTTPConnectionCreateOutgoing(pull->host, 0, &pull->conn);
	if(rc < 0) {
		fprintf(stderr, "Pull couldn't connect to %s (%s)\n", pull->host, uv_strerror(rc));
		return rc;
	}

	str_t path[URI_MAX];
	str_t *query_encoded = NULL;
	if(pull->query) query_encoded = QSEscape(pull->query, strlen(pull->query), true);
	snprintf(path, sizeof(path), "/sln/query-obsolete?q=%s", query_encoded ?: "");
	FREE(&query_encoded);
	HTTPConnectionWriteRequest(pull->conn, HTTP_GET, path, pull->host);
	// TODO
	// - New API /sln/query and /sln/metafiles
	// - Pagination ?start=[last URI seen]
	// - Error handling
	// - Query string formatter
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
		return UV_EACCES;
	}
	if(status < 200 || status >= 300) {
		fprintf(stderr, "Pull connection error %d\n", status);
		return UV_EPROTO;
	}

	// TODO: All this does is scan past the headers.
	// We don't actually use them...
	HTTPHeadersRef headers;
	rc = HTTPHeadersCreateFromConnection(pull->conn, &headers);
	assert(rc >= 0); // TODO
	HTTPHeadersFree(&headers);
/*	rc = HTTPConnectionReadHeaders(pull->conn, NULL, NULL, 0);
	if(rc < 0) {
		fprintf(stderr, "Pull connection error %s\n", uv_strerror(rc));
		return rc;
	}*/

	return 0;
}


static int import(SLNPullRef const pull, strarg_t const URI, size_t const pos, HTTPConnectionRef *const conn) {
	if(!pull) return 0;

	// TODO: Even if there's nothing to do, we have to enqueue something to fill up our reserved slots. I guess it's better than doing a lot of work inside the connection lock, but there's got to be a better way.
	SLNSubmissionRef sub = NULL;
	HTTPHeadersRef headers = NULL;

	if(!URI) goto enqueue;

	str_t algo[SLN_ALGO_SIZE];
	str_t hash[SLN_HASH_SIZE];
	if(SLNParseURI(URI, algo, hash) < 0) goto enqueue;

	int rc = SLNSessionGetFileInfo(pull->session, URI, NULL);
	if(DB_SUCCESS == rc) goto enqueue;
	db_assertf(DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));

	// TODO: We're logging out of order when we do it like this...
//	fprintf(stderr, "Pulling %s\n", URI);

	if(!*conn) {
		rc = HTTPConnectionCreateOutgoing(pull->host, 0, conn);
		if(rc < 0) {
			fprintf(stderr, "Pull import connection error %s\n", uv_strerror(rc));
			goto fail;
		}
	}

	str_t *path = aasprintf("/sln/file/%s/%s", algo, hash);
	if(!path) {
		fprintf(stderr, "Pull aasprintf error\n");
		goto fail;
	}
	rc = HTTPConnectionWriteRequest(*conn, HTTP_GET, path, pull->host);
	assert(rc >= 0); // TODO
	FREE(&path);

	HTTPConnectionWriteHeader(*conn, "Cookie", pull->cookie);
	HTTPConnectionBeginBody(*conn);
	rc = HTTPConnectionEnd(*conn);
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

	rc = HTTPHeadersCreateFromConnection(*conn, &headers);
	assert(rc >= 0); // TODO
/*	if(rc < 0) {
		fprintf(stderr, "Pull import headers error %s\n", uv_strerror(rc));
		goto fail;
	}*/
	strarg_t const type = HTTPHeadersGet(headers, "content-type");

	rc = SLNSubmissionCreate(pull->session, type, &sub);
	if(rc < 0) {
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
		rc = SLNSubmissionWrite(sub, (byte_t *)buf->base, buf->len);
		if(rc < 0) {
			fprintf(stderr, "Pull write error\n");
			goto fail;
		}
	}
	rc = SLNSubmissionEnd(sub);
	if(rc < 0) {
		fprintf(stderr, "Pull submission error %s\n", uv_strerror(rc));
		goto fail;
	}

enqueue:
	HTTPHeadersFree(&headers);
	async_mutex_lock(pull->mutex);
	pull->queue[pos] = sub; sub = NULL;
	pull->filled[pos] = true;
	async_cond_broadcast(pull->cond);
	async_mutex_unlock(pull->mutex);
	return 0;

fail:
	HTTPHeadersFree(&headers);
	SLNSubmissionFree(&sub);
	HTTPConnectionFree(conn);
	return -1;
}

