#include "../deps/crypt_blowfish-1.0.4/ow-crypt.h"
#include "../deps/sqlite/sqlite3.h"
#include "EarthFS.h"

struct EFSSession {
	EFSRepoRef repo;
	int64_t userID;
	EFSMode mode;
};

static int passcmp(strarg_t const a, strarg_t const b) {
	volatile int r = 0;
	for(off_t i = 0; ; ++i) {
		if(a[i] != b[i]) r = -1;
		if(!a[i] || !b[i]) break;
	}
	return r;
}

EFSSessionRef EFSRepoCreateSession(EFSRepoRef const repo, strarg_t const username, strarg_t const password, strarg_t const cookie, EFSMode const mode) {
	if(!repo) return NULL;

	sqlite3 *db = NULL;
	int const err = BTSQLiteErr(sqlite3_open_v2(
		EFSRepoGetDBPath(repo),
		&db,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX,
		NULL
	));
	if(SQLITE_OK != err) {
		return NULL;
	}

	sqlite3_stmt *stmt = NULL;
	(void)BTSQLiteErr(sqlite3_prepare_v2(db,
		"SELECT \"userID\", \"passhash\""
		" FROM \"users\" WHERE \"username\" = ?"
		" LIMIT 1",
		-1, &stmt, NULL));
	(void)BTSQLiteErr(sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT));
	(void)BTSQLiteErr(sqlite3_step(stmt));

	int64_t const userID = sqlite3_column_int(stmt, 0);
	strarg_t passhash = (strarg_t)sqlite3_column_text(stmt, 1);

	if(!userID || !passhash) {
		(void)BTSQLiteErr(sqlite3_finalize(stmt));
		return NULL;
	}

	int size = 0;
	void *data = NULL;
	strarg_t attempt = crypt_ra(password, passhash, &data, &size);
	if(!attempt || 0 != passcmp(attempt, passhash)) {
		FREE(&data); attempt = NULL;
		(void)BTSQLiteErr(sqlite3_finalize(stmt));
		return NULL;
	}
	FREE(&data); attempt = NULL;
	(void)BTSQLiteErr(sqlite3_finalize(stmt));
	(void)BTSQLiteErr(sqlite3_close(db));

	EFSSessionRef const session = calloc(1, sizeof(struct EFSSession));
	session->repo = repo;
	session->userID = userID;
	session->mode = EFS_RDWR; // TODO: Check token and use input.
	return session;
}
void EFSSessionFree(EFSSessionRef const session) {
	if(!session) return;
	session->repo = NULL;
/*	FREE(&session->user);
	FREE(&session->pass);
	FREE(&session->cookie);
	(void)BTSQLiteErr(sqlite3_close(session->db)); session->db = NULL;*/
	free(session);
}
/*sqlite3 *EFSSessionGetDB(EFSSessionRef const session) {
	if(!session) return NULL;
	return session->db;
}*/

