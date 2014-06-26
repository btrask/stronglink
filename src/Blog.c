#define _GNU_SOURCE
#include <yajl/yajl_tree.h>
#include "common.h"
#include "async.h"
#include "fs.h"
#include "EarthFS.h"
#include "HTTPServer.h"
#include "QueryString.h"

#define RESULTS_MAX 50

// TODO: Real public API.
EFSSessionRef auth(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const qs);

// TODO: Use a real library or put this somewhere.
static str_t *htmlenc(strarg_t const str) {
	if(!str) return NULL;
	size_t total = 0;
	for(off_t i = 0; str[i]; ++i) switch(str[i]) {
		case '<': total += 4; break;
		case '>': total += 4; break;
		case '&': total += 5; break;
		case '"': total += 6; break;
		default: total += 1; break;
	}
	str_t *enc = malloc(total+1);
	if(!enc) return NULL;
	for(off_t i = 0, j = 0; str[i]; ++i) switch(str[i]) {
		case '<': memcpy(enc+j, "&lt;", 4); j += 4; break;
		case '>': memcpy(enc+j, "&gt;", 4); j += 4; break;
		case '&': memcpy(enc+j, "&amp;", 5); j += 5; break;
		case '"': memcpy(enc+j, "&quot;", 6); j += 6; break;
		default: enc[j++] = str[i]; break;
	}
	enc[total] = '\0';
	return enc;
}

static str_t *BlogCopyPreviewPath(EFSRepoRef const repo, strarg_t const hash) {
	str_t *path;
	if(asprintf(&path, "%s/blog/%.2s/%s", EFSRepoGetCacheDir(repo), hash, hash) < 0) return NULL;
	return path;
}

