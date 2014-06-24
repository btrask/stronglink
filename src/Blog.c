#define _GNU_SOURCE
#include "common.h"
#include "async.h"
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
		"ORDER BY r.\"sort\" ASC LIMIT ?");
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

static err_t sendPreview(HTTPConnectionRef const conn, strarg_t const previewPath) {
	uv_fs_t req = { .data = co_active() };
	uv_fs_open(loop, &req, previewPath, O_RDONLY, 0400, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	if(req.result < 0) return -1;
	uv_file const file = req.result;
	uv_fs_fstat(loop, &req, file, async_fs_cb);
	co_switch(yield);
	uint64_t const size = req.statbuf.st_size;
	uv_fs_req_cleanup(&req);
	if(req.result > 0) {
		HTTPConnectionWriteChunkLength(conn, size);
		HTTPConnectionWriteFile(conn, file);
		HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2);
	}
	uv_fs_close(loop, &req, file, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	return req.result > 0 ? 0 : -1;
}
static err_t sendNewPreview(HTTPConnectionRef const conn, strarg_t const URI, EFSFileInfo const *const info, strarg_t const previewPath) {
	str_t *tmpPath = NULL; // TODO: EFSRepoCopyTempPath()
	if(!tmpPath) return -1;

	// TODO: mkdirp tmpdir

	uv_fs_t req = { .data = co_active() };
	uv_fs_open(loop, &req, tmpPath, O_CREAT | O_EXCL | O_WRONLY, 0400, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	uv_file const tmp = req.result;
	if(tmp < 0) return -1;

	// TODO: Generate preview, send it and save it.
	fprintf(stderr, "Generation of preview at %s not implemented\n", previewPath);

	uv_fs_close(loop, &req, tmp, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);

	// TODO: mkdir preview dir
	uv_fs_link(loop, &req, tmpPath, previewPath, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);

	uv_fs_unlink(loop, &req, tmpPath, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);

	return -1; // TODO
}
static err_t sendPlaceholder(HTTPConnectionRef const conn, strarg_t const URI, EFSFileInfo const *const info) {
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

		if(sendPreview(conn, previewPath) < 0) {
			EFSFileInfo info;
			if(EFSSessionGetFileInfoForURI(session, &info, URI) < 0) {
				FREE(&previewPath);
				continue;
			}

			if(sendNewPreview(conn, URI, &info, previewPath) < 0) {

				sendPlaceholder(conn, URI, &info);

			}

			FREE(&info.path);
			FREE(&info.type);
		}

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

