#define _GNU_SOURCE
#include <yajl/yajl_tree.h>
#include <limits.h>
#include "common.h"
#include "async/async.h"
#include "EarthFS.h"
#include "Template.h"
#include "http/HTTPServer.h"
#include "http/HTTPHeaders.h"
#include "http/MultipartForm.h"
#include "http/QueryString.h"

#define RESULTS_MAX 50
#define PENDING_MAX 4
#define BUFFER_SIZE (1024 * 8)

typedef struct {
	strarg_t content_type;
	strarg_t content_disposition;
} BlogSubmissionHeaders;
static strarg_t const BlogSubmissionFields[] = {
	"content-type",
	"content-disposition",
};

typedef struct Blog* BlogRef;

struct Blog {
	EFSRepoRef repo;

	str_t *dir;
	str_t *staticDir;
	str_t *templateDir;
	str_t *cacheDir;

	TemplateRef header;
	TemplateRef footer;
	TemplateRef entry_start;
	TemplateRef entry_end;
	TemplateRef preview;
	TemplateRef empty;
	TemplateRef compose;
	TemplateRef login;

	async_mutex_t pending_mutex[1];
	async_cond_t pending_cond[1];
	strarg_t pending[PENDING_MAX];
};

// TODO: Real public API.
bool URIPath(strarg_t const URI, strarg_t const path, strarg_t *const qs);

static bool emptystr(strarg_t const str) {
	return !str || '\0' == str[0];
}
static str_t *BlogCopyPreviewPath(BlogRef const blog, strarg_t const hash) {
	str_t *path;
	if(asprintf(&path, "%s/%.2s/%s", blog->cacheDir, hash, hash) < 0) return NULL;
	return path;
}


int markdown_convert(strarg_t const dst, strarg_t const src);
static int genMarkdownPreview(BlogRef const blog, EFSSessionRef const session, strarg_t const URI, strarg_t const tmp, EFSFileInfo const *const info) {
	if(
		0 != strcasecmp("text/markdown; charset=utf-8", info->type) &&
		0 != strcasecmp("text/markdown", info->type)
	) return -1; // TODO: Other types, plugins, w/e.

	async_pool_enter(NULL);
	int rc = markdown_convert(tmp, info->path);
	async_pool_leave(NULL);
	return rc;
}


static int genPlainTextPreview(BlogRef const blog, EFSSessionRef const session, strarg_t const URI, strarg_t const tmp, EFSFileInfo const *const info) {
	if(
		0 != strcasecmp("text/plain; charset=utf-8", info->type) &&
		0 != strcasecmp("text/plain", info->type)
	) return -1; // TODO: Other types, plugins, w/e.

	// TODO
	return -1;
}


