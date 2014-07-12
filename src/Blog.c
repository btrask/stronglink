#define _GNU_SOURCE
#include <yajl/yajl_tree.h>
#include <yajl/yajl_gen.h>
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
static HeaderField const BlogHTTPFields[] = {
	{"cookie", 100},
	{"content-type", 100},
};
typedef struct {
	strarg_t content_type;
	strarg_t content_disposition;
} BlogSubmissionHeaders;
static HeaderField const BlogSubmissionFields[] = {
	{"content-type", 100},
	{"content-disposition", 100},
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

	EFSFileInfo *info = EFSSessionCopyFileInfo(session, URI);
	if(!info) return -1;

	if(
		0 != strcasecmp("text/markdown; charset=utf-8", info->type) &&
		0 != strcasecmp("text/markdown", info->type)
	) {
		EFSFileInfoFree(&info);
		return -1; // TODO: Other types, plugins, w/e.
	}

	if(async_fs_mkdirp_dirname(previewPath, 0700) < 0) {
		EFSFileInfoFree(&info);
		return -1;
	}

	str_t *tmpPath = EFSRepoCopyTempPath(EFSSessionGetRepo(session));
	if(async_fs_mkdirp_dirname(tmpPath, 0700) < 0) {
		FREE(&tmpPath);
		EFSFileInfoFree(&info);
		return -1;
	}

	cothread_t const thread = co_active();
	async_state state = { .thread = thread };
	uv_process_t proc = { .data = &state };
	str_t *args[] = {
		"markdown",
		"-f", "autolink",
		"-G",
		"-o", tmpPath,
		info->path,
		NULL
	};
	uv_process_options_t const opts = {
		.file = args[0],
		.args = args,
		.exit_cb = async_exit_cb,
	};
	uv_spawn(loop, &proc, &opts);
	co_switch(yield);

	if(state.status < 0) {
		FREE(&tmpPath);
		EFSFileInfoFree(&info);
		return -1;
	}

	err_t const err = async_fs_link(tmpPath, previewPath);

	async_fs_unlink(tmpPath);
	FREE(&tmpPath);
	EFSFileInfoFree(&info);

	if(err < 0) return -1;
	return 0;
}
static err_t genPreview(BlogRef const blog, EFSSessionRef const session, strarg_t const URI, strarg_t const previewPath) {
	if(genMarkdownPreview(blog, session, URI, previewPath) >= 0) return 0;

	if(async_fs_mkdirp_dirname(previewPath, 0700) < 0) return -1;

	EFSFilterRef const backlinks = EFSFilterCreate(EFSBacklinkFilesFilter);
	EFSFilterAddStringArg(backlinks, URI, -1);
	EFSFilterRef const metafiles = EFSFilterCreate(EFSFileTypeFilter);
	EFSFilterAddStringArg(metafiles, "text/efs-meta+json; charset=utf-8", -1);
	EFSFilterRef filter = EFSFilterCreate(EFSIntersectionFilter);
	EFSFilterAddFilterArg(filter, backlinks);
	EFSFilterAddFilterArg(filter, metafiles);

	URIListRef metaURIs = EFSSessionCreateFilteredURIList(session, filter, 10);
	count_t const metaURIsCount = URIListGetCount(metaURIs);

	str_t *URI_HTMLSafe = htmlenc(URI);
	str_t *URIEncoded_HTMLSafe = htmlenc(URI); // TODO
	str_t *title_HTMLSafe = NULL;
	str_t *sourceURI_HTMLSafe = NULL;
	str_t *description_HTMLSafe = NULL;
	str_t *thumbnailURI_HTMLSafe = NULL;
	str_t *faviconURI_HTMLSafe = NULL;

	for(index_t i = 0; i < metaURIsCount; ++i) {
		// TODO: Streaming.

		strarg_t const metaURI = URIListGetURI(metaURIs, 0);
		EFSFileInfo *metaInfo = EFSSessionCopyFileInfo(session, metaURI);
		if(!metaInfo) continue;
		uv_file file = async_fs_open(metaInfo->path, O_RDONLY, 0000);
		EFSFileInfoFree(&metaInfo);
		if(file < 0) continue;

		str_t *str = malloc(BUFFER_SIZE + 1);
		uv_buf_t bufInfo = uv_buf_init(str, BUFFER_SIZE);
		ssize_t const len = async_fs_read(file, &bufInfo, 1, 0);

		async_fs_close(file); file = -1;

		if(len < 0) {
			FREE(&str);
			continue;
		}

		str[len] = '\0';
		str_t err[200];
		yajl_val obj = yajl_tree_parse(str, err, sizeof(err));
		if(!emptystr(err)) fprintf(stderr, "parse error %s:\n%s\n", metaURI, err);

		FREE(&str);

		strarg_t yajl_title[] = { "title", NULL };
		if(!title_HTMLSafe) title_HTMLSafe = htmlenc(YAJL_GET_STRING(yajl_tree_get(obj, yajl_title, yajl_t_string)));
		strarg_t yajl_sourceURI[] = { "sourceURI", NULL };
		if(!sourceURI_HTMLSafe) sourceURI_HTMLSafe = htmlenc(YAJL_GET_STRING(yajl_tree_get(obj, yajl_sourceURI, yajl_t_string)));
		strarg_t yajl_description[] = { "description", NULL };
		if(!description_HTMLSafe) description_HTMLSafe = htmlenc(YAJL_GET_STRING(yajl_tree_get(obj, yajl_description, yajl_t_string)));

		yajl_tree_free(obj); obj = NULL;

//		break;
	}

	str_t *tmpPath = EFSRepoCopyTempPath(blog->repo);
	async_fs_mkdirp_dirname(tmpPath, 0700);

	uv_file file = async_fs_open(tmpPath, O_CREAT | O_EXCL | O_WRONLY, 0400);
	err_t err = file;

	if(emptystr(title_HTMLSafe)) FREE(&title_HTMLSafe);
	if(emptystr(description_HTMLSafe)) FREE(&description_HTMLSafe);
	if(emptystr(sourceURI_HTMLSafe)) FREE(&sourceURI_HTMLSafe);
	if(emptystr(thumbnailURI_HTMLSafe)) FREE(&thumbnailURI_HTMLSafe);
	if(emptystr(faviconURI_HTMLSafe)) FREE(&faviconURI_HTMLSafe);

	TemplateArg const args[] = {
		{"URI", URI_HTMLSafe, -1},
		{"URIEncoded", URIEncoded_HTMLSafe, -1},
		{"title", title_HTMLSafe ?: "(no title)", -1},
		{"description", description_HTMLSafe ?: "(no description)", -1},
		{"sourceURI", sourceURI_HTMLSafe, -1},
		{"thumbnailURI", thumbnailURI_HTMLSafe ?: "/file.png", -1},
		{"faviconURI", faviconURI_HTMLSafe, -1},
	};
	err = err < 0 ? err : TemplateWriteFile(blog->preview, args, numberof(args), file);

	async_fs_close(file); file = -1;

	err = err < 0 ? err : async_fs_link(tmpPath, previewPath);

	async_fs_unlink(tmpPath);

	FREE(&tmpPath);

	FREE(&URI_HTMLSafe);
	FREE(&URIEncoded_HTMLSafe);
	FREE(&title_HTMLSafe);
	FREE(&description_HTMLSafe);
	FREE(&sourceURI_HTMLSafe);
	FREE(&thumbnailURI_HTMLSafe);
	FREE(&faviconURI_HTMLSafe);

	URIListFree(&metaURIs);
	EFSFilterFree(&filter);

	if(err < 0) return -1;
	return 0;
}
static void sendPreview(BlogRef const blog, HTTPMessageRef const msg, EFSSessionRef const session, strarg_t const URI, strarg_t const previewPath) {
	str_t *URI_HTMLSafe = htmlenc(URI);
	str_t *URIEncoded_HTMLSafe = htmlenc(URI); // TODO: URI enc
	TemplateArg const args[] = {
		{"URI", URI_HTMLSafe, -1},
		{"URIEncoded", URIEncoded_HTMLSafe, -1},
	};

	TemplateWriteHTTPChunk(blog->entry_start, args, numberof(args), msg);

	if(
		HTTPMessageWriteChunkFile(msg, previewPath) < 0 &&
		(genPreview(blog, session, URI, previewPath) < 0 ||
		HTTPMessageWriteChunkFile(msg, previewPath) < 0)
	) {
		TemplateWriteHTTPChunk(blog->empty, args, numberof(args), msg);
	}

	TemplateWriteHTTPChunk(blog->entry_end, args, numberof(args), msg);

	FREE(&URI_HTMLSafe);
	FREE(&URIEncoded_HTMLSafe);
}

