// Copyright 2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <limits.h>
#include "../http/QueryString.h"
#include "RSSServer.h"
#include "Template.h"

#define RESULTS_MAX 10

struct RSSServer {
	SLNRepoRef repo;
	TemplateRef head;
	TemplateRef tail;
	TemplateRef item_start;
	TemplateRef item_end;
};

int RSSServerCreate(SLNRepoRef const repo, RSSServerRef *const out) {
	assert(repo);
	RSSServerRef rss = calloc(1, sizeof(struct RSSServer));
	if(!rss) return UV_ENOMEM;
	int rc = 0;

	rss->repo = repo;
	strarg_t const repodir = SLNRepoGetDir(repo);
	str_t tmp[PATH_MAX];

	snprintf(tmp, sizeof(tmp), "%s/%s", repodir, "blog/template/rss/head.xml");
	rss->head = TemplateCreateFromPath(tmp);
	snprintf(tmp, sizeof(tmp), "%s/%s", repodir, "blog/template/rss/tail.xml");
	rss->tail = TemplateCreateFromPath(tmp);
	snprintf(tmp, sizeof(tmp), "%s/%s", repodir, "blog/template/rss/item-start.xml");
	rss->item_start = TemplateCreateFromPath(tmp);
	snprintf(tmp, sizeof(tmp), "%s/%s", repodir, "blog/template/rss/item-end.xml");
	rss->item_end = TemplateCreateFromPath(tmp);

	*out = rss; rss = NULL;
cleanup:
	RSSServerFree(&rss);
	return rc;
}
void RSSServerFree(RSSServerRef *const rssptr) {
	RSSServerRef rss = *rssptr;
	if(!rss) return;
	rss->repo = NULL;
	TemplateFree(&rss->head);
	TemplateFree(&rss->tail);
	TemplateFree(&rss->item_start);
	TemplateFree(&rss->item_end);
	assert_zeroed(rss, 1);
	FREE(rssptr); rss = NULL;
}

static int GET_feed(RSSServerRef const rss, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method) return -1;
	strarg_t qs = NULL;
	if(0 != uripathcmp("/feed.xml", URI, &qs)) return -1;

	int rc = 0;
	int status = -1;
	SLNFilterRef filter = NULL;
	str_t *URIs[RESULTS_MAX] = {};
	ssize_t count = 0;

	static strarg_t const fields[] = {
		"q",
	};
	str_t *values[numberof(fields)] = {};
	QSValuesParse(qs, values, fields, numberof(fields));
	rc = SLNUserFilterParse(session, values[0], &filter);
	QSValuesCleanup(values, numberof(values));
	if(DB_EACCES == rc) {
		status = 403;
		goto cleanup;
	}
	if(DB_EINVAL == rc) rc = SLNFilterCreate(session, SLNVisibleFilterType, &filter);
	if(rc < 0) {
		status = 500;
		goto cleanup;
	}




	SLNFilterPosition pos[1] = {{ .dir = -1 }};
	uint64_t max = RESULTS_MAX;
	int outdir = -1;
	SLNFilterParseOptions(qs, pos, &max, &outdir, NULL);
	if(max < 1) max = 1;
	if(max > RESULTS_MAX) max = RESULTS_MAX;

	count = SLNFilterCopyURIs(filter, session, pos, outdir, false, URIs, (size_t)max);
	SLNFilterPositionCleanup(pos);
	if(count < 0) {
		if(DB_NOTFOUND == count) {
			// Possibly a filter age-function bug.
			alogf("Invalid start parameter? %s\n", URI);
			status = 500;
			goto cleanup;
		}
		alogf("Filter error: %s\n", sln_strerror(count));
		status = 500;
		goto cleanup;
	}
	SLNFilterFree(&filter);


	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Content-Type", "application/rss+xml");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	if(0 == SLNSessionGetUserID(session)) {
		HTTPConnectionWriteHeader(conn, "Cache-Control", "no-cache, public");
	} else {
		HTTPConnectionWriteHeader(conn, "Cache-Control", "no-cache, private");
	}
	HTTPConnectionBeginBody(conn);

	TemplateStaticArg const args[] = {
		{"reponame", "reponame"},
		{"title", "title"},
		{"description", "description"},
		{"queryURI", "http://example.com"},
		{NULL, NULL},
	};

	TemplateWriteHTTPChunk(rss->head, &TemplateStaticCBs, args, conn);

	for(size_t i = 0; i < count; i++) {
		TemplateWriteHTTPChunk(rss->item_start, &TemplateStaticCBs, args, conn);
		uv_buf_t x = uv_buf_init((char *)STR_LEN("test"));
		HTTPConnectionWriteChunkv(conn, &x, 1);
		TemplateWriteHTTPChunk(rss->item_end, &TemplateStaticCBs, args, conn);
	}


	TemplateWriteHTTPChunk(rss->tail, &TemplateStaticCBs, args, conn);

	HTTPConnectionWriteChunkEnd(conn);
	HTTPConnectionEnd(conn);


cleanup:
	SLNFilterFree(&filter);
	for(size_t i = 0; i < count; i++) FREE(&URIs[i]);
	assert_zeroed(URIs, count);
	return status;
}


int RSSServerDispatch(RSSServerRef const rss, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	int rc = -1;
	rc = rc >= 0 ? rc : GET_feed(rss, session, conn, method, URI, headers);
	return rc;
}