typedef struct {
	BlogRef blog;
	strarg_t fileURI;
} preview_state;
static str_t *preview_metadata(preview_state const *const state, strarg_t const var) {
	strarg_t unsafe = NULL;
	str_t buf[URI_MAX];
	if(0 == strcmp(var, "rawURI")) {
		str_t algo[EFS_ALGO_SIZE]; // EFS_INTERNAL_ALGO
		str_t hash[EFS_HASH_SIZE];
		EFSParseURI(state->fileURI, algo, hash);
		snprintf(buf, sizeof(buf), "/efs/file/%s/%s", algo, hash);
		unsafe = buf;
	}
	if(0 == strcmp(var, "queryURI")) {
		// TODO: Query string escaping
		snprintf(buf, sizeof(buf), "/?q=%s", state->fileURI);
		unsafe = buf;
	}
	if(0 == strcmp(var, "hashURI")) {
		unsafe = state->fileURI;
	}
	if(unsafe) return htmlenc(unsafe);

	DB_env *db = NULL;
	int rc = EFSRepoDBOpen(state->blog->repo, &db);
	assert(rc >= 0);
	DB_txn *txn = NULL;
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	assert(DB_SUCCESS == rc);

	DB_cursor *metafiles = NULL;
	rc = db_cursor_open(txn, &metafiles);
	assert(DB_SUCCESS == rc);

	DB_cursor *values = NULL;
	rc = db_cursor_open(txn, &values);
	assert(DB_SUCCESS == rc);

	DB_RANGE(metaFileIDs, DB_VARINT_MAX + DB_INLINE_MAX);
	db_bind_uint64(metaFileIDs->min, EFSTargetURIAndMetaFileID);
	db_bind_string(txn, metaFileIDs->min, state->fileURI);
	db_range_genmax(metaFileIDs);
	DB_val metaFileID_key[1];
	rc = db_cursor_firstr(metafiles, metaFileIDs, metaFileID_key, NULL, +1);
	assert(DB_SUCCESS == rc || DB_NOTFOUND == rc);
	for(; DB_SUCCESS == rc; rc = db_cursor_nextr(metafiles, metaFileIDs, metaFileID_key, NULL, +1)) {
		uint64_t const table = db_read_uint64(metaFileID_key);
		assert(EFSTargetURIAndMetaFileID == table);
		strarg_t const u = db_read_string(txn, metaFileID_key);
		assert(0 == strcmp(state->fileURI, u));
		uint64_t const metaFileID = db_read_uint64(metaFileID_key);
		DB_RANGE(vrange, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1);
		db_bind_uint64(vrange->min, EFSMetaFileIDFieldAndValue);
		db_bind_uint64(vrange->min, metaFileID);
		db_bind_string(txn, vrange->min, var);
		db_range_genmax(vrange);
		DB_val value_val[1];
		rc = db_cursor_firstr(values, vrange, value_val, NULL, +1);
		assert(DB_SUCCESS == rc || DB_NOTFOUND == rc);
		for(; DB_SUCCESS == rc; rc = db_cursor_nextr(values, vrange, value_val, NULL, +1)) {
			uint64_t const table2 = db_read_uint64(value_val);
			assert(EFSMetaFileIDFieldAndValue == table2);
			uint64_t const m = db_read_uint64(value_val);
			assert(metaFileID == m);
			strarg_t const f = db_read_string(txn, value_val);
			assert(0 == strcmp(var, f));
			strarg_t const value = db_read_string(txn, value_val);
			if(0 == strcmp("", value)) continue;
			unsafe = value;
			break;
		}
		if(unsafe) break;
	}

	db_cursor_close(values); values = NULL;
	db_cursor_close(metafiles); metafiles = NULL;

	if(!unsafe) {
		if(0 == strcmp(var, "thumbnailURI")) unsafe = "/file.png";
		if(0 == strcmp(var, "title")) unsafe = "(no title)";
		if(0 == strcmp(var, "description")) unsafe = "(no description)";
	}
	str_t *result = htmlenc(unsafe);

	db_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(state->blog->repo, &db);
	return result;
}
static void preview_free(preview_state const *const state, strarg_t const var, str_t **const val) {
	FREE(val);
}
static TemplateArgCBs const preview_cbs = {
	.lookup = (str_t *(*)())preview_metadata,
	.free = (void (*)())preview_free,
};

static int genGenericPreview(BlogRef const blog, EFSSessionRef const session, strarg_t const URI, strarg_t const tmp, EFSFileInfo const *const info) {
	uv_file file = async_fs_open(tmp, O_CREAT | O_EXCL | O_WRONLY, 0400);
	if(file < 0) return -1;

	preview_state const state = {
		.blog = blog,
		.fileURI = URI,
	};
	int const e1 = TemplateWriteFile(blog->preview, &preview_cbs, &state, file);

	int const e2 = async_fs_close(file); file = -1;
	if(e1 < 0 || e2 < 0) {
		async_fs_unlink(tmp);
		return -1;
	}
	return 0;
}

