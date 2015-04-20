#include <yajl/yajl_tree.h>
#include <limits.h>
#include "Blog.h"

#define RESULTS_MAX 50
#define BUFFER_SIZE (1024 * 8)
#define AUTH_FORM_MAX 1023

// TODO: Real public API.
bool URIPath(strarg_t const URI, strarg_t const path, strarg_t *const qs);

static bool emptystr(strarg_t const str) {
	return !str || '\0' == str[0];
}
static str_t *BlogCopyPreviewPath(BlogRef const blog, strarg_t const hash) {
	return aasprintf("%s/%.2s/%s", blog->cacheDir, hash, hash);
}


static bool gen_pending(BlogRef const blog, strarg_t const path) {
	for(index_t i = 0; i < PENDING_MAX; ++i) {
		if(!blog->pending[i]) continue;
		if(0 == strcmp(blog->pending[i], path)) return true;
	}
	return false;
}
static bool gen_available(BlogRef const blog, strarg_t const path, index_t *const x) {
	for(index_t i = 0; i < PENDING_MAX; ++i) {
		if(blog->pending[i]) continue;
		blog->pending[i] = path;
		*x = i;
		return true;
	}
	return false;
}
static void gen_done(BlogRef const blog, strarg_t const path, index_t const x) {
	async_mutex_lock(blog->pending_mutex);
	assert(path == blog->pending[x]);
	blog->pending[x] = NULL;
	async_cond_broadcast(blog->pending_cond);
	async_mutex_unlock(blog->pending_mutex);
}
static void sendPreview(BlogRef const blog, HTTPConnectionRef const conn, SLNSessionRef const session, strarg_t const URI, strarg_t const path) {
	if(!path) return;

	preview_state const state = {
		.blog = blog,
		.session = session,
		.fileURI = URI,
	};
	TemplateWriteHTTPChunk(blog->entry_start, &preview_cbs, &state, conn);

	if(HTTPConnectionWriteChunkFile(conn, path) >= 0) {
		TemplateWriteHTTPChunk(blog->entry_end, &preview_cbs, &state, conn);
		return;
	}

	// It's okay to accidentally regenerate a preview
	// It's okay to send an error if another thread tried to gen and failed
	// We want to minimize false positives and false negatives
	// In particular, if a million connections request a new file at once, we want to avoid starting gen for each connection before any of them have finished
	// Capping the total number of concurrent gens to PENDING_MAX is not a bad side effect
	index_t x = 0;
	async_mutex_lock(blog->pending_mutex);
	for(;; async_cond_wait(blog->pending_cond, blog->pending_mutex)) {
		if(gen_pending(blog, path)) { x = PENDING_MAX; continue; }
		if(x >= PENDING_MAX) break;
		if(gen_available(blog, path, &x)) break;
	}
	async_mutex_unlock(blog->pending_mutex);

	if(x < PENDING_MAX) {
		SLNFileInfo src[1];
		int rc = SLNSessionGetFileInfo(session, URI, src);
		assert(rc >= 0); // TODO;
		// TODO: We should be able to pass NULL for meta because we
		// don't need a meta-file synthesized at this point.
		SLNSubmissionRef meta = NULL;
		rc = BlogConvert(blog, session, path, &meta, URI, src);
		assert(rc >= 0); // TODO
		SLNSubmissionFree(&meta);
		SLNFileInfoCleanup(src);
		gen_done(blog, path, x);
	}

	if(HTTPConnectionWriteChunkFile(conn, path) < 0) {
		TemplateWriteHTTPChunk(blog->empty, &preview_cbs, &state, conn);
	}

	TemplateWriteHTTPChunk(blog->entry_end, &preview_cbs, &state, conn);
}