static err_t genMarkdownPreview(HTTPConnectionRef const conn, EFSSessionRef const session, strarg_t const URI, EFSFileInfo const *const info, strarg_t const previewPath) {

	if(0 != strcasecmp("text/markdown; charset=utf-8", info->type)) return -1; // TODO: Other types, plugins, w/e.

	str_t *tmpPath = EFSRepoCopyTempPath(EFSSessionGetRepo(session));
	if(!tmpPath) {
		fprintf(stderr, "No temp path\n");
		return -1;
	}

	ssize_t const dirlen = dirname(tmpPath, strlen(tmpPath));
	if(dirlen < 0 || mkdirp(tmpPath, dirlen, 0700) < 0) {
		fprintf(stderr, "Couldn't make temp dir %s\n", tmpPath);
		FREE(&tmpPath);
		return -1;
	}

	cothread_t const thread = co_active();
	async_state state = { .thread = thread };
	uv_process_t proc = { .data = &state };
	str_t *args[] = {
		"markdown",
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

	str_t *previewDir = strdup(previewPath);
	ssize_t const pdirlen = dirname(previewDir, strlen(previewDir));
	if(pdirlen < 0 || mkdirp(previewDir, pdirlen, 0700) < 0) {
		fprintf(stderr, "Couldn't create preview dir %s\n", previewDir);
		FREE(&previewDir);
		FREE(&tmpPath);
		return -1;
	}
	FREE(&previewDir);

	uv_fs_t req = { .data = thread };
	uv_fs_link(loop, &req, tmpPath, previewPath, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);

	uv_fs_unlink(loop, &req, tmpPath, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);

	FREE(&tmpPath);
	return 0;
}
static err_t genPreview(HTTPConnectionRef const conn, EFSSessionRef const session, strarg_t const URI, EFSFileInfo const *const info, strarg_t const previewPath) {
	if(genMarkdownPreview(conn, session, URI, info, previewPath) >= 0) return 0;

	EFSFilterRef const backlinks = EFSFilterCreate(EFSBacklinkFilesFilter);
	EFSFilterAddStringArg(backlinks, URI, -1);
	EFSFilterRef const metafiles = EFSFilterCreate(EFSFileTypeFilter);
	EFSFilterAddStringArg(metafiles, "text/efs-meta+json; charset=utf-8", -1);
	EFSFilterRef const filter = EFSFilterCreate(EFSIntersectionFilter);
	EFSFilterAddFilterArg(filter, backlinks);
	EFSFilterAddFilterArg(filter, metafiles);

	URIListRef const metaURIs = EFSSessionCreateFilteredURIList(session, filter, 10);
	count_t const metaURIsCount = URIListGetCount(metaURIs);

	str_t *URI_HTMLSafe = htmlenc(URI);
	str_t *title_HTMLSafe = NULL;
	str_t *sourceURI_HTMLSafe = NULL;
	str_t *description_HTMLSafe = NULL;
	str_t *thumbnailURI_HTMLSafe = NULL;
	str_t *faviconURI_HTMLSafe = NULL;

	for(index_t i = 0; i < metaURIsCount; ++i) {
		strarg_t const metaURI = URIListGetURI(metaURIs, 0);
		EFSFileInfo metaInfo;
		if(EFSSessionGetFileInfoForURI(session, &metaInfo, metaURI) < 0) continue;

		// TODO: Streaming, error checking.
		uv_fs_t req = { .data = co_active() };
		uv_fs_open(loop, &req, metaInfo.path, O_RDONLY, 0000, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);
		uv_file const file = req.result;

		FREE(&metaInfo.path);
		FREE(&metaInfo.type);

		byte_t *buf = malloc(1024 * 8 + 1);
		uv_buf_t bufInfo = uv_buf_init(buf, 1024 * 8);
		uv_fs_read(loop, &req, file, &bufInfo, 1, 0, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);
		size_t const len = req.result;

		uv_fs_close(loop, &req, file, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);

		buf[len] = '\0';
		yajl_val const obj = yajl_tree_parse(buf, NULL, 0);

		FREE(&buf);

		strarg_t yajl_title[] = { "title", NULL };
		if(!title_HTMLSafe) title_HTMLSafe = htmlenc(YAJL_GET_STRING(yajl_tree_get(obj, yajl_title, yajl_t_string)));
		strarg_t yajl_sourceURI[] = { "sourceURI", NULL };
		if(!sourceURI_HTMLSafe) sourceURI_HTMLSafe = htmlenc(YAJL_GET_STRING(yajl_tree_get(obj, yajl_sourceURI, yajl_t_string)));



		break;
	}

	str_t *preview;
	int const plen = asprintf(&preview,
		"<div class=\"preview\">\n"
		"	<a href=\"%1$s\"><img class=\"thumbnail\" href=\"%5$s\"></a>\n"
		"	<div class=\"details\">\n"
		"		<div class=\"title field\">\n"
		"			<a href=\"%1$s\">%2$s</a>\n"
		"		</div>\n"
		"		<div class=\"uri field nowrap\">\n"
		"			<a href=\"%4$s\">\n"
		"				<img class=\"favicon\" href=\"%6$s\">\n"
		"				%4$s\n"
		"			</a>\n"
		"		</div>\n"
		"		<div class=\"field nowrap\">%3$s</div>\n"
		"	</div>\n"
		"	<div class=\"clear\"></div>\n"
		"</div>",
		URI_HTMLSafe, title_HTMLSafe, description_HTMLSafe, sourceURI_HTMLSafe, thumbnailURI_HTMLSafe, faviconURI_HTMLSafe);

	FREE(&URI_HTMLSafe);
	FREE(&title_HTMLSafe);
	FREE(&description_HTMLSafe);
	FREE(&sourceURI_HTMLSafe);
	FREE(&thumbnailURI_HTMLSafe);
	FREE(&faviconURI_HTMLSafe);


	if(plen > 0) {
		// TODO: Use temp path for atomicity.
		// TODO: Error handling

		ssize_t const dirlen = dirname(previewPath, strlen(previewPath));
		mkdirp((char *)previewPath, dirlen, 0700); // HAX

		uv_fs_t req = { .data = co_active() };
		uv_fs_open(loop, &req, previewPath, O_CREAT | O_EXCL | O_TRUNC | O_WRONLY, 0400, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);
		uv_file const file = req.result;

		uv_buf_t info = uv_buf_init(preview, plen);
		uv_fs_write(loop, &req, file, &info, 1, 0, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);

		uv_fs_close(loop, &req, file, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);

	}
	if(plen >= 0) FREE(&preview);


	URIListFree(metaURIs);
	EFSFilterFree(filter);
	return 0;
}
static void sendPreview(HTTPConnectionRef const conn, EFSSessionRef const session, strarg_t const URI, EFSFileInfo const *const info, strarg_t const previewPath) {
	// TODO: Real template system
	str_t *URI_HTMLSafe = htmlenc(URI);
	str_t *URI_URISafe = strdup("hash://asdf/asdf"); // TODO: URI enc
	str_t *type_HTMLSafe = htmlenc(info->type);
	unsigned long long const size_ull = info->size;

	str_t *before;
	int const blen = asprintf(&before,
		"<div class=\"entry\">\n"
		"	<div class=\"title\">\n"
		"		<a href=\"?q=%2$s\">%1$s</a>\n"
		"	</div>\n"
		"	<div class=\"content\">",
		URI_HTMLSafe, URI_URISafe, type_HTMLSafe, size_ull);
	if(blen > 0) {
		HTTPConnectionWriteChunkLength(conn, blen);
		HTTPConnectionWrite(conn, (byte_t const *)before, blen);
		HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2);
	}
	if(blen >= 0) FREE(&before);

	if(
		HTTPConnectionWriteChunkFile(conn, previewPath) < 0 &&
		(genPreview(conn, session, URI, info, previewPath) < 0 ||
		HTTPConnectionWriteChunkFile(conn, previewPath) < 0)
	) {
		str_t *msg;
		int const mlen = asprintf(&msg,
			"%1$.0s" "%2$.0s" "%3$.0s" // TODO: HACK
			"<p class=\"light\">(no preview for file of type %3$s)</p>",
			URI_HTMLSafe, URI_URISafe, type_HTMLSafe, size_ull);
		if(mlen > 0) {
			HTTPConnectionWriteChunkLength(conn, mlen);
			HTTPConnectionWrite(conn, (byte_t const *)msg, mlen);
			HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2);
		}
		if(mlen >= 0) FREE(&msg);
	}

	str_t *after;
	int const alen = asprintf(&after,
			"</div>\n"
		"</div>\n",
		URI_HTMLSafe, URI_URISafe, type_HTMLSafe, size_ull);
	if(alen > 0) {
		HTTPConnectionWriteChunkLength(conn, alen);
		HTTPConnectionWrite(conn, (byte_t const *)after, alen);
		HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2);
	}
	if(alen >= 0) FREE(&after);

	FREE(&URI_HTMLSafe);
	FREE(&URI_URISafe);
	FREE(&type_HTMLSafe);
}

