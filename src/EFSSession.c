#include <stdlib.h>
#include "EarthFS.h"

struct EFSSession {
	EFSRepoRef repo;
	str_t *user;
	str_t *pass;
	str_t *cookie;
//	sqlite3 *db;
	int userID; // TODO: Use sqlite3-appropriate type.
	// TODO: Mode? Read/write?
};

EFSSessionRef EFSRepoCreateSession(EFSRepoRef const repo, str_t const *const user, str_t const *const pass, str_t const *const cookie) { // TODO: Mode?
	if(!repo) return NULL;
	EFSSessionRef const session = calloc(1, sizeof(struct EFSSession));
	session->repo = repo;
	session->user = strdup(user);
	session->pass = strdup(pass);
	session->cookie = strdup(cookie);
/*	int const err = BTSQLiteErr(sqlite3_open_v2(
		EFSRepoGetDBPath(repo),
		&session->db,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX,
		NULL
	));
	if(SQLITE_OK != err) {
		EFSSessionFree(session);
		return NULL;
	}*/
	return session;
}
void EFSSessionFree(EFSSessionRef const session) {
	if(!session) return;
	session->repo = NULL;
	free(session->user); session->user = NULL;
	free(session->pass); session->pass = NULL;
	free(session->cookie); session->cookie = NULL;
//	(void)BTSQLiteErr(sqlite3_close(session->db)); session->db = NULL;
	free(session);
}
/*sqlite3 *EFSSessionGetDB(EFSSessionRef const session) {
	if(!session) return NULL;
	return session->db;
}*/