static int GET_query(BlogRef const blog, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method) return -1;
	strarg_t qs = NULL;
	if(!URIPath(URI, "/", &qs)) return -1;

	str_t *query_HTMLSafe = NULL;
	SLNFilterRef filter = NULL;
	int rc;

	static strarg_t const fields[] = {
		"q",
	};
	str_t *values[numberof(fields)] = {};
	QSValuesParse(qs, values, fields, numberof(fields));
	query_HTMLSafe = htmlenc(values[0]);
	rc = SLNUserFilterParse(session, values[0], &filter);
	QSValuesCleanup(values, numberof(values));
	if(DB_EACCES == rc) {
		FREE(&query_HTMLSafe);
		return 403;
	}
	if(DB_EINVAL == rc) rc = SLNFilterCreate(session, SLNAllFilterType, &filter);
	if(DB_SUCCESS != rc) {
		FREE(&query_HTMLSafe);
		return 500;
	}
//	SLNFilterPrint(filter, 0); // DEBUG

	SLNFilterOpts opts[1];
	rc = SLNFilterOptsParse(qs, -1, RESULTS_MAX, opts);
	if(DB_SUCCESS != rc) {
		FREE(&query_HTMLSafe);
		SLNFilterFree(&filter);
		return 500;
	}
	int const outdir = opts->outdir;

	size_t count;
	str_t *URIs[RESULTS_MAX];
	rc = SLNSessionCopyFilteredURIs(session, filter, opts, URIs, &count);
	SLNFilterOptsCleanup(opts);
	if(DB_SUCCESS != rc) {
		FREE(&query_HTMLSafe);
		SLNFilterFree(&filter);
		return 500;
	}

	str_t *reponame_HTMLSafe = htmlenc(SLNRepoGetName(blog->repo));

	str_t tmp[URI_MAX+1];

	str_t *account_HTMLSafe;
	if(0 == SLNSessionGetUserID(session)) {
		account_HTMLSafe = htmlenc("Log In");
	} else {
		strarg_t const user = SLNSessionGetUsername(session);
		snprintf(tmp, sizeof(tmp), "Account: %s", user);
		account_HTMLSafe = htmlenc(tmp);
	}

	SLNFilterToUserFilterString(filter, tmp, sizeof(tmp), 0);
	str_t *parsed_HTMLSafe = htmlenc(tmp);


	// TODO: Write a real function for building query strings
	// Don't use ?: GNUism
	// Preserve other query parameters like `dir`
	str_t *firstpage_HTMLSafe = NULL;
	str_t *prevpage_HTMLSafe = NULL;
	str_t *nextpage_HTMLSafe = NULL;
	str_t *lastpage_HTMLSafe = NULL;
	snprintf(tmp, sizeof(tmp), "?q=%s&start=-", query_HTMLSafe ?: "");
	firstpage_HTMLSafe = htmlenc(tmp);
	strarg_t const p = !count ? NULL : URIs[outdir > 0 ? 0 : count-1];
	strarg_t const n = !count ? NULL : URIs[outdir > 0 ? count-1 : 0];
	snprintf(tmp, sizeof(tmp), "?q=%s&start=%s", query_HTMLSafe ?: "", p ?: "");
	prevpage_HTMLSafe = htmlenc(tmp);
	snprintf(tmp, sizeof(tmp), "?q=%s&start=-%s", query_HTMLSafe ?: "", n ?: "");
	nextpage_HTMLSafe = htmlenc(tmp);
	snprintf(tmp, sizeof(tmp), "?q=%s", query_HTMLSafe ?: "");
	lastpage_HTMLSafe = htmlenc(tmp);


	TemplateStaticArg const args[] = {
		{"reponame", reponame_HTMLSafe},
		{"account", account_HTMLSafe},
		{"query", query_HTMLSafe},
		{"parsed", parsed_HTMLSafe},
		{"firstpage", firstpage_HTMLSafe},
		{"prevpage", prevpage_HTMLSafe},
		{"nextpage", nextpage_HTMLSafe},
		{"lastpage", lastpage_HTMLSafe},
		{NULL, NULL},
	};

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	if(0 == SLNSessionGetUserID(session)) {
		HTTPConnectionWriteHeader(conn, "Cache-Control", "no-cache, public");
	} else {
		HTTPConnectionWriteHeader(conn, "Cache-Control", "no-cache, private");
	}
	HTTPConnectionBeginBody(conn);
	TemplateWriteHTTPChunk(blog->header, &TemplateStaticCBs, args, conn);

	// TODO: This is pretty broken. We probably need a whole separate mode.
	SLNFilterRef const coreFilter = SLNFilterUnwrap(filter);
	if(coreFilter && SLNMetadataFilterType == SLNFilterGetType(coreFilter)) {
		strarg_t const field = SLNFilterGetStringArg(coreFilter, 0);
		strarg_t const URI = SLNFilterGetStringArg(coreFilter, 1);
		SLNFileInfo info[1];
		if(0 == strcmp("link", field) && SLNSessionGetFileInfo(session, URI, info) >= 0) {
			str_t *canonical = SLNFormatURI(SLN_INTERNAL_ALGO, info->hash);
			str_t *previewPath = BlogCopyPreviewPath(blog, info->hash);
			sendPreview(blog, conn, session, canonical, previewPath);
			FREE(&previewPath);
			FREE(&canonical);
			SLNFileInfoCleanup(info);
			// TODO: Remember this file and make sure it doesn't show up as a duplicate below.
			if(count > 0) TemplateWriteHTTPChunk(blog->backlinks, &TemplateStaticCBs, args, conn);
		}
	}

	for(size_t i = 0; i < count; i++) {
		str_t algo[SLN_ALGO_SIZE]; // SLN_INTERNAL_ALGO
		str_t hash[SLN_HASH_SIZE];
		SLNParseURI(URIs[i], algo, hash);
		str_t *previewPath = BlogCopyPreviewPath(blog, hash);
		sendPreview(blog, conn, session, URIs[i], previewPath);
		FREE(&previewPath);
	}

	TemplateWriteHTTPChunk(blog->footer, &TemplateStaticCBs, args, conn);
	FREE(&reponame_HTMLSafe);
	FREE(&account_HTMLSafe);
	FREE(&query_HTMLSafe);
	FREE(&parsed_HTMLSafe);
	FREE(&firstpage_HTMLSafe);
	FREE(&prevpage_HTMLSafe);
	FREE(&nextpage_HTMLSafe);
	FREE(&lastpage_HTMLSafe);

	HTTPConnectionWriteChunkv(conn, NULL, 0);
	HTTPConnectionEnd(conn);

	for(size_t i = 0; i < count; i++) FREE(&URIs[i]);
	assert_zeroed(URIs, count);
	SLNFilterFree(&filter);
	return 0;
}

