#define _GNU_SOURCE
#include <yajl/yajl_tree.h>
#include <limits.h>
#include "common.h"
#include "async.h"
#include "EarthFS.h"
#include "Template.h"
#include "http/HTTPServer.h"
#include "http/MultipartForm.h"
#include "http/QueryString.h"

#define RESULTS_MAX 50
#define BUFFER_SIZE (1024 * 8)

typedef struct {
	strarg_t cookie;
	strarg_t content_type;
} BlogHTTPHeaders;
static strarg_t const BlogHTTPFields[] = {
	"cookie",
	"content-type",
};
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
};

// TODO: Real public API.
bool_t URIPath(strarg_t const URI, strarg_t const path, strarg_t *const qs);

static bool_t emptystr(strarg_t const str) {
	return !str || '\0' == str[0];
}
static str_t *BlogCopyPreviewPath(BlogRef const blog, strarg_t const hash) {
	str_t *path;
	if(asprintf(&path, "%s/%.2s/%s", blog->cacheDir, hash, hash) < 0) return NULL;
	return path;
}

static err_t genMarkdownPreview(BlogRef const blog, EFSSessionRef const session, strarg_t const URI, strarg_t const previewPath) {

	EFSFileInfo info;
	if(EFSSessionGetFileInfo(session, URI, &info) < 0) return -1;

	if(
		0 != strcasecmp("text/markdown; charset=utf-8", info.type) &&
		0 != strcasecmp("text/markdown", info.type)
	) {
		EFSFileInfoCleanup(&info);
		return -1; // TODO: Other types, plugins, w/e.
	}

	if(async_fs_mkdirp_dirname(previewPath, 0700) < 0) {
		EFSFileInfoCleanup(&info);
		return -1;
	}

	str_t *tmpPath = EFSRepoCopyTempPath(EFSSessionGetRepo(session));
	if(async_fs_mkdirp_dirname(tmpPath, 0700) < 0) {
		FREE(&tmpPath);
		EFSFileInfoCleanup(&info);
		return -1;
	}

	cothread_t const thread = co_active();
	async_state state = { .thread = thread };
	uv_process_t proc;
	proc.data = &state;
	str_t *args[] = {
		"markdown",
		"-f", "autolink",
		"-G",
		"-o", tmpPath,
		info.path,
		NULL
	};
	uv_process_options_t const opts = {
		.file = args[0],
		.args = args,
		.exit_cb = async_exit_cb,
	};
	uv_spawn(loop, &proc, &opts);
	async_yield();

	if(state.status < 0) {
		FREE(&tmpPath);
		EFSFileInfoCleanup(&info);
		return -1;
	}

	err_t const err = async_fs_link(tmpPath, previewPath);

	async_fs_unlink(tmpPath);
	FREE(&tmpPath);
	EFSFileInfoCleanup(&info);

	if(err < 0) return -1;
	return 0;
}
typedef struct {
	BlogRef blog;
	strarg_t fileURI;
} md_state;
static str_t *md_lookup(md_state const *const state, strarg_t const var) {
	strarg_t unsafe = NULL;
	if(0 == strcmp(var, "rawURI")) unsafe = state->fileURI; // TODO
	if(unsafe) return htmlenc(unsafe);
	sqlite3f *db = EFSRepoDBConnect(state->blog->repo);
	sqlite3_stmt *select = QUERY(db,
		"SELECT value FROM meta_data\n"
		"WHERE uri = ? AND field = ?\n"
		"AND value != '' LIMIT 1");
	sqlite3_bind_text(select, 1, state->fileURI, -1, SQLITE_STATIC);
	sqlite3_bind_text(select, 2, var, -1, SQLITE_STATIC);
	if(SQLITE_ROW == STEP(select)) {
		unsafe = (strarg_t)sqlite3_column_text(select, 0);
	}
	if(!unsafe) {
		if(0 == strcmp(var, "thumbnailURI")) unsafe = "/file.png";
		if(0 == strcmp(var, "title")) unsafe = "(no title)";
		if(0 == strcmp(var, "description")) unsafe = "(no description)";
	}
	str_t *result = htmlenc(unsafe);
	sqlite3f_finalize(select); select = NULL;
	EFSRepoDBClose(state->blog->repo, &db);
	return result;
}
static void md_free(md_state const *const state, strarg_t const var, str_t **const val) {
	FREE(val);
}
static TemplateArgCBs const md_cbs = {
	.lookup = (str_t *(*)())md_lookup,
	.free = (void (*)())md_free,
};
static err_t genPreview(BlogRef const blog, EFSSessionRef const session, strarg_t const URI, strarg_t const previewPath) {
	if(genMarkdownPreview(blog, session, URI, previewPath) >= 0) return 0;

	if(async_fs_mkdirp_dirname(previewPath, 0700) < 0) return -1;
	str_t *tmpPath = EFSRepoCopyTempPath(blog->repo);
	async_fs_mkdirp_dirname(tmpPath, 0700);
	uv_file file = async_fs_open(tmpPath, O_CREAT | O_EXCL | O_WRONLY, 0400);
	err_t err = file;

	md_state const state = {
		.blog = blog,
		.fileURI = URI,
	};
	err = err < 0 ? err : TemplateWriteFile(blog->preview, &md_cbs, &state, file); // TODO: Dynamic lookup function.

	async_fs_close(file); file = -1;

	err = err < 0 ? err : async_fs_link(tmpPath, previewPath);

	async_fs_unlink(tmpPath);

	FREE(&tmpPath);

	if(err < 0) return -1;
	return 0;
}
static void sendPreview(BlogRef const blog, HTTPMessageRef const msg, EFSSessionRef const session, strarg_t const URI, strarg_t const previewPath) {
	str_t *URI_HTMLSafe = htmlenc(URI);
	str_t *URIEncoded_HTMLSafe = htmlenc(URI); // TODO: URI enc
	TemplateStaticArg const args[] = {
		{"URI", URI_HTMLSafe},
		{"URIEncoded", URIEncoded_HTMLSafe},
		{NULL, NULL},
	};

	TemplateWriteHTTPChunk(blog->entry_start, &TemplateStaticCBs, args, msg);

	if(
		HTTPMessageWriteChunkFile(msg, previewPath) < 0 &&
		(genPreview(blog, session, URI, previewPath) < 0 ||
		HTTPMessageWriteChunkFile(msg, previewPath) < 0)
	) {
		TemplateWriteHTTPChunk(blog->empty, &TemplateStaticCBs, args, msg);
	}

	TemplateWriteHTTPChunk(blog->entry_end, &TemplateStaticCBs, args, msg);

	FREE(&URI_HTMLSafe);
	FREE(&URIEncoded_HTMLSafe);
}