static int genPreview(BlogRef const blog, EFSSessionRef const session, strarg_t const URI, strarg_t const path) {
	EFSFileInfo info[1];
	if(EFSSessionGetFileInfo(session, URI, info) < 0) return -1;
	str_t *tmp = EFSRepoCopyTempPath(blog->repo);
	if(!tmp) {
		EFSFileInfoCleanup(info);
		return -1;
	}

	bool success = false;
	success = success || genMarkdownPreview(blog, session, URI, tmp, info) >= 0;
	success = success || genPlainTextPreview(blog, session, URI, tmp, info) >= 0;
	success = success || genGenericPreview(blog, session, URI, tmp, info) >= 0;
	if(!success) async_fs_mkdirp_dirname(tmp, 0700);
	success = success || genMarkdownPreview(blog, session, URI, tmp, info) >= 0;
	success = success || genPlainTextPreview(blog, session, URI, tmp, info) >= 0;
	success = success || genGenericPreview(blog, session, URI, tmp, info) >= 0;

	EFSFileInfoCleanup(info);
	if(!success) {
		FREE(&tmp);
		return -1;
	}

	success = false;
	success = success || async_fs_link(tmp, path) >= 0;
	if(!success) async_fs_mkdirp_dirname(path, 0700);
	success = success || async_fs_link(tmp, path) >= 0;

	async_fs_unlink(tmp);
	FREE(&tmp);
	return success ? 0 : -1;
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
static void sendPreview(BlogRef const blog, HTTPConnectionRef const conn, EFSSessionRef const session, strarg_t const URI, strarg_t const path) {
	if(!path) return;

	preview_state const state = {
		.blog = blog,
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
		(void)genPreview(blog, session, URI, path);
		gen_done(blog, path, x);
	}

	if(HTTPConnectionWriteChunkFile(conn, path) < 0) {
		TemplateWriteHTTPChunk(blog->empty, &preview_cbs, &state, conn);
	}

	TemplateWriteHTTPChunk(blog->entry_end, &preview_cbs, &state, conn);
}

typedef struct {
	str_t *query;
	str_t *file; // It'd be nice if we didn't need a separate parameter for this.
} BlogQueryValues;
static strarg_t const BlogQueryFields[] = {
	"q",
	"f",
};
static int GET_query(BlogRef const blog, EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method) return -1;
	strarg_t qs = NULL;
	if(!URIPath(URI, "/", &qs)) return -1;

	BlogQueryValues *params = QSValuesCopy(qs, BlogQueryFields, numberof(BlogQueryFields));
	EFSFilterRef filter = EFSUserFilterParse(params->query);
	if(!filter) filter = EFSFilterCreate(EFSAllFilterType);
	QSValuesFree((QSValues *)&params, numberof(BlogQueryFields));

//	EFSFilterPrint(filter, 0); // DEBUG

	str_t **URIs = EFSSessionCopyFilteredURIs(session, filter, RESULTS_MAX);
	if(!URIs) {
		EFSFilterFree(&filter);
		return 500;
	}

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionBeginBody(conn);

	str_t q[200];
	size_t qlen = EFSFilterToUserFilterString(filter, q, sizeof(q), 0);
	str_t *q_HTMLSafe = htmlenc(q);
	TemplateStaticArg const args[] = {
		{"q", q_HTMLSafe},
		{NULL, NULL},
	};

	TemplateWriteHTTPChunk(blog->header, &TemplateStaticCBs, args, conn);

	// TODO: This is pretty broken. We probably need a whole separate mode.
	EFSFilterRef const coreFilter = EFSFilterUnwrap(filter);
	if(coreFilter && EFSMetadataFilterType == EFSFilterGetType(coreFilter)) {
		strarg_t const field = EFSFilterGetStringArg(coreFilter, 0);
		strarg_t const URI = EFSFilterGetStringArg(coreFilter, 1);
		EFSFileInfo info[1];
		if(0 == strcmp("link", field) && EFSSessionGetFileInfo(session, URI, info) >= 0) {
			str_t *canonical = EFSFormatURI(EFS_INTERNAL_ALGO, info->hash);
			str_t *previewPath = BlogCopyPreviewPath(blog, info->hash);
			sendPreview(blog, conn, session, canonical, previewPath);
			FREE(&previewPath);
			FREE(&canonical);
			EFSFileInfoCleanup(info);
			// TODO: Remember this file and make sure it doesn't show up as a duplicate below.
		}
	}

	for(index_t i = 0; URIs[i]; ++i) {
		str_t algo[EFS_ALGO_SIZE]; // EFS_INTERNAL_ALGO
		str_t hash[EFS_HASH_SIZE];
		EFSParseURI(URIs[i], algo, hash);
		str_t *previewPath = BlogCopyPreviewPath(blog, hash);
		sendPreview(blog, conn, session, URIs[i], previewPath);
		FREE(&previewPath);
	}

	TemplateWriteHTTPChunk(blog->footer, &TemplateStaticCBs, args, conn);
	FREE(&q_HTMLSafe);

	HTTPConnectionWriteChunkv(conn, NULL, 0);
	HTTPConnectionEnd(conn);

	for(index_t i = 0; URIs[i]; ++i) FREE(&URIs[i]);
	FREE(&URIs);
	EFSFilterFree(&filter);
	return 0;
}
static int GET_new(BlogRef const blog, EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method) return -1;
	if(!URIPath(URI, "/compose", NULL)) return -1;

	// TODO
	TemplateStaticArg const args[] = {
		{"reponame", "Blog"},
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
	return 0;
}
static int POST_submit(BlogRef const blog, EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_POST != method) return -1;
	if(!URIPath(URI, "/submission", NULL)) return -1;

	// TODO: CSRF token
	strarg_t const formtype = HTTPHeadersGet(headers, "content-type"); 
	MultipartFormRef form = MultipartFormCreate(conn, formtype, BlogSubmissionFields, numberof(BlogSubmissionFields));
	FormPartRef const part = MultipartFormGetPart(form);
	BlogSubmissionHeaders const *const fheaders = FormPartGetHeaders(part);
	// TODO: Handle failures, e.g. submission of non-multipart data

	strarg_t type;
	if(0 == strcmp("form-data; name=\"markdown\"", fheaders->content_disposition)) {
		type = "text/markdown; charset=utf-8";
	} else {
		type = fheaders->content_type;
	}

	strarg_t title = NULL; // TODO: Get file name from form part.

	EFSSubmissionRef sub = NULL;
	EFSSubmissionRef meta = NULL;
	if(EFSSubmissionCreateQuickPair(session, type, (ssize_t (*)())FormPartGetBuffer, part, title, &sub, &meta) < 0) {
		MultipartFormFree(&form);
		return 500;
	}

	EFSSubmissionRef subs[2] = { sub, meta };
	int err = EFSSubmissionBatchStore(subs, numberof(subs));

	EFSSubmissionFree(&sub);
	EFSSubmissionFree(&meta);
	MultipartFormFree(&form);

	if(err < 0) return 500;

	HTTPConnectionWriteResponse(conn, 303, "See Other");
	HTTPConnectionWriteHeader(conn, "Location", "/");
	HTTPConnectionWriteContentLength(conn, 0);
	HTTPConnectionBeginBody(conn);
	HTTPConnectionEnd(conn);
	return 0;
}

