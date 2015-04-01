#include <yajl/yajl_tree.h>
#include <limits.h>
#include "../http/HTTPServer.h"
#include "../http/HTTPHeaders.h"
#include "../http/MultipartForm.h"
#include "../http/QueryString.h"
#include "../StrongLink.h"
#include "Template.h"

#define RESULTS_MAX 50
#define PENDING_MAX 4
#define BUFFER_SIZE (1024 * 8)

typedef struct Blog* BlogRef;

struct Blog {
	SLNRepoRef repo;

	str_t *dir;
	str_t *cacheDir;

	TemplateRef header;
	TemplateRef footer;
	TemplateRef entry_start;
	TemplateRef entry_end;
	TemplateRef preview;
	TemplateRef empty;
	TemplateRef compose;
	TemplateRef upload;
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
	return aasprintf("%s/%.2s/%s", blog->cacheDir, hash, hash);
}


int markdown_convert(strarg_t const dst, strarg_t const src);
static int genMarkdownPreview(BlogRef const blog, SLNSessionRef const session, strarg_t const URI, strarg_t const tmp, SLNFileInfo const *const info) {
	if(
		0 != strcasecmp("text/markdown; charset=utf-8", info->type) &&
		0 != strcasecmp("text/markdown", info->type) &&
		0 != strcasecmp("text/x-markdown; charset=utf-8", info->type) &&
		0 != strcasecmp("text/x-markdown", info->type)
	) return -1; // TODO: Other types, plugins, w/e.

	async_pool_enter(NULL);
	int rc = markdown_convert(tmp, info->path);
	async_pool_leave(NULL);
	return rc;
}


static int genPlainTextPreview(BlogRef const blog, SLNSessionRef const session, strarg_t const URI, strarg_t const tmp, SLNFileInfo const *const info) {
	if(
		0 != strcasecmp("text/plain; charset=utf-8", info->type) &&
		0 != strcasecmp("text/plain", info->type)
	) return -1; // TODO: Other types, plugins, w/e.

	// TODO
	return -1;
}


