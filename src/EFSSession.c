#include "../deps/sqlite/sqlite3.h"
#include "EarthFS.h"

struct EFSSession {
	EFSRepoRef repo;
	str_t *user;
	str_t *pass;
	str_t *cookie;
	sqlite3 *db;
	int userID; // TODO: Use sqlite3-appropriate type.
	// TODO: Mode? Read/write?
};

EFSSessionRef EFSRepoCreateSession(EFSRepoRef const repo, strarg_t const user, strarg_t const pass, strarg_t const cookie, EFSMode const mode) {
	if(!repo) return NULL;
	EFSSessionRef const session = calloc(1, sizeof(struct EFSSession));
	session->repo = repo;
	// TODO: We don't need to keep these in memory, do we? Especially the password.
//	session->user = strdup(user);
//	session->pass = strdup(pass);
//	session->cookie = strdup(cookie);
	// TODO: Mode?
	int const err = BTSQLiteErr(sqlite3_open_v2(
		EFSRepoGetDBPath(repo),
		&session->db,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX,
		NULL
	));
	if(SQLITE_OK != err) {
		EFSSessionFree(session);
		return NULL;
	}
	return session;
}
void EFSSessionFree(EFSSessionRef const session) {
	if(!session) return;
	session->repo = NULL;
	FREE(&session->user);
	FREE(&session->pass);
	FREE(&session->cookie);
	(void)BTSQLiteErr(sqlite3_close(session->db)); session->db = NULL;
	free(session);
}
/*sqlite3 *EFSSessionGetDB(EFSSessionRef const session) {
	if(!session) return NULL;
	return session->db;
}*/

