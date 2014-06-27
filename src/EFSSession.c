#include <uv.h>
#include "../deps/crypt_blowfish-1.0.4/ow-crypt.h"
#include "../deps/libco/libco.h"
#include "../deps/sqlite/sqlite3.h"
#include "EarthFS.h"

struct EFSSession {
	EFSRepoRef repo;
	int64_t userID;
	EFSMode mode;
};

static int passcmp(volatile strarg_t const a, volatile strarg_t const b) {
	int r = 0;
	for(off_t i = 0; ; ++i) {
		if(a[i] != b[i]) r = -1;
		if(!a[i] || !b[i]) break;
	}
	return r;
}

EFSSessionRef EFSRepoCreateSession(EFSRepoRef const repo, strarg_t const username, strarg_t const password, strarg_t const cookie, EFSMode const mode) {
	if(!repo) return NULL;
	if(!password) return NULL;
	/* TODO: More complex logic needed.
		1. Use cookie if no user/pass provided
		2. Use user/pass if no cookie provided
		3. Use cookie if username matches cookie user ID
		4. Use username if does not match, use user/pass
	*/

	sqlite3 *const db = EFSRepoDBConnect(repo);
	if(!db) return NULL;

	sqlite3_stmt *stmt = NULL;
	(void)BTSQLiteErr(sqlite3_prepare_v2(db,
		"SELECT \"userID\", \"passhash\""
		" FROM \"users\" WHERE \"username\" = ?"
		" LIMIT 1",
		-1, &stmt, NULL));
	(void)BTSQLiteErr(sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT));
	(void)BTSQLiteErr(sqlite3_step(stmt));

	int64_t const userID = sqlite3_column_int64(stmt, 0);
	strarg_t passhash = (strarg_t)sqlite3_column_text(stmt, 1);

	if(!passhash) {
		(void)BTSQLiteErr(sqlite3_finalize(stmt));
		EFSRepoDBClose(repo, db);
		return NULL;
	}

	int size = 0;
	void *data = NULL;
	strarg_t attempt = crypt_ra(password, passhash, &data, &size);
	if(!attempt || 0 != passcmp(attempt, passhash)) {
		FREE(&data); attempt = NULL;
		(void)BTSQLiteErr(sqlite3_finalize(stmt));
		EFSRepoDBClose(repo, db);
		return NULL;
	}
	FREE(&data); attempt = NULL;
	(void)BTSQLiteErr(sqlite3_finalize(stmt));
	EFSRepoDBClose(repo, db);

	if(!userID) return NULL;

	EFSSessionRef const session = calloc(1, sizeof(struct EFSSession));
	session->repo = repo;
	session->userID = userID;
	session->mode = mode; // TODO: Validate
	return session;
}
void EFSSessionFree(EFSSessionRef const session) {
	if(!session) return;
	session->repo = NULL;
	session->userID = -1;
	session->mode = 0;
	free(session);
}
EFSRepoRef const EFSSessionGetRepo(EFSSessionRef const session) {
	if(!session) return NULL;
	return session->repo;
}
int64_t EFSSessionGetUserID(EFSSessionRef const session) {
	if(!session) return -1;
	return session->userID;
}

URIListRef EFSSessionCreateFilteredURIList(EFSSessionRef const session, EFSFilterRef const filter, count_t const max) { // TODO: Sort order, pagination.
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
EFSFileInfo *EFSSessionCopyFileInfo(EFSSessionRef const session, strarg_t const URI) {
	if(!session) return NULL;
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
		return NULL;
	}
	EFSFileInfo *const info = calloc(1, sizeof(EFSFileInfo));
	info->path = EFSRepoCopyInternalPath(repo, (strarg_t)sqlite3_column_text(select, 0));
	info->type = strdup((strarg_t)sqlite3_column_text(select, 1));
	info->size = sqlite3_column_int64(select, 2);
	sqlite3_finalize(select);
	EFSRepoDBClose(repo, db);
	return info;
}
void EFSFileInfoFree(EFSFileInfo *const info) {
	if(!info) return;
	FREE(&info->path);
	FREE(&info->type);
	free(info);
}