// TODO: Lots of duplication here
static int GET_compose(BlogRef const blog, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method) return -1;
	if(!URIPath(URI, "/compose", NULL)) return -1;

	if(!SLNSessionHasPermission(session, SLN_WRONLY)) return 403;

	str_t *reponame_HTMLSafe = htmlenc(SLNRepoGetName(blog->repo));
	if(!reponame_HTMLSafe) return 500;
	TemplateStaticArg const args[] = {
		{"reponame", reponame_HTMLSafe},
		{"token", "asdf"},
		{NULL, NULL},
	};

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionBeginBody(conn);
	TemplateWriteHTTPChunk(blog->compose, &TemplateStaticCBs, args, conn);
	HTTPConnectionWriteChunkv(conn, NULL, 0);
	HTTPConnectionEnd(conn);

	FREE(&reponame_HTMLSafe);
	return 0;
}
static int GET_upload(BlogRef const blog, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method) return -1;
	if(!URIPath(URI, "/upload", NULL)) return -1;

	if(!SLNSessionHasPermission(session, SLN_WRONLY)) return 403;

	str_t *reponame_HTMLSafe = htmlenc(SLNRepoGetName(blog->repo));
	if(!reponame_HTMLSafe) return 500;
	TemplateStaticArg const args[] = {
		{"reponame", reponame_HTMLSafe},
		{"token", "asdf"},
		{NULL, NULL},
	};

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionBeginBody(conn);
	TemplateWriteHTTPChunk(blog->upload, &TemplateStaticCBs, args, conn);
	HTTPConnectionWriteChunkv(conn, NULL, 0);
	HTTPConnectionEnd(conn);

	FREE(&reponame_HTMLSafe);
	return 0;
}





