// Copyright 2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include "http/HTTP.h"
#include "http/QueryString.h"
#include "StrongLink.h"

#define WORKER_COUNT 32

struct SLNPull {
	SLNSessionRef session;
	SLNSyncRef sync;
	str_t *certhash; // TODO
	str_t *host;
	str_t *path;
	str_t *query;
	str_t *cookie;
	bool run;
};

int SLNPullCreate(SLNSessionCacheRef const cache, uint64_t const sessionID, strarg_t const certhash, strarg_t const host, strarg_t const path, strarg_t const query, strarg_t const cookie, SLNPullRef *const out) {
	assert(out);
	if(!sessionID) return UV_EINVAL;
	if(!host) return UV_EINVAL;

	SLNSessionRef session = NULL;
	SLNPullRef pull = NULL;
	int rc;

	rc = SLNSessionCacheLoadSessionUnsafe(cache, sessionID, &session);
	if(rc < 0) goto cleanup;

	pull = calloc(1, sizeof(struct SLNPull));
	if(!pull) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	pull->session = session; session = NULL;

	rc = SLNSyncCreate(pull->session, &pull->sync);
	if(rc < 0) goto cleanup;

	pull->certhash = certhash ? strdup(certhash) : NULL;
	pull->host = strdup(host);
	pull->path = strdup(path ? path : ""); // TODO: Strip trailing /
	pull->query = strdup(query ? query : "");
	pull->cookie = aasprintf("s=%s", cookie ? cookie : "");
	if(!pull->certhash || !pull->host) rc = UV_ENOMEM;
	if(!pull->path || !pull->query || !pull->cookie) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	pull->run = false;

	*out = pull; pull = NULL;

cleanup:
	SLNSessionRelease(&session);
	SLNPullFree(&pull);
	return rc;
}
void SLNPullFree(SLNPullRef *const pullptr) {
	assert(pullptr);
	SLNPullRef pull = *pullptr;
	if(!pull) return;

	SLNPullStop(pull);

	SLNSessionRelease(&pull->session);
	SLNSyncFree(&pull->sync);
	FREE(&pull->certhash);
	FREE(&pull->host);
	FREE(&pull->path);
	FREE(&pull->query);
	FREE(&pull->cookie);

	assert_zeroed(pull, 1);
	FREE(pullptr); pull = NULL;
}


static void reader(SLNPullRef const pull, bool const meta) {
	HTTPConnectionRef conn = NULL;
	int rc = 0;

	str_t fileURI[SLN_URI_MAX];
	str_t metaURI[SLN_URI_MAX];
	rc = SLNSessionCopyLastSubmissionURIs(pull->session, fileURI, metaURI);
	if(rc < 0) goto cleanup;

	str_t path[URI_MAX]; // TODO: Escaping
	if(meta) {
		rc = snprintf(path, sizeof(path), "%s/sln/metafiles?q=%s&start=%s",
			pull->path, pull->query, metaURI);
	} else {
		rc = snprintf(path, sizeof(path), "%s/sln/query?q=%s&start=%s",
			pull->path, pull->query, fileURI);
	}
	if(rc >= sizeof(path)) rc = UV_ENAMETOOLONG;
	if(rc < 0) goto cleanup;

	rc = rc < 0 ? rc : HTTPConnectionCreateOutgoing(pull->host, 0, &conn);
	rc = rc < 0 ? rc : HTTPConnectionWriteRequest(conn, HTTP_GET, path, pull->host);
	rc = rc < 0 ? rc : HTTPConnectionWriteHeader(conn, "Cookie", pull->cookie);
	rc = rc < 0 ? rc : HTTPConnectionBeginBody(conn);
	rc = rc < 0 ? rc : HTTPConnectionEnd(conn);
	if(rc < 0) goto cleanup;

	for(;;) {
		if(!pull->run) goto cleanup;

		str_t URI[SLN_URI_MAX*2];
		rc = HTTPConnectionReadBodyLine(conn, URI, sizeof(URI));
		if(rc < 0) goto cleanup;

		if(meta) {
			str_t metaURI[SLN_URI_MAX]; metaURI[0] = '\0';
			str_t targetURI[SLN_URI_MAX]; targetURI[0] = '\0';
			int len = 0;
			sscanf(URI, SLN_URI_FMT " -> " SLN_URI_FMT "%n",
				metaURI, targetURI, &len);
			if('\0' != URI[len]) rc = SLN_INVALIDTARGET; // TODO: Parse error?
			if('\0' == metaURI[0]) rc = SLN_INVALIDTARGET; // TODO
			if('\0' == targetURI[0]) rc = SLN_INVALIDTARGET;
			if(rc < 0) goto cleanup;
			rc = SLNSyncIngestMetaURI(pull->sync, metaURI, targetURI);
			if(rc < 0) goto cleanup;
		} else {
			rc = SLNSyncIngestFileURI(pull->sync, URI);
			if(rc < 0) goto cleanup;
		}
	}

cleanup:
	pull->run = false;
	if(rc < 0) {
		alogf("Pull reader error: %s\n", sln_strerror(rc));
	}
	HTTPConnectionFree(&conn);
}
static void filereader(void *const arg) {
	SLNPullRef const pull = arg;
	reader(pull, false);
}
static void metareader(void *const arg) {
	SLNPullRef const pull = arg;
	reader(pull, true);
}

