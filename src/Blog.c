#define _GNU_SOURCE
#include "common.h"
#include "async.h"
#include "fs.h"
#include "EarthFS.h"
#include "HTTPServer.h"
#include "QueryString.h"

// TODO: We should have a real public API, and this should be part of it.
typedef struct {
	str_t *path;
	str_t *type;
	uint64_t size;
} EFSFileInfo;

URIListRef EFSSessionCreateFilteredURIList(EFSSessionRef const session, EFSFilterRef const filter, count_t const max) {
	if(!session) return NULL;
	// TODO: Check session mode.
	EFSRepoRef const repo = EFSSessionGetRepo(session);
	sqlite3 *const db = EFSRepoDBConnect(repo);
	EFSFilterCreateTempTables(db);
	EFSFilterExec(filter, db, 0);
	sqlite3_stmt *const select = QUERY(db,
		"SELECT ('hash://' || ? || '/' || f.\"internalHash\")\n"
		"FROM \"files\" AS f\n"
		"INNER JOIN \"results\" AS r ON (r.\"fileID\" = f.\"fileID\")\n"
		"ORDER BY r.\"sort\" DESC LIMIT ?");
	sqlite3_bind_text(select, 1, "sha256", -1, SQLITE_STATIC);
	sqlite3_bind_int64(select, 2, max);
	URIListRef const URIs = URIListCreate();
	while(SQLITE_ROW == sqlite3_step(select)) {
		strarg_t const URI = (strarg_t)sqlite3_column_text(select, 0);
		URIListAddURI(URIs, URI, strlen(URI));
	}
	sqlite3_finalize(select);
	EFSRepoDBClose(repo, db);
	return URIs;
}
err_t EFSSessionGetFileInfoForURI(EFSSessionRef const session, EFSFileInfo *const info, strarg_t const URI) {
	if(!session) return -1;
	// TODO: Check session mode.
	EFSRepoRef const repo = EFSSessionGetRepo(session);
	sqlite3 *const db = EFSRepoDBConnect(repo);
	sqlite3_stmt *const select = QUERY(db,
		"SELECT f.\"internalHash\", f.\"type\", f.\"size\"\n"
		"FROM \"files\" AS f\n"
		"LEFT JOIN \"fileURIs\" AS f2 ON (f2.\"fileID\" = f.\"fileID\")\n"
		"LEFT JOIN \"URIs\" AS u ON (u.\"URIID\" = f2.\"URIID\")\n"
		"WHERE u.\"URI\" = ? LIMIT 1");
	sqlite3_bind_text(select, 1, URI, -1, SQLITE_STATIC);
	if(SQLITE_ROW != sqlite3_step(select)) {
		sqlite3_finalize(select);
		EFSRepoDBClose(repo, db);
		return -1;
	}
	info->path = EFSRepoCopyInternalPath(repo, (strarg_t)sqlite3_column_text(select, 0));
	info->type = strdup((strarg_t)sqlite3_column_text(select, 1));
	info->size = sqlite3_column_int64(select, 2);
	sqlite3_finalize(select);
	EFSRepoDBClose(repo, db);
	return 0;
}


EFSSessionRef auth(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const qs); // Hack.


// Real blog-specific code starts here...


#define RESULTS_MAX 50