static int GET_login(BlogRef const blog, EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method) return -1;
	if(!URIPath(URI, "/login", NULL)) return -1;

	// TODO
	TemplateStaticArg const args[] = {
		{"reponame", "Blog"},
		{"token", "asdf"},
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
	return 0;
}
static int POST_auth(BlogRef const blog, EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_POST != method) return -1;
	if(!URIPath(URI, "/auth", NULL)) return -1;


// TODO: This is pretty ugly...
// Do we need a streaming form-data parser?
// Or a parser that uses our old predefined-fields trick?
// It'd be good to cap the lengths

#define FORMDATA_MAX 1024
	str_t formdata[FORMDATA_MAX];
	size_t len = 0;
	for(;;) {
		uv_buf_t buf[1];
		int rc = HTTPConnectionReadBody(conn, buf);
		if(rc < 0) return 500;
		if(!buf->len) break;
		if(len+buf->len >= FORMDATA_MAX) return 400; // TODO
		memcpy(formdata, buf->base, buf->len);
		len += buf->len;
		formdata[len] = '\0';
	}


	EFSRepoRef const repo = EFSSessionGetRepo(session);
	str_t *cookie = NULL;
	int rc = EFSRepoCookieCreate(repo, "ben", "testing", &cookie); // TODO
	if(0 != rc) {
		FREE(&cookie);
		return 403;
	}

	HTTPConnectionWriteResponse(conn, 303, "See Other");
	HTTPConnectionWriteHeader(conn, "Location", "/");
	HTTPConnectionWriteSetCookie(conn, "s", cookie, "/", 60 * 60 * 24 * 365);
	HTTPConnectionWriteContentLength(conn, 0);
	HTTPConnectionBeginBody(conn);
	HTTPConnectionEnd(conn);

	FREE(&cookie);
	return 0;
}