static void worker(void *const arg) {
	SLNPullRef const pull = arg;
	HTTPConnectionRef conn = NULL;
	HTTPHeadersRef headers = NULL;
	int rc = 0;

	rc = HTTPConnectionCreateOutgoing(pull->host, 0, &conn);
	if(rc < 0) goto cleanup;

	for(;;) {
		if(!pull->run) goto cleanup;

		SLNSubmissionRef sub = NULL;
		rc = SLNSyncWorkAwait(pull->sync, &sub);
		if(rc < 0) goto cleanup;

		strarg_t const URI = SLNSubmissionGetKnownURI(sub);
		str_t algo[SLN_ALGO_SIZE];
		str_t hash[SLN_HASH_SIZE];
		SLNParseURI(URI, algo, hash);
		str_t path[URI_MAX];
		rc = snprintf(path, sizeof(path), "%s/sln/file/%s/%s", pull->path, algo, hash);
		if(rc >= sizeof(path)) rc = UV_ENAMETOOLONG;
		if(rc < 0) goto cleanup;

		rc = rc < 0 ? rc : HTTPConnectionWriteRequest(conn, HTTP_GET, path, pull->host);
		rc = rc < 0 ? rc : HTTPConnectionWriteHeader(conn, "Cookie", pull->cookie);
		rc = rc < 0 ? rc : HTTPConnectionBeginBody(conn);
		rc = rc < 0 ? rc : HTTPConnectionEnd(conn);
		if(rc < 0) goto cleanup;

		int const status = HTTPConnectionReadResponseStatus(conn);
		if(200 != status) goto cleanup;

		// TODO: HTTPConnectionReadHeadersStatic?
		rc = HTTPHeadersCreateFromConnection(conn, &headers);
		if(rc < 0) goto cleanup;

		strarg_t const type = HTTPHeadersGet(headers, "content-type");
		rc = SLNSubmissionSetType(sub, type);
		if(rc < 0) goto cleanup;

		HTTPHeadersFree(&headers);

		for(;;) {
			if(!pull->run) goto cleanup;
			uv_buf_t buf[1];
			rc = HTTPConnectionReadBody(conn, buf);
			if(rc < 0) goto cleanup;
			if(0 == buf->len) goto cleanup;
			rc = SLNSubmissionWrite(sub, (byte_t *)buf->base, buf->len);
			if(rc < 0) goto cleanup;
		}

		rc = SLNSubmissionEnd(sub);
		if(rc < 0) goto cleanup;
		rc = SLNSyncWorkDone(pull->sync, sub);
		if(rc < 0) goto cleanup;

	}

cleanup:
	pull->run = false;
	if(rc < 0) {
		alogf("Pull worker error: %s\n", sln_strerror(rc));
	}
	HTTPConnectionFree(&conn);
	HTTPHeadersFree(&headers);
}

int SLNPullStart(SLNPullRef const pull) {
	if(!pull) return UV_EINVAL;
	if(pull->run) return 0;

	pull->run = true;

	async_spawn(STACK_DEFAULT, filereader, pull);
	async_spawn(STACK_DEFAULT, metareader, pull);

	for(size_t i = 0; i < WORKER_COUNT; i++) {
		async_spawn(STACK_DEFAULT, worker, pull);
	}

	return 0;
}
void SLNPullStop(SLNPullRef const pull) {
	if(!pull) return;
	if(!pull->run) return;
	pull->run = false;
	async_yield(); // TODO
}