typedef struct {
	BlogRef blog;
	SLNSessionRef session;
	strarg_t fileURI;
} preview_state;
static str_t *preview_metadata(preview_state const *const state, strarg_t const var) {
	strarg_t unsafe = NULL;
	str_t buf[URI_MAX];
	if(0 == strcmp(var, "rawURI")) {
		str_t algo[SLN_ALGO_SIZE]; // SLN_INTERNAL_ALGO
		str_t hash[SLN_HASH_SIZE];
		SLNParseURI(state->fileURI, algo, hash);
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

	str_t value[1024 * 4];
	int rc = SLNSessionGetValueForField(state->session, value, sizeof(value), state->fileURI, var);
	if(DB_SUCCESS == rc && '\0' != value[0]) unsafe = value;

	if(!unsafe) {
		if(0 == strcmp(var, "thumbnailURI")) unsafe = "/file.png";
		if(0 == strcmp(var, "title")) unsafe = "(no title)";
		if(0 == strcmp(var, "description")) unsafe = "(no description)";
	}
	str_t *result = htmlenc(unsafe);

	return result;
}
static void preview_free(preview_state const *const state, strarg_t const var, str_t **const val) {
	FREE(val);
}
static TemplateArgCBs const preview_cbs = {
	.lookup = (str_t *(*)())preview_metadata,
	.free = (void (*)())preview_free,
};

static int genGenericPreview(BlogRef const blog, SLNSessionRef const session, strarg_t const URI, strarg_t const tmp, SLNFileInfo const *const info) {
	uv_file file = async_fs_open(tmp, O_CREAT | O_EXCL | O_WRONLY, 0400);
	if(file < 0) return -1;

	preview_state const state = {
		.blog = blog,
		.session = session,
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

static int genPreview(BlogRef const blog, SLNSessionRef const session, strarg_t const URI, strarg_t const path) {
	SLNFileInfo info[1];
	if(SLNSessionGetFileInfo(session, URI, info) < 0) return -1;
	str_t *tmp = SLNRepoCopyTempPath(blog->repo);
	if(!tmp) {
		SLNFileInfoCleanup(info);
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

	SLNFileInfoCleanup(info);
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
static int GET_query(BlogRef const blog, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method) return -1;
	strarg_t qs = NULL;
	if(!URIPath(URI, "/", &qs)) return -1;

	if(!SLNSessionHasPermission(session, SLN_RDONLY)) return 403;

	BlogQueryValues *params = QSValuesCopy(qs, BlogQueryFields, numberof(BlogQueryFields));
	SLNFilterRef filter = SLNUserFilterParse(params->query);
	if(!filter) filter = SLNFilterCreate(SLNAllFilterType);
	QSValuesFree((QSValues *)&params, numberof(BlogQueryFields));

//	SLNFilterPrint(filter, 0); // DEBUG

	// TODO: str_t *URIs[RESULTS_MAX]; ?
	str_t **URIs = SLNSessionCopyFilteredURIs(session, filter, RESULTS_MAX);
	if(!URIs) {
		SLNFilterFree(&filter);
		return 500;
	}

	str_t *reponame_HTMLSafe = htmlenc(SLNRepoGetName(SLNSessionGetRepo(session)));

	str_t q[200];
	size_t qlen = SLNFilterToUserFilterString(filter, q, sizeof(q), 0);
	str_t *q_HTMLSafe = htmlenc(q);

	if(!reponame_HTMLSafe || !q_HTMLSafe) {
		FREE(&reponame_HTMLSafe);
		FREE(&q_HTMLSafe);
		for(index_t i = 0; URIs[i]; ++i) FREE(&URIs[i]); // TODO
		FREE(&URIs);
		SLNFilterFree(&filter);
		return 500;
	}

	TemplateStaticArg const args[] = {
		{"reponame", reponame_HTMLSafe},
		{"q", q_HTMLSafe},
		{NULL, NULL},
	};

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	if(SLN_RDONLY & SLNRepoGetPublicMode(blog->repo)) {
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
		}
	}

	for(index_t i = 0; URIs[i]; ++i) {
		str_t algo[SLN_ALGO_SIZE]; // SLN_INTERNAL_ALGO
		str_t hash[SLN_HASH_SIZE];
		SLNParseURI(URIs[i], algo, hash);
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
	SLNFilterFree(&filter);
	return 0;
}

// TODO: Lots of duplication here
static int GET_new(BlogRef const blog, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method) return -1;
	if(!URIPath(URI, "/new", NULL)) return -1;

	if(!SLNSessionHasPermission(session, SLN_WRONLY)) return 403;

	str_t *reponame_HTMLSafe = htmlenc(SLNRepoGetName(SLNSessionGetRepo(session)));
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
static int GET_up(BlogRef const blog, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method) return -1;
	if(!URIPath(URI, "/up", NULL)) return -1;

	if(!SLNSessionHasPermission(session, SLN_WRONLY)) return 403;

	str_t *reponame_HTMLSafe = htmlenc(SLNRepoGetName(SLNSessionGetRepo(session)));
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





#include <regex.h>
#include <yajl/yajl_gen.h>

#define FTS_MAX (1024 * 50)

// TODO
static int quickpair(SLNSessionRef const session, MultipartFormRef const form, strarg_t const title, SLNSubmissionRef *const outSub, SLNSubmissionRef *const outMeta) {
assert(form);

// TODO
#define STR_LEN(x) x, sizeof(x)+1
#define BUF_LEN(x) x, sizeof(x)

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
	int rc = MultipartFormReadStaticHeaders(form, values, fields, numberof(values));
	if(rc < 0) {
		return rc;
	}

	strarg_t type;
	if(0 == strcmp("form-data; name=\"markdown\"", content_disposition)) {
		type = "text/markdown; charset=utf-8";
	} else {
		type = content_type;
	}



	SLNSubmissionRef sub = SLNSubmissionCreate(session, type);
	SLNSubmissionRef meta = SLNSubmissionCreate(session, "text/efs-meta+json; charset=utf-8");
	if(!sub || !meta) {
		SLNSubmissionFree(&sub);
		SLNSubmissionFree(&meta);
		return -1;
	}


	str_t *fulltext = NULL;
	size_t fulltextlen = 0;
	if(
		0 == strcasecmp(type, "text/markdown; charset=utf-8") ||
		0 == strcasecmp(type, "text/markdown") ||
		0 == strcasecmp(type, "text/x-markdown; charset=utf-8") ||
		0 == strcasecmp(type, "text/x-markdown") ||
		0 == strcasecmp(type, "text/plain; charset=utf-8") ||
		0 == strcasecmp(type, "text/plain")
	) {
		fulltext = malloc(FTS_MAX + 1);
		// TODO
	}
	for(;;) {
		uv_buf_t buf[1];
		rc = MultipartFormReadData(form, buf);
		if(rc < 0) {
			FREE(&fulltext);
			SLNSubmissionFree(&sub);
			SLNSubmissionFree(&meta);
			return rc;
		}
		if(0 == buf->len) break;
		rc = SLNSubmissionWrite(sub, (byte_t const *)buf->base, buf->len);
		if(rc < 0) {
			FREE(&fulltext);
			SLNSubmissionFree(&sub);
			SLNSubmissionFree(&meta);
			return rc;
		}
		if(fulltext) {
			size_t const use = MIN(FTS_MAX-fulltextlen, buf->len);
			memcpy(fulltext+fulltextlen, buf->base, use);
			fulltextlen += use;
		}
	}
	if(fulltext) {
		fulltext[fulltextlen] = '\0';
	}


	if(SLNSubmissionEnd(sub) < 0) {
		FREE(&fulltext);
		SLNSubmissionFree(&sub);
		SLNSubmissionFree(&meta);
		return -1;
	}

	strarg_t const targetURI = SLNSubmissionGetPrimaryURI(sub);
	SLNSubmissionWrite(meta, (byte_t const *)targetURI, strlen(targetURI));
	SLNSubmissionWrite(meta, (byte_t const *)"\r\n\r\n", 4);

	yajl_gen json = yajl_gen_alloc(NULL);
	yajl_gen_config(json, yajl_gen_print_callback, (void (*)())SLNSubmissionWrite, meta);
	yajl_gen_config(json, yajl_gen_beautify, (int)true);

	yajl_gen_map_open(json);

	if(title) {
		yajl_gen_string(json, (byte_t const *)"title", strlen("title"));
		yajl_gen_array_open(json);
		yajl_gen_string(json, (byte_t const *)title, strlen(title));
		if(fulltextlen) {
			// TODO: Try to determine title from content
		}
		yajl_gen_array_close(json);
	}

	if(fulltextlen) {
		yajl_gen_string(json, (byte_t const *)"fulltext", strlen("fulltext"));
		yajl_gen_string(json, (byte_t const *)fulltext, fulltextlen);


		yajl_gen_string(json, (byte_t const *)"link", strlen("link"));
		yajl_gen_array_open(json);

		regex_t linkify[1];
		// <http://daringfireball.net/2010/07/improved_regex_for_matching_urls>
		// Painstakingly ported to POSIX
		int rc = regcomp(linkify, "([a-z][a-z0-9_-]+:(/{1,3}|[a-z0-9%])|www[0-9]{0,3}[.]|[a-z0-9.-]+[.][a-z]{2,4}/)([^[:space:]()<>]+|\\(([^[:space:]()<>]+|(\\([^[:space:]()<>]+\\)))*\\))+(\\(([^[:space:]()<>]+|(\\([^[:space:]()<>]+\\)))*\\)|[^][[:space:]`!(){};:'\".,<>?«»“”‘’])", REG_ICASE | REG_EXTENDED);
		assert(0 == rc);

		strarg_t pos = fulltext;
		regmatch_t match;
		while(0 == regexec(linkify, pos, 1, &match, 0)) {
			regoff_t const loc = match.rm_so;
			regoff_t const len = match.rm_eo - match.rm_so;
			yajl_gen_string(json, (byte_t const *)pos+loc, len);
			pos += loc+len;
		}

		regfree(linkify);

		yajl_gen_array_close(json);
	}

	yajl_gen_map_close(json);
	yajl_gen_free(json); json = NULL;
	FREE(&fulltext);

	if(SLNSubmissionEnd(meta) < 0) {
		SLNSubmissionFree(&sub);
		SLNSubmissionFree(&meta);
		return -1;
	}

	*outSub = sub;
	*outMeta = meta;
	return 0;
}














static int POST_submit(BlogRef const blog, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_POST != method) return -1;
	if(!URIPath(URI, "/submission", NULL)) return -1;

	if(!SLNSessionHasPermission(session, SLN_WRONLY)) return 403;

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


	strarg_t title = NULL; // TODO: Get file name from form part.

	SLNSubmissionRef sub = NULL;
	SLNSubmissionRef meta = NULL;


	rc = quickpair(session, form, title, &sub, &meta);
	if(rc < 0) {
		MultipartFormFree(&form);
		return 500;
	}

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

static int GET_login(BlogRef const blog, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method) return -1;
	if(!URIPath(URI, "/login", NULL)) return -1;

	str_t *reponame_HTMLSafe = htmlenc(SLNRepoGetName(SLNSessionGetRepo(session)));
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


	SLNSessionCacheRef const cache = SLNRepoGetSessionCache(SLNSessionGetRepo(session));
	SLNSessionRef s;
	int rc = SLNSessionCacheCreateSession(cache, "ben", "testing", &s); // TODO
	if(DB_SUCCESS != rc) return 403;

	str_t *cookie = SLNSessionCopyCookie(s);
	SLNSessionRelease(&s);
	if(!cookie) return 500;

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
	rc = rc >= 0 ? rc : GET_new(blog, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_up(blog, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : POST_submit(blog, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_login(blog, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : POST_auth(blog, session, conn, method, URI, headers);

	if(403 == rc) {
		HTTPConnectionWriteResponse(conn, 303, "See Other");
		HTTPConnectionWriteContentLength(conn, 0);
		HTTPConnectionWriteHeader(conn, "Location", "/login");
		HTTPConnectionBeginBody(conn);
		HTTPConnectionEnd(conn);
		return 0;
	}
	if(rc >= 0) return rc; // TODO: Pretty 404 pages, etc.

	if(HTTP_GET != method && HTTP_HEAD != method) return -1;

	if(strchr(URI, '?')) return 500; // TODO: Ignore query parameters
	if(strstr(URI, "..")) return 400;
	str_t path[PATH_MAX];
	rc = snprintf(path, PATH_MAX, "%s/static/%s", blog->dir, URI);
	if(rc >= PATH_MAX) return 414; // Request-URI Too Large
	if(rc < 0) return 500;

	HTTPConnectionSendFile(conn, path, NULL, -1); // TODO: Determine file type.
	return 0;
}