void BlogFree(BlogRef *const blogptr);

BlogRef BlogCreate(EFSRepoRef const repo) {
	assertf(repo, "Blog requires valid repo");

	BlogRef blog = calloc(1, sizeof(struct Blog));
	if(!blog) return NULL;
	blog->repo = repo;

	if(
		asprintf(&blog->dir, "%s/blog", EFSRepoGetDir(repo)) < 0 ||
		asprintf(&blog->staticDir, "%s/static", blog->dir) < 0 ||
		asprintf(&blog->templateDir, "%s/template", blog->dir) < 0 ||
		asprintf(&blog->cacheDir, "%s/blog", EFSRepoGetCacheDir(repo)) < 0
	) {
		BlogFree(&blog);
		return NULL;
	}

	str_t path[PATH_MAX];
	snprintf(path, PATH_MAX, "%s/header.html", blog->templateDir);
	blog->header = TemplateCreateFromPath(path);
	snprintf(path, PATH_MAX, "%s/footer.html", blog->templateDir);
	blog->footer = TemplateCreateFromPath(path);
	snprintf(path, PATH_MAX, "%s/entry-start.html", blog->templateDir);
	blog->entry_start = TemplateCreateFromPath(path);
	snprintf(path, PATH_MAX, "%s/entry-end.html", blog->templateDir);
	blog->entry_end = TemplateCreateFromPath(path);
	snprintf(path, PATH_MAX, "%s/preview.html", blog->templateDir);
	blog->preview = TemplateCreateFromPath(path);
	snprintf(path, PATH_MAX, "%s/empty.html", blog->templateDir);
	blog->empty = TemplateCreateFromPath(path);
	snprintf(path, PATH_MAX, "%s/compose.html", blog->templateDir);
	blog->compose = TemplateCreateFromPath(path);
	snprintf(path, PATH_MAX, "%s/login.html", blog->templateDir);
	blog->login = TemplateCreateFromPath(path);

	async_mutex_init(blog->pending_mutex, 0);
	async_cond_init(blog->pending_cond, 0);

	return blog;
}
void BlogFree(BlogRef *const blogptr) {
	BlogRef blog = *blogptr;
	if(!blog) return;

	FREE(&blog->dir);
	FREE(&blog->staticDir);
	FREE(&blog->templateDir);
	FREE(&blog->cacheDir);

	TemplateFree(&blog->header);
	TemplateFree(&blog->footer);
	TemplateFree(&blog->entry_start);
	TemplateFree(&blog->entry_end);
	TemplateFree(&blog->preview);
	TemplateFree(&blog->empty);
	TemplateFree(&blog->compose);
	TemplateFree(&blog->login);

	async_mutex_destroy(blog->pending_mutex);
	async_cond_destroy(blog->pending_cond);

	FREE(blogptr); blog = NULL;
}
int BlogDispatch(BlogRef const blog, EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	int rc = -1;
	rc = rc >= 0 ? rc : GET_query(blog, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_new(blog, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : POST_submit(blog, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_login(blog, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : POST_auth(blog, session, conn, method, URI, headers);
	if(rc >= 0) return rc;

	if(HTTP_GET != method && HTTP_HEAD != method) return -1;

	// TODO: Ignore query parameters, check for `..` (security critical).
	str_t *path;
	int const plen = asprintf(&path, "%s%s", blog->staticDir, URI);
	if(plen < 0) return 500;

	HTTPConnectionSendFile(conn, path, NULL, -1); // TODO: Determine file type.
	FREE(&path);
	return 0;
}

