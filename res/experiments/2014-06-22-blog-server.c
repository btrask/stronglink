

// TODO: This should be part of the EFS public interface.
typedef struct {
	str_t *internalHash;
	str_t *type;
	int64_t size;
} EFSFileInfo;
typedef struct {
	count_t count;
	EFSFileInfo items[0];
} EFSFileInfoList;
EFSFileInfo *EFSSessionCreateFileInfoList(EFSSessionRef const session, EFSFilterRef const filter, count_t const max) { // TODO: Return actual count somehow?
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
	sqlite3_bind_int64(select, 0, max);
	EFSFileInfo *const files = malloc(sizeof(EFSFileInfoList) + sizeof(EFSFileInfo) * max);
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
	EFSFilterRef const filter = EFSFilterCreate(EFSTypeFilter);
	EFSFilterAddStringArg(filter, "text/html; charset=utf-8");

	EFSFileInfo *const files = EFSSessionCreateFileInfoList(session, filter, RESULTS_MAX);

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionBeginBody(conn);

	// TODO: Page header

	uv_fs_t req = { .data = co_active(); }
	for(index_t i = 0; i < files->count; ++i) {
		uv_fs_open(loop, &req, path, O_RDONLY, 0400, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);
		uv_file const file = req.result;
		if(file < 0) continue;
		HTTPConnectionWriteChunkLength(conn, files->items[i].size);
		HTTPConnectionWriteFile(conn, file);
		uv_fs_close(loop, &req, file, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);
		HTTPConnectionWrite(conn, "\r\n", 2);
	}

	// TODO: Page trailer

	HTTPConnectionWriteChunkLength(conn, 0);
	HTTPConnectionWrite(conn, "\r\n", 2);
	HTTPConnectionEnd(conn);

	EFSFileInfoListFree(files);
	return true;
}


