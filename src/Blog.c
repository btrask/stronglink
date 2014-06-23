#include "common.h"
#include "async.h"
#include "EarthFS.h"
#include "HTTPServer.h"
#include "QueryString.h"

// TODO: We should have a real public API, and this should be part of it.
typedef struct {
	str_t *internalHash;
	str_t *type;
	int64_t size;
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
		strarg_t const URI = sqlite3_column_text(select, 0);
		URIListAddURI(URIs, URI, strlen(URI));
	}
	sqlite3_finalize(select);
	EXEC(QUERY(db, "DELETE FROM \"results\"")); // TODO: Shouldn't be necessary?
	EFSRepoDBClose(repo, db);
	return URIs;
}
// TODO: This should probably be generalized to EFSSessionGetFileInfo, accepting a filter and using the first result.
// Ideally it wouldn't be creating and destroying its own DB connection every time too.
err_t EFSSessionGetPreviewInfoForURI(EFSSessionRef const session, EFSFileInfo *const info, strarg_t const URI) {
	if(!session) return -1;
	// TODO: Check session mode.

	// TODO: Public API for easily creating filters.
	// Although this API should be public too.
	EFSFilterRef const URIFilter = EFSFilterCreate(EFSBacklinkFilesFilter);
	EFSFilterAddStringArg(URIFilter, URI, strlen(URI));
	EFSFilterRef const previewFilter = EFSFilterCreate(EFSBacklinkFilesFilter);
	EFSFilterAddStringArg(previewFilter, "efs://preview", 13);
	EFSFilterRef const filter = EFSFilterCreate(EFSIntersectionFilter);
	EFSFilterAddFilterArg(filter, URIFilter);
	EFSFilterAddFilterArg(filter, previewFilter);

	EFSRepoRef const repo = EFSSessionGetRepo(session);
	sqlite3 *const db = EFSRepoDBConnect(repo);
	EFSFilterCreateTempTables(db);
	EFSFilterExec(filter, db, 0);
	sqlite3_stmt *const select = QUERY(db,
		"SELECT f.\"internalHash\", f.\"type\", f.\"size\"\n"
		"FROM \"files\" AS f\n"
		"INNER JOIN \"results\" AS r ON (r.\"fileID\" = f.\"fileID\")\n"
		"ORDER BY r.\"sort\" ASC LIMIT 1");
	if(SQLITE_ROW != sqlite3_step(select)) {
		EFSRepoDBClose(repo, db);
		EFSFilterFree(filter);
		return -1;
	}
	info->internalHash = strdup(sqlite3_column_text(select, 0));
	info->type = strdup(sqlite3_column_text(select, 1));
	info->size = sqlite3_column_int64(select, 2);

	EXEC(QUERY(db, "DELETE FROM \"results\"")); // TODO: Shouldn't be necessary?
	EFSRepoDBClose(repo, db);
	EFSFilterFree(filter);
	return 0;
}


EFSSessionRef auth(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const qs); // Hack.


// Real blog-specific code starts here...


#define RESULTS_MAX 50

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

	URIListRef const URIs = EFSSessionCreateFilteredURIList(session, filter, RESULTS_MAX);

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
	HTTPConnectionWrite(conn, header, sizeof(header)-1);
	HTTPConnectionWrite(conn, "\r\n", 2);

	uv_fs_t req = { .data = co_active() };
	for(index_t i = 0; i < URIListGetCount(URIs); ++i) {
		EFSFileInfo info;
		strarg_t const URI = URIListGetURI(URIs, i);
		if(EFSSessionGetPreviewInfoForURI(session, &info, URI) < 0) {
			str_t const empty[] =
				"<div class=\"entry\">\n"
				"\t" "<div class=\"title\">\n"
				"\t" "\t" "<a href=\"#\">hash://asdf/asdf</a>\n"
				"\t" "</div>"
				"\t" "<div class=\"content\">\n"
				"\t" "\t" "(no preview)\n"
				"\t" "</div>\n"
				"</div>";
			size_t const emptylen = sizeof(empty)-1;
			HTTPConnectionWriteChunkLength(conn, emptylen);
			HTTPConnectionWrite(conn, empty, emptylen);
			HTTPConnectionWrite(conn, "\r\n", 2);
			continue;
		}
		fprintf(stderr, "%s\n\t%s\n", URI, info.internalHash);
		str_t *const path = EFSRepoCopyInternalPath(repo, info.internalHash);
		if(!path) continue;
		uv_fs_open(loop, &req, path, O_RDONLY, 0400, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);
		free(path);
		uv_file const file = req.result;
		if(file < 0) continue;
		str_t const before[] =
			"<div class=\"entry\">\n"
			"<div class=\"title\"><a href=\"#\">hash://asdf/asdf</a></div>\n";
		str_t const after[] = "\n</div>\n";
		size_t const blen = sizeof(before)-1;
		size_t const alen = sizeof(after)-1;
		HTTPConnectionWriteChunkLength(conn, blen + info.size + alen);
		HTTPConnectionWrite(conn, before, blen);
		HTTPConnectionWriteFile(conn, file);
		HTTPConnectionWrite(conn, after, alen);
		HTTPConnectionWrite(conn, "\r\n", 2);
		uv_fs_close(loop, &req, file, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);
	}

	// TODO: Page trailer

	HTTPConnectionWriteChunkLength(conn, 0);
	HTTPConnectionWrite(conn, "\r\n", 2);
	HTTPConnectionEnd(conn);

	URIListFree(URIs);
	return true;
}

bool_t BlogDispatch(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(getPage(repo, conn, method, URI)) return true;
	// TODO: Other resources, 404 page.
	return false;
}