typedef struct {
	str_t *query;
	str_t *file; // It'd be nice if we didn't need a separate parameter for this.
} BlogQueryValues;
static strarg_t const BlogQueryFields[] = {
	"q",
	"f",
};
static bool_t getResultsPage(BlogRef const blog, HTTPMessageRef const msg, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_GET != method) return false;
	strarg_t qs = NULL;
	if(!URIPath(URI, "/", &qs)) return false;

	BlogHTTPHeaders const *const headers = HTTPMessageGetHeaders(msg, BlogHTTPFields, numberof(BlogHTTPFields));
	EFSSessionRef session = EFSRepoCreateSession(blog->repo, headers->cookie);
	if(!session) {
		HTTPMessageSendStatus(msg, 403);
		return true;
	}

	EFSFilterRef filter = EFSFilterCreate(EFSIntersectionFilterType);
	EFSFilterRef const visibility = EFSFilterCreate(EFSLinksToFilterType);
	EFSFilterAddStringArg(visibility, "efs://user", -1);
	EFSFilterAddFilterArg(filter, visibility);

	BlogQueryValues *params = QSValuesCopy(qs, BlogQueryFields, numberof(BlogQueryFields));
	EFSFilterRef const query = EFSUserFilterParse(params->query);
	if(query) EFSFilterAddFilterArg(filter, query);
	QSValuesFree((QSValues *)&params, numberof(BlogQueryFields));

	EFSFilterPrint(query, 0); // DEBUG

	URIListRef URIs = EFSSessionCreateFilteredURIList(session, filter, RESULTS_MAX); // TODO: We should be able to specify a specific algorithm here.

	HTTPMessageWriteResponse(msg, 200, "OK");
	HTTPMessageWriteHeader(msg, "Content-Type", "text/html; charset=utf-8");
	HTTPMessageWriteHeader(msg, "Transfer-Encoding", "chunked");
	HTTPMessageBeginBody(msg);

	// TODO: Template args like repo name.
	TemplateWriteHTTPChunk(blog->header, NULL, NULL, msg);

	for(index_t i = 0; i < URIListGetCount(URIs); ++i) {
		strarg_t const URI = URIListGetURI(URIs, i);
		str_t algo[EFS_ALGO_SIZE]; // EFS_INTERNAL_ALGO
		str_t hash[EFS_HASH_SIZE];
		EFSParseURI(URI, algo, hash);
		str_t *previewPath = BlogCopyPreviewPath(blog, hash);
		if(!previewPath) continue;
		sendPreview(blog, msg, session, URI, previewPath);
		FREE(&previewPath);
	}

	TemplateWriteHTTPChunk(blog->footer, NULL, NULL, msg);

	HTTPMessageWriteChunkLength(msg, 0);
	HTTPMessageWrite(msg, (byte_t const *)"\r\n", 2);
	HTTPMessageEnd(msg);

	URIListFree(&URIs);
	EFSFilterFree(&filter);
	EFSSessionFree(&session);
	return true;
}
static bool_t getCompose(BlogRef const blog, HTTPMessageRef const msg, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_GET != method) return false;
	if(!URIPath(URI, "/compose", NULL)) return false;

	BlogHTTPHeaders const *const headers = HTTPMessageGetHeaders(msg, BlogHTTPFields, numberof(BlogHTTPFields));
	EFSSessionRef session = EFSRepoCreateSession(blog->repo, headers->cookie);
	if(!session) {
		HTTPMessageSendStatus(msg, 403);
		return true;
	}

	HTTPMessageWriteResponse(msg, 200, "OK");
	HTTPMessageWriteHeader(msg, "Content-Type", "text/html; charset=utf-8");
	HTTPMessageWriteHeader(msg, "Transfer-Encoding", "chunked");
	HTTPMessageBeginBody(msg);
	TemplateWriteHTTPChunk(blog->compose, NULL, NULL, msg);
	HTTPMessageWriteChunkLength(msg, 0);
	HTTPMessageWrite(msg, (byte_t const *)"\r\n", 2);
	HTTPMessageEnd(msg);

	EFSSessionFree(&session);
	return true;
}
static bool_t postSubmission(BlogRef const blog, HTTPMessageRef const msg, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_POST != method) return false;
	if(!URIPath(URI, "/submission", NULL)) return false;

	BlogHTTPHeaders const *const headers = HTTPMessageGetHeaders(msg, BlogHTTPFields, numberof(BlogHTTPFields));
	EFSSessionRef session = EFSRepoCreateSession(blog->repo, headers->cookie);
	if(!session) {
		HTTPMessageSendStatus(msg, 403);
		return true;
	}

	// TODO: CSRF token
	MultipartFormRef form = MultipartFormCreate(msg, headers->content_type, BlogSubmissionFields, numberof(BlogSubmissionFields));
	FormPartRef const part = MultipartFormGetPart(form);
	BlogSubmissionHeaders const *const fheaders = FormPartGetHeaders(part);

	strarg_t type;
	if(0 == strcmp("form-data; name=\"markdown\"", fheaders->content_disposition)) {
		type = "text/markdown; charset=utf-8";
	} else {
		type = fheaders->content_type;
	}

	strarg_t title = NULL; // TODO: Get file name from form part.

	EFSSubmissionRef sub = NULL;
	EFSSubmissionRef meta = NULL;
	if(EFSSubmissionCreatePair(session, type, (ssize_t (*)())FormPartGetBuffer, part, title, &sub, &meta) < 0) {
		HTTPMessageSendStatus(msg, 500);
		MultipartFormFree(&form);
		EFSSessionFree(&session);
		return true;
	}

	sqlite3f *db = EFSRepoDBConnect(blog->repo);
	EXEC(QUERY(db, "SAVEPOINT addpair"));

	bool_t const success =
		EFSSubmissionStore(sub, db) >= 0 &&
		EFSSubmissionStore(meta, db) >= 0;

	if(!success) EXEC(QUERY(db, "ROLLBACK TO addpair"));
	EXEC(QUERY(db, "RELEASE addpair"));
	EFSRepoDBClose(blog->repo, &db);
	EFSSubmissionFree(&sub);
	EFSSubmissionFree(&meta);
	MultipartFormFree(&form);
	EFSSessionFree(&session);

	if(success) {
		HTTPMessageWriteResponse(msg, 303, "See Other");
		HTTPMessageWriteHeader(msg, "Location", "/");
		HTTPMessageBeginBody(msg);
		HTTPMessageEnd(msg);
	} else {
		HTTPMessageSendStatus(msg, 500);
	}

	return true;
}