static str_t *BlogCopyPreviewPath(EFSRepoRef const repo, strarg_t const hash) {
	str_t *path;
	if(asprintf(&path, "%s/blog/%.2s/%s", EFSRepoGetCacheDir(repo), hash, hash) < 0) return NULL;
	return path;
}
// TODO: Use a real library or put this somewhere.
static str_t *htmlenc(strarg_t const str) {
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

static err_t sendPreview(EFSRepoRef const repo, HTTPConnectionRef const conn, strarg_t const URI, EFSFileInfo const *const info, strarg_t const previewPath) {
	uv_fs_t req = { .data = co_active() };
	uv_fs_open(loop, &req, previewPath, O_RDONLY, 0400, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	if(req.result < 0) return -1;
	uv_file const file = req.result;

	uv_fs_fstat(loop, &req, file, async_fs_cb);
	co_switch(yield);
	if(req.result < 0) {
		uv_fs_req_cleanup(&req);

		uv_fs_close(loop, &req, file, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);
		return -1;
	}
	uint64_t const size = req.statbuf.st_size;
	uv_fs_req_cleanup(&req);

	// TODO: Don't copy and paste
	// TODO: Real template system
	str_t *URI_HTMLSafe = htmlenc(URI);
	str_t *URI_URISafe = strdup("hash://asdf/asdf"); // TODO: URI enc
	str_t *type_HTMLSafe = htmlenc(info->type);
	unsigned long long const size_ull = info->size;

	str_t *before;
	int const blen = asprintf(&before,
		"<div class=\"entry\">\n"
		"\t" "<div class=\"title\">\n"
		"\t" "\t" "<a href=\"?q=%2$s\">%1$s</a>\n"
		"\t" "</div>"
		"\t" "<div class=\"content\">",
		URI_HTMLSafe, URI_URISafe, type_HTMLSafe, size_ull);
	if(blen < 0) before = NULL;
	str_t *after;
	int const alen = asprintf(&after,
			"</div>\n"
		"</div>",
		URI_HTMLSafe, URI_URISafe, type_HTMLSafe, size_ull);
	if(alen < 0) after = NULL;

	bool_t sent = false;
	if(blen >= 0 && alen >= 0) {
		HTTPConnectionWriteChunkLength(conn, blen+size+alen);
		HTTPConnectionWrite(conn, (byte_t const *)before, blen);
		HTTPConnectionWriteFile(conn, file);
		HTTPConnectionWrite(conn, (byte_t const *)after, alen);
		HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2);
		sent = true;
	}
	FREE(&before);
	FREE(&after);

	uv_fs_close(loop, &req, file, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	return sent ? 0 : -1;
}
static err_t sendNewPreview(EFSRepoRef const repo, HTTPConnectionRef const conn, strarg_t const URI, EFSFileInfo const *const info, strarg_t const previewPath) {

	if(0 != strcasecmp("text/markdown; charset=utf-8", info->type)) return -1; // TODO: Other types, plugins, w/e.

	str_t *tmpPath = EFSRepoCopyTempPath(repo);
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
		"/usr/bin/pandoc",
		"-f", "markdown",
		"-t", "html",
		"--strict",
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
	return sendPreview(repo, conn, URI, info, previewPath);
}
static err_t sendPlaceholder(EFSRepoRef const repo, HTTPConnectionRef const conn, strarg_t const URI, EFSFileInfo const *const info) {
	str_t *URI_HTMLSafe = htmlenc(URI);
	str_t *URI_URISafe = strdup("hash://asdf/asdf"); // TODO: URI enc
	str_t *type_HTMLSafe = htmlenc(info->type);
	unsigned long long const size_ull = info->size;

	str_t *str;
	int const len = asprintf(&str,
		"<div class=\"entry\">\n"
		"\t" "<div class=\"title\">\n"
		"\t" "\t" "<a href=\"?q=%2$s\">%1$s</a>\n"
		"\t" "</div>"
		"\t" "<div class=\"content\">\n"
		"\t" "\t" "(no preview for file of type %3$s)\n"
		"\t" "</div>\n"
		"</div>", URI_HTMLSafe, URI_URISafe, type_HTMLSafe, size_ull);
	if(len >= 0) {
		HTTPConnectionWriteChunkLength(conn, len);
		HTTPConnectionWrite(conn, (byte_t const *)str, len);
		HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2);
		FREE(&str);
	}
	FREE(&URI_HTMLSafe);
	FREE(&URI_URISafe);
	FREE(&type_HTMLSafe);
	return len >= 0 ? 0 : -1;
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

	// TODO: Load the header from a configurable file
	// Include a search field and stuff
	str_t const header[] =
		"<!doctype html>\n"
		"<meta charset=\"utf-8\">\n"
		"<title>EarthFS Blog Test</title>\n";
	HTTPConnectionWriteChunkLength(conn, sizeof(header)-1);
	HTTPConnectionWrite(conn, (byte_t const *)header, sizeof(header)-1);
	HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2);

	for(index_t i = 0; i < URIListGetCount(URIs); ++i) {
		strarg_t const URI = URIListGetURI(URIs, i);
		str_t const prefix[] = "hash://sha256/";
		strarg_t const hash = URI+sizeof(prefix)-1;
		str_t *previewPath = BlogCopyPreviewPath(repo, hash);

		EFSFileInfo info;
		if(EFSSessionGetFileInfoForURI(session, &info, URI) < 0) {
			FREE(&previewPath);
			continue;
		}

		if(sendPreview(repo, conn, URI, &info, previewPath) < 0) {

			if(sendNewPreview(repo, conn, URI, &info, previewPath) < 0) {

				sendPlaceholder(repo, conn, URI, &info);

			}

		}

		FREE(&info.path);
		FREE(&info.type);
		FREE(&previewPath);
	}

	// TODO: Page trailer

	HTTPConnectionWriteChunkLength(conn, 0);
	HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2);
	HTTPConnectionEnd(conn);

	URIListFree(URIs);
	return true;
}

bool_t BlogDispatch(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(getPage(repo, conn, method, URI)) return true;
	// TODO: Other resources, 404 page.
	return false;
}