static bool_t getPage(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_GET != method) return false;
	size_t pathlen = prefix("/", URI);
	if(!pathlen) return false;
	if(!pathterm(URI, (size_t)pathlen)) return false;
	EFSSessionRef const session = auth(repo, conn, method, URI+pathlen);
	if(!session) {
		HTTPConnectionSendStatus(conn, 403);
		return true;
	}

	// TODO: Parse querystring `q` parameter
	// TODO: Filter OUT previews. Make sure all of the files are real "user files."
	EFSFilterRef const filter = EFSFilterCreate(EFSNoFilter);

	URIListRef const URIs = EFSSessionCreateFilteredURIList(session, filter, RESULTS_MAX); // TODO: We should be able to specify a specific algorithm here.

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionBeginBody(conn);

	str_t *hpath;
	int const hpathlen = asprintf(&hpath, "%s/blog-static/header.html", EFSRepoGetDir(repo));
	if(hpathlen >= 0) {
		HTTPConnectionWriteChunkFile(conn, hpath);
		FREE(&hpath);
	}

	for(index_t i = 0; i < URIListGetCount(URIs); ++i) {
		strarg_t const URI = URIListGetURI(URIs, i);
		str_t const prefix[] = "hash://sha256/";
		strarg_t const hash = URI+sizeof(prefix)-1;
		str_t *previewPath = BlogCopyPreviewPath(repo, hash);
		if(!previewPath) {
			continue;
		}
		EFSFileInfo info;
		if(EFSSessionGetFileInfoForURI(session, &info, URI) < 0) {
			FREE(&previewPath);
			continue;
		}
		sendPreview(conn, session, URI, &info, previewPath);
		FREE(&info.path);
		FREE(&info.type);
		FREE(&previewPath);
	}

	str_t *fpath;
	int const fpathlen = asprintf(&fpath, "%s/blog-static/footer.html", EFSRepoGetDir(repo));
	if(fpathlen >= 0) {
		HTTPConnectionWriteChunkFile(conn, fpath);
		FREE(&fpath);
	}

	HTTPConnectionWriteChunkLength(conn, 0);
	HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2);
	HTTPConnectionEnd(conn);

	URIListFree(URIs);
	return true;
}

bool_t BlogDispatch(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(getPage(repo, conn, method, URI)) return true;


	// TODO: Ignore query parameters, check for `..` (security critical).
	str_t *path;
	int const plen = asprintf(&path, "%s/blog-static/%s", EFSRepoGetDir(repo), URI);
	if(plen < 0) {
		HTTPConnectionSendStatus(conn, 500);
		return true;
	}

	HTTPConnectionSendFile(conn, path, NULL, -1); // TODO: Determine file type.
	FREE(&path);
	return true;
}

