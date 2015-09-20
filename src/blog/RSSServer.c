// Copyright 2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <limits.h>
#include "../http/QueryString.h"
#include "RSSServer.h"
#include "Template.h"

#define RESULTS_MAX 10

static int write_cdata(HTTPConnectionRef const conn, uv_buf_t const *const buf) {
	if(!buf->len) return 0;
	char const *pos = buf->base;
	for(size_t i = 0; i < buf->len; i++) {
		if(i+0 >= buf->len || ']' != buf->base[i+0]) continue;
		if(i+1 >= buf->len || ']' != buf->base[i+1]) continue;
		if(i+2 >= buf->len || '>' != buf->base[i+2]) continue;
		i += 2;
		uv_buf_t parts[] = {
			uv_buf_init((char *)STR_LEN("<![CDATA[")),
			uv_buf_init((char *)pos, i-(pos-buf->base)),
			uv_buf_init((char *)STR_LEN("]]>")),
		};
		int rc = HTTPConnectionWriteChunkv(conn, parts, numberof(parts));
		if(rc < 0) return rc;
		pos = buf->base+i;
	}
	uv_buf_t last[] = {
		uv_buf_init((char *)STR_LEN("<![CDATA[")),
		uv_buf_init((char *)pos, buf->len-(pos-buf->base)),
		uv_buf_init((char *)STR_LEN("]]>")),
	};
	return HTTPConnectionWriteChunkv(conn, last, numberof(last));
}
// TODO: HACK
#define BUFFER_SIZE (1024*8)
static int write_file_cdata(HTTPConnectionRef const conn, uv_file const file) {
	if(!conn) return 0;
	char *buf = malloc(BUFFER_SIZE);
	if(!buf) return UV_ENOMEM;
	uv_buf_t const info = uv_buf_init(buf, BUFFER_SIZE);
	int rc;
	for(;;) {
		ssize_t const len = rc = async_fs_readall_simple(file, &info);
		if(0 == len) break;
		if(rc < 0) break;
		uv_buf_t write = uv_buf_init(buf, len);
		rc = write_cdata(conn, &write);
		if(rc < 0) break;
	}
	FREE(&buf);
	return rc;
}

struct RSSServer {
	SLNRepoRef repo;
	str_t *dir;
	str_t *cacheDir;
	TemplateRef head;
	TemplateRef tail;
	TemplateRef item_start;
	TemplateRef item_end;
};

// TODO: Basically identical to version in Blog.c.
static int load_template(RSSServerRef const rss, strarg_t const name, TemplateRef *const out) {
	assert(rss);
	assert(rss->dir);
	assert(out);
	str_t tmp[PATH_MAX];
	int rc = snprintf(tmp, sizeof(tmp), "%s/template/rss/%s", rss->dir, name);
	if(rc < 0) return rc; // TODO: snprintf(3) error reporting?
	if(rc >= sizeof(tmp)) return UV_ENAMETOOLONG;
	TemplateRef t = NULL;
	rc = TemplateCreateFromPath(tmp, &t);
	if(rc < 0) {
		alogf("Error loading template at '%s': %s\n", tmp, uv_strerror(rc));
		if(UV_ENOENT == rc) alogf("(Try reinstalling the resource files.)\n");
		return UV_ENOENT; // TODO
	}
	*out = t; t = NULL;
	return 0;
}
int RSSServerCreate(SLNRepoRef const repo, RSSServerRef *const out) {
	assert(repo);
	RSSServerRef rss = calloc(1, sizeof(struct RSSServer));
	if(!rss) return UV_ENOMEM;
	int rc = 0;

	rss->repo = repo;
	rss->dir = aasprintf("%s/blog", SLNRepoGetDir(repo));
	rss->cacheDir = aasprintf("%s/rss", SLNRepoGetCacheDir(repo));
	if(!rss->dir || !rss->cacheDir) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	rc = rc < 0 ? rc : load_template(rss, "head.xml", &rss->head);
	rc = rc < 0 ? rc : load_template(rss, "tail.xml", &rss->tail);
	rc = rc < 0 ? rc : load_template(rss, "item-start.xml", &rss->item_start);
	rc = rc < 0 ? rc : load_template(rss, "item-end.xml", &rss->item_end);
	if(rc < 0) goto cleanup;

	*out = rss; rss = NULL;
cleanup:
	RSSServerFree(&rss);
	return rc;
}
void RSSServerFree(RSSServerRef *const rssptr) {
	RSSServerRef rss = *rssptr;
	if(!rss) return;
	rss->repo = NULL;
	FREE(&rss->dir);
	FREE(&rss->cacheDir);
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


	SLNFilterPosition pos[1] = {};
	pos->dir = -1;
	count = SLNFilterCopyURIs(filter, session, pos, -1, false, URIs, RESULTS_MAX);
	SLNFilterPositionCleanup(pos);
	if(count < 0) {
		alogf("Filter error: %s\n", sln_strerror(count));
		status = 500;
		goto cleanup;
	}
	SLNFilterFree(&filter);

	// TODO: Load all meta-data up front, in the same transaction?
	// How are we going to handle coming up with titles and descriptions for everything?
	// Also we need to escape the content for the CDATA section...


	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Content-Type", "application/rss+xml");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	if(0 == SLNSessionGetUserID(session)) {
		HTTPConnectionWriteHeader(conn, "Cache-Control", "no-cache, public");
	} else {
		HTTPConnectionWriteHeader(conn, "Cache-Control", "no-cache, private");
	}
	HTTPConnectionBeginBody(conn);

	str_t *reponame_encoded = htmlenc(SLNRepoGetName(rss->repo));
	TemplateStaticArg const args[] = {
		{"reponame", reponame_encoded},
		{NULL, NULL},
	};

	TemplateWriteHTTPChunk(rss->head, &TemplateStaticCBs, args, conn);

	for(size_t i = 0; i < count; i++) {

		str_t tmp[URI_MAX];
		// It's insane that RSS apparently doesn't support relative URLs.
		strarg_t const proto = HTTPConnectionGetProtocol(conn);
		strarg_t const host = HTTPHeadersGet(headers, "host");
		str_t *escaped = QSEscape(URIs[i], strlen(URIs[i]), true);
		snprintf(tmp, sizeof(tmp), "%s://%s/?q=%s", proto, host, escaped);
		FREE(&escaped);
		str_t *queryURI_encoded = htmlenc(tmp);

		str_t *hashURI_encoded = htmlenc(URIs[i]);
		TemplateStaticArg const itemargs[] = {
			{"title", "(title)"},
			{"description", "(description)"},
			{"queryURI", queryURI_encoded},
			{"hashURI", hashURI_encoded},
			{NULL, NULL},
		};

		TemplateWriteHTTPChunk(rss->item_start, &TemplateStaticCBs, itemargs, conn);

		// TODO: HACK
		str_t algo[SLN_ALGO_SIZE]; // SLN_INTERNAL_ALGO
		str_t hash[SLN_HASH_SIZE];
		SLNParseURI(URIs[i], algo, hash);
		str_t path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/blog/%.2s/%s", SLNRepoGetCacheDir(rss->repo), hash, hash);

		uv_file file = async_fs_open(path, O_RDONLY, 0000);
		write_file_cdata(conn, file);
		async_fs_close(file);

		TemplateWriteHTTPChunk(rss->item_end, &TemplateStaticCBs, itemargs, conn);

		FREE(&queryURI_encoded);
		FREE(&hashURI_encoded);
	}


	TemplateWriteHTTPChunk(rss->tail, &TemplateStaticCBs, args, conn);
	FREE(&reponame_encoded);

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