// TODO
#define BUF_LEN(x) x, sizeof(x)
static int parse_file(BlogRef const blog,
                      SLNSessionRef const session,
                      MultipartFormRef const form,
                      SLNSubmissionRef *const outfile,
                      SLNSubmissionRef *const outmeta)
{
	assert(form);

	SLNSubmissionRef file = NULL;
	SLNSubmissionRef meta = NULL;
	SLNFileInfo src[1] = {};
	str_t *htmlpath = NULL;
	int rc = 0;

	strarg_t const fields[] = {
		"content-type",
		"content-disposition",
	};
	char content_type[100];
	char content_disposition[100];
	uv_buf_t values[] = {
		uv_buf_init(BUF_LEN(content_type)),
		uv_buf_init(BUF_LEN(content_disposition)),
	};
	assert(numberof(fields) == numberof(values));
	rc = MultipartFormReadHeadersStatic(form, values, fields, numberof(values));
	if(rc < 0) goto cleanup;

	strarg_t type;
	if(0 == strcmp("form-data; name=\"markdown\"", content_disposition)) {
		type = "text/markdown; charset=utf-8";
	} else {
		type = content_type;
	}

	rc = SLNSubmissionCreate(session, type, &file);
	if(rc < 0) goto cleanup;
	for(;;) {
		uv_buf_t buf[1];
		rc = MultipartFormReadData(form, buf);
		if(rc < 0) goto cleanup;
		if(0 == buf->len) break;
		rc = SLNSubmissionWrite(file, (byte_t const *)buf->base, buf->len);
		if(rc < 0) goto cleanup;
	}
	rc = SLNSubmissionEnd(file);
	if(rc < 0) goto cleanup;

	rc = SLNSubmissionGetFileInfo(file, src);
	if(rc < 0) goto cleanup;

	htmlpath = BlogCopyPreviewPath(blog, src->hash);
	if(!htmlpath) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	strarg_t const URI = SLNSubmissionGetPrimaryURI(file);
	rc = BlogConvert(blog, session, htmlpath, &meta, URI, src);
	if(rc < 0) goto cleanup;

	*outfile = file; file = NULL;
	*outmeta = meta; meta = NULL;

cleanup:
	SLNSubmissionFree(&file);
	SLNSubmissionFree(&meta);
	SLNFileInfoCleanup(src);
	FREE(&htmlpath);

	return rc;
}
static int POST_post(BlogRef const blog,
                     SLNSessionRef const session,
                     HTTPConnectionRef const conn,
                     HTTPMethod const method,
                     strarg_t const URI,
                     HTTPHeadersRef const headers)
{
	if(HTTP_POST != method) return -1;
	if(!URIPath(URI, "/post", NULL)) return -1;

	// TODO: CSRF token
	strarg_t const formtype = HTTPHeadersGet(headers, "content-type"); 
	uv_buf_t boundary[1];
	int rc = MultipartBoundaryFromType(formtype, boundary);
	if(rc < 0) return 400;

	MultipartFormRef form = NULL;
	rc = MultipartFormCreate(conn, boundary, &form);
	if(rc < 0) {
		return 500;
	}

	SLNSubmissionRef sub = NULL;
	SLNSubmissionRef meta = NULL;
	rc = parse_file(blog, session, form, &sub, &meta);
	if(UV_EACCES == rc) {
		MultipartFormFree(&form);
		return 403;
	}
	if(rc < 0) {
		MultipartFormFree(&form);
		return 500;
	}

	// TODO: Create an additional meta-file with submission-specific
	// information.
	// - Submission time
	// - File name
	// - Submitter name?
	// - Comment?
	// Note that these meta-files will be small, so it would be good to
	// have more optimizations for small files (like storing files under
	// 2K in the DB rather than in the file system).

	SLNSubmissionRef subs[] = { sub, meta };
	int err = SLNSubmissionBatchStore(subs, numberof(subs));

	SLNSubmissionFree(&sub);
	SLNSubmissionFree(&meta);
	MultipartFormFree(&form);

	if(err < 0) return 500;

	HTTPConnectionWriteResponse(conn, 303, "See Other");
	HTTPConnectionWriteHeader(conn, "Location", "/");
	HTTPConnectionWriteContentLength(conn, 0);
	HTTPConnectionBeginBody(conn);
	HTTPConnectionEnd(conn);
	return 0;
}

