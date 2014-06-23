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
typedef struct {
	count_t count;
	EFSFileInfo items[0];
} EFSFileInfoList;

EFSFileInfoList *EFSSessionCreateFileInfoList(EFSSessionRef const session, EFSFilterRef const filter, count_t const max) { // TODO: Return actual count somehow?
	if(!session) return NULL;
	// TODO: Check session mode.
	EFSRepoRef const repo = EFSSessionGetRepo(session);
	sqlite3 *const db = EFSRepoDBConnect(repo);
	EFSFilterCreateTempTables(db);
	EFSFilterExec(filter, db, 0);
	sqlite3_stmt *const select = QUERY(db,
		"SELECT f.\"internalHash\", f.\"type\", f.\"size\"\n"
		"FROM \"results\" AS r\n"
		"LEFT JOIN \"files\" AS f ON (f.\"fileID\" = r.\"fileID\")\n"
		"ORDER BY r.\"sort\" ASC LIMIT ?");
	sqlite3_bind_int64(select, 1, max);
	EFSFileInfoList *const files = malloc(sizeof(EFSFileInfoList) + sizeof(EFSFileInfo) * max);
	index_t i = 0;
	for(; i < max; ++i) {
		if(SQLITE_ROW != sqlite3_step(select)) break;
		files->items[i].internalHash = strdup(sqlite3_column_text(select, 0));
		files->items[i].type = strdup(sqlite3_column_text(select, 1));
		files->items[i].size = sqlite3_column_int64(select, 2);
	}
	files->count = i;
	sqlite3_finalize(select);
	EFSRepoDBClose(repo, db);
	return files;
}
void EFSFileInfoListFree(EFSFileInfoList *const list) {
	if(!list) return;
	for(index_t i = 0; i < list->count; ++i) {
		FREE(&list->items[i].internalHash);
		FREE(&list->items[i].type);
		list->items[i].size = 0;
	}
	free(list);
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
	// TODO: Make sure the results are marked as previews (security critical)
	EFSFilterRef const filter = EFSFilterCreate(EFSTypeFilter);
	strarg_t const previewType = "text/preview+html; charset=utf-8"; // TODO: HACK
	EFSFilterAddStringArg(filter, previewType, strlen(previewType));

	EFSFileInfoList *const files = EFSSessionCreateFileInfoList(session, filter, RESULTS_MAX);

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
	for(index_t i = 0; i < files->count; ++i) {
		str_t *const path = EFSRepoCopyInternalPath(repo, files->items[i].internalHash);
		if(!path) continue;
		uv_fs_open(loop, &req, path, O_RDONLY, 0400, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);
		free(path);
		uv_file const file = req.result;
		if(file < 0) continue;
		str_t const before[] = "<div class=\"entry\">";
		str_t const after[] = "</div>";
		size_t const blen = sizeof(before)-1;
		size_t const alen = sizeof(after)-1;
		HTTPConnectionWriteChunkLength(conn, files->items[i].size + blen + alen);
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

	EFSFileInfoListFree(files);
	return true;
}

bool_t BlogDispatch(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(getPage(repo, conn, method, URI)) return true;
	// TODO: Other resources, 404 page.
	return false;
}