BlogRef BlogCreate(EFSRepoRef const repo) {
	assertf(repo, "Blog requires valid repo");

	BlogRef const blog = calloc(1, sizeof(struct Blog));
	blog->repo = repo;
	asprintf(&blog->dir, "%s/blog", EFSRepoGetDir(repo));
	asprintf(&blog->staticDir, "%s/static", blog->dir);
	asprintf(&blog->templateDir, "%s/template", blog->dir);
	asprintf(&blog->cacheDir, "%s/blog", EFSRepoGetCacheDir(repo));

	str_t *path = malloc(PATH_MAX);
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
	FREE(&path);

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
	FREE(blogptr); blog = NULL;
}
bool_t BlogDispatch(BlogRef const blog, HTTPMessageRef const msg, HTTPMethod const method, strarg_t const URI) {
	if(getResultsPage(blog, msg, method, URI)) return true;
	if(getCompose(blog, msg, method, URI)) return true;
	if(postSubmission(blog, msg, method, URI)) return true;

	if(HTTP_GET != method && HTTP_HEAD != method) return false;

	// TODO: Ignore query parameters, check for `..` (security critical).
	str_t *path;
	int const plen = asprintf(&path, "%s%s", blog->staticDir, URI);
	if(plen < 0) {
		HTTPMessageSendStatus(msg, 500);
		return true;
	}

	HTTPMessageSendFile(msg, path, NULL, -1); // TODO: Determine file type.
	FREE(&path);
	return true;
}