static int GET_account(BlogRef const blog, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method) return -1;
	if(!URIPath(URI, "/account", NULL)) return -1;

	str_t *reponame_HTMLSafe = htmlenc(SLNRepoGetName(blog->repo));
	if(!reponame_HTMLSafe) return 500;
	TemplateStaticArg const args[] = {
		{"reponame", reponame_HTMLSafe},
		{"token", "asdf"}, // TODO
		{"userlen", "32"},
		{"passlen", "64"},
		{NULL, NULL},
	};

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionBeginBody(conn);
	TemplateWriteHTTPChunk(blog->login, &TemplateStaticCBs, args, conn);
	HTTPConnectionWriteChunkv(conn, NULL, 0);
	HTTPConnectionEnd(conn);

	FREE(&reponame_HTMLSafe);
	return 0;
}
static int POST_auth(BlogRef const blog, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_POST != method) return -1;
	if(!URIPath(URI, "/auth", NULL)) return -1;

	str_t formdata[AUTH_FORM_MAX+1];
	ssize_t len = HTTPConnectionReadBodyStatic(conn, (byte_t *)formdata, AUTH_FORM_MAX);
	if(UV_EMSGSIZE == len) return 413; // Request Entity Too Large
	if(len < 0) return 500;
	formdata[len] = '\0';

	SLNSessionCacheRef const cache = SLNRepoGetSessionCache(blog->repo);
	static strarg_t const fields[] = {
		"action-login",
		"action-register",
		"user",
		"pass",
		"token", // TODO: CSRF protection
	};
	str_t *values[numberof(fields)] = {};
	QSValuesParse(formdata, values, fields, numberof(fields));
	if(values[1]) {
		QSValuesCleanup(values, numberof(values));
		return 501; // TODO: Not Implemented
	}
	if(!values[0]) {
		QSValuesCleanup(values, numberof(values));
		return 400; // Not login?
	}
	SLNSessionRef s;
	int rc = SLNSessionCacheCreateSession(cache, values[2], values[3], &s); // TODO
	QSValuesCleanup(values, numberof(values));

	if(DB_SUCCESS != rc) {
		HTTPConnectionWriteResponse(conn, 303, "See Other");
		HTTPConnectionWriteHeader(conn, "Location", "/account?err=1");
		HTTPConnectionWriteContentLength(conn, 0);
		HTTPConnectionBeginBody(conn);
		HTTPConnectionEnd(conn);
		return 0;
	}

	str_t *cookie = SLNSessionCopyCookie(s);
	SLNSessionRelease(&s);
	if(!cookie) return 500;

	HTTPConnectionWriteResponse(conn, 303, "See Other");
	HTTPConnectionWriteHeader(conn, "Location", "/");
	HTTPConnectionWriteSetCookie(conn, cookie, "/", 60 * 60 * 24 * 365);
	HTTPConnectionWriteContentLength(conn, 0);
	HTTPConnectionBeginBody(conn);
	HTTPConnectionEnd(conn);

	FREE(&cookie);
	return 0;
}

void BlogFree(BlogRef *const blogptr);