static bool_t getResultsPage(BlogRef const blog, HTTPMessageRef const msg, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_GET != method) return false;
	if(!URIPath(URI, "/", NULL)) return false;

	BlogHTTPHeaders const *const headers = HTTPMessageGetHeaders(msg, BlogHTTPFields, numberof(BlogHTTPFields));
	EFSSessionRef const session = EFSRepoCreateSession(blog->repo, headers->cookie);
	if(!session) {
		HTTPMessageSendStatus(msg, 403);
		return true;
	}

	// TODO: Parse querystring `q` parameter
	EFSFilterRef filter = EFSFilterCreate(EFSBacklinkFilesFilter);
	EFSFilterAddStringArg(filter, "efs://user", -1);

	URIListRef URIs = EFSSessionCreateFilteredURIList(session, filter, RESULTS_MAX); // TODO: We should be able to specify a specific algorithm here.

	HTTPMessageWriteResponse(msg, 200, "OK");
	HTTPMessageWriteHeader(msg, "Content-Type", "text/html; charset=utf-8");
	HTTPMessageWriteHeader(msg, "Transfer-Encoding", "chunked");
	HTTPMessageBeginBody(msg);

	// TODO: Template args like repo name.
	TemplateWriteHTTPChunk(blog->header, NULL, 0, msg);

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

	TemplateWriteHTTPChunk(blog->footer, NULL, 0, msg);

	HTTPMessageWriteChunkLength(msg, 0);
	HTTPMessageWrite(msg, (byte_t const *)"\r\n", 2);
	HTTPMessageEnd(msg);

	URIListFree(&URIs);
	EFSFilterFree(&filter);
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
	TemplateWriteHTTPChunk(blog->compose, NULL, 0, msg);
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

	MultipartFormRef form = MultipartFormCreate(msg, headers->content_type, BlogSubmissionFields, numberof(BlogSubmissionFields));
	FormPartRef const part = MultipartFormGetPart(form);
	BlogSubmissionHeaders const *const fheaders = FormPartGetHeaders(part);

	strarg_t type;
	if(0 == strcmp("form-data; name=\"markdown\"", fheaders->content_disposition)) {
		type = "text/markdown; charset=utf-8";
	} else {
		type = fheaders->content_type;
	}
	EFSSubmissionRef sub = EFSSubmissionCreate(session, type);
	if(
		!sub ||
		EFSSubmissionWriteFrom(sub, (ssize_t (*)())FormPartGetBuffer, part) < 0 ||
		EFSSubmissionStore(sub) < 0
	) {
		fprintf(stderr, "Blog submission error\n");
		HTTPMessageSendStatus(msg, 500);
		EFSSubmissionFree(&sub);
		MultipartFormFree(&form);
		EFSSessionFree(&session);
		return true;
	}


	EFSSubmissionRef meta = EFSSubmissionCreate(session, "text/efs-meta+json; charset=utf-8");

	yajl_gen const json = yajl_gen_alloc(NULL);
	yajl_gen_config(json, yajl_gen_print_callback, (void (*)())EFSSubmissionWrite, meta);
	yajl_gen_config(json, yajl_gen_beautify, (int)true);

	yajl_gen_map_open(json);

	strarg_t const metaURI = EFSSubmissionGetPrimaryURI(sub);
	yajl_gen_string(json, (byte_t const *)"metaURI", strlen("metaURI"));
	yajl_gen_string(json, (byte_t const *)metaURI, strlen(metaURI));

	yajl_gen_string(json, (byte_t const *)"links", strlen("links"));
	yajl_gen_array_open(json);
	yajl_gen_string(json, (byte_t const *)"efs://user", strlen("efs://user"));
	yajl_gen_array_close(json);
	// TODO: Full text indexing, determine links, etc.

	yajl_gen_map_close(json);

	if(
		!meta ||
		EFSSubmissionEnd(meta) < 0 ||
		EFSSubmissionStore(meta) < 0
	) {
		HTTPMessageSendStatus(msg, 500);
		EFSSubmissionFree(&meta);
		EFSSessionFree(&session);
		return true;
	}

	HTTPMessageWriteResponse(msg, 303, "See Other");
	HTTPMessageWriteHeader(msg, "Location", "/");
	HTTPMessageBeginBody(msg);
	HTTPMessageEnd(msg);

	EFSSubmissionFree(&meta);
	EFSSessionFree(&session);
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