static bool load_template(BlogRef const blog, strarg_t const name, TemplateRef *const out) {
	assert(out);
	assert(blog->dir);
	str_t path[PATH_MAX];
	int rc = snprintf(path, PATH_MAX, "%s/template/%s", blog->dir, name);
	if(rc < 0 || rc >= PATH_MAX) return false;
	TemplateRef t = TemplateCreateFromPath(path);
	if(!t) {
		fprintf(stderr, "Blog couldn't load template at %s\n", path);
		return false;
	}
	*out = t;
	return true;
}
BlogRef BlogCreate(SLNRepoRef const repo) {
	assertf(repo, "Blog requires valid repo");

	BlogRef blog = calloc(1, sizeof(struct Blog));
	if(!blog) return NULL;
	blog->repo = repo;

	blog->dir = aasprintf("%s/blog", SLNRepoGetDir(repo));
	blog->cacheDir = aasprintf("%s/blog", SLNRepoGetCacheDir(repo));
	if(!blog->dir || !blog->cacheDir) {
		BlogFree(&blog);
		return NULL;
	}

	if(
		!load_template(blog, "header.html", &blog->header) ||
		!load_template(blog, "footer.html", &blog->footer) ||
		!load_template(blog, "backlinks.html", &blog->backlinks) ||
		!load_template(blog, "entry-start.html", &blog->entry_start) ||
		!load_template(blog, "entry-end.html", &blog->entry_end) ||
		!load_template(blog, "preview.html", &blog->preview) ||
		!load_template(blog, "empty.html", &blog->empty) ||
		!load_template(blog, "compose.html", &blog->compose) ||
		!load_template(blog, "upload.html", &blog->upload) ||
		!load_template(blog, "login.html", &blog->login)
	) {
		BlogFree(&blog);
		return NULL;
	}


	async_mutex_init(blog->pending_mutex, 0);
	async_cond_init(blog->pending_cond, 0);

	return blog;
}
void BlogFree(BlogRef *const blogptr) {
	BlogRef blog = *blogptr;
	if(!blog) return;

	blog->repo = NULL;

	FREE(&blog->dir);
	FREE(&blog->cacheDir);

	TemplateFree(&blog->header);
	TemplateFree(&blog->footer);
	TemplateFree(&blog->backlinks);
	TemplateFree(&blog->entry_start);
	TemplateFree(&blog->entry_end);
	TemplateFree(&blog->preview);
	TemplateFree(&blog->empty);
	TemplateFree(&blog->compose);
	TemplateFree(&blog->upload);
	TemplateFree(&blog->login);

	async_mutex_destroy(blog->pending_mutex);
	async_cond_destroy(blog->pending_cond);

	assert_zeroed(blog, 1);
	FREE(blogptr); blog = NULL;
}
int BlogDispatch(BlogRef const blog, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	int rc = -1;
	rc = rc >= 0 ? rc : GET_query(blog, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_compose(blog, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_upload(blog, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : POST_post(blog, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_account(blog, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : POST_auth(blog, session, conn, method, URI, headers);

	if(403 == rc) {
		HTTPConnectionWriteResponse(conn, 303, "See Other");
		HTTPConnectionWriteContentLength(conn, 0);
		HTTPConnectionWriteHeader(conn, "Location", "/account");
		HTTPConnectionBeginBody(conn);
		HTTPConnectionEnd(conn);
		return 0;
	}
	if(rc >= 0) return rc; // TODO: Pretty 404 pages, etc.

	if(HTTP_GET != method && HTTP_HEAD != method) return -1;

	if(strchr(URI, '?')) return 501; // TODO: Not Implemented
	if(strstr(URI, "..")) return 400;
	str_t path[PATH_MAX];
	rc = snprintf(path, PATH_MAX, "%s/static/%s", blog->dir, URI);
	if(rc >= PATH_MAX) return 414; // Request-URI Too Large
	if(rc < 0) return 500;

	HTTPConnectionSendFile(conn, path, NULL, -1); // TODO: Determine file type.
	return 0;
}

