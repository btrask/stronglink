#define _GNU_SOURCE
#include "../deps/crypt_blowfish-1.0.4/ow-crypt.h"
#include "../deps/sqlite/sqlite3.h"
#include "EarthFS.h"

struct EFSSession {
	EFSRepoRef repo;
	int64_t userID;
	str_t *cookie;
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
static bool_t checkpass(strarg_t const pass, strarg_t const hash) {
	int size = 0;
	void *data = NULL;
	strarg_t attempt = crypt_ra(pass, hash, &data, &size);
	bool_t const success = (attempt && 0 == passcmp(attempt, hash));
	FREE(&data); attempt = NULL;
	return success;
}

EFSSessionRef EFSRepoCreateSession(EFSRepoRef const repo, strarg_t const username, strarg_t const password, strarg_t const cookie, EFSMode const mode) {
	if(!repo) return NULL;
	if(!username && !cookie) return NULL;

	long long sessionID = -1;
	str_t *sessionKey = NULL;
	if(cookie) {
		sessionKey = malloc(strlen(cookie));
		sscanf(cookie, "%lld:%s", &sessionID, sessionKey);
		if(sessionID <= 0) return NULL;
		if('\0' == sessionKey[0]) return NULL;
	}

	sqlite3 *db = EFSRepoDBConnect(repo);
	if(!db) {
		FREE(&sessionKey);
		return NULL;
	}

	sqlite3_stmt *select = QUERY(db,
		"SELECT u.\"userID\", u.\"passhash\", s.\"sessionHash\"\n"
		"FROM \"users\" AS u\n"
		"LEFT JOIN \"sessions\" AS s ON (s.\"userID\" = u.\"userID\")\n"
		"WHERE (u.\"username\" = ?1 OR ?1 IS NULL)\n"
		"AND (s.\"sessionID\" = ?2 OR ?2 = -1) LIMIT 1");
	sqlite3_bind_text(select, 1, username, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(select, 2, sessionID);
	if(SQLITE_ROW != sqlite3_step(select)) {
		fprintf(stderr, "Unrecognized user: %s, cookie: %s\n", username, cookie);
		FREE(&sessionKey);
		sqlite3_finalize(select); select = NULL;
		EFSRepoDBClose(repo, db); db = NULL;
		return NULL;
	}

	int64_t const userID = sqlite3_column_int64(select, 0);
	if(userID <= 0) {
		FREE(&sessionKey);
		sqlite3_finalize(select); select = NULL;
		EFSRepoDBClose(repo, db); db = NULL;
		return NULL;
	}

	// TODO: Save session keys in memory for at least say 30 seconds for performance. Don't do any caching for passwords, not worth it.

	strarg_t passhash = (strarg_t)sqlite3_column_text(select, 1);
	strarg_t sessionHash = (strarg_t)sqlite3_column_text(select, 2);
	bool_t const success = sessionKey ?
		checkpass(sessionKey, sessionHash) :
		checkpass(password, passhash);
	FREE(&sessionKey);
	if(!success) {
		sqlite3_finalize(select); select = NULL;
		EFSRepoDBClose(repo, db); db = NULL;
		return NULL;
	}
	sqlite3_finalize(select); select = NULL;

	EFSSessionRef const session = calloc(1, sizeof(struct EFSSession));
	if(!session) {
		EFSRepoDBClose(repo, db); db = NULL;
		return NULL;
	}

	session->repo = repo;
	session->userID = userID;
	session->mode = mode; // TODO: Validate

	EFSRepoDBClose(repo, db); db = NULL;
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
strarg_t EFSSessionGetCookie(EFSSessionRef const session) {
	if(!session) return NULL;
	return session->cookie;
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

str_t *EFSSessionCreateCookie(EFSSessionRef const session) {
	if(!session) return NULL;
	if(session->userID <= 0) return NULL;
	str_t *sessionKey = strdup("not-very-random"); // TODO: Generate
	if(!sessionKey) return NULL;
	int size = 0;
	void *data = NULL;
	strarg_t sessionHash = crypt_ra(sessionKey, "$2a$08", &data, &size);
	if(!sessionHash) {
		FREE(&data);
		FREE(&sessionKey);
		return NULL;
	}
	sqlite3 *db = EFSRepoDBConnect(session->repo);
	sqlite3_stmt *insert = QUERY(db,
		"INSERT INTO \"sessions\" (\"sessionHash\", \"userID\")\n"
		"SELECT ?, ?");
	sqlite3_bind_text(insert, 1, sessionHash, -1, SQLITE_STATIC);
	sqlite3_bind_int64(insert, 2, session->userID);
	int const status = sqlite3_step(insert);
	FREE(&data); sessionHash = NULL;
	sqlite3_finalize(insert); insert = NULL;
	str_t *cookie = NULL;
	if(SQLITE_DONE == status) {
		long long const sessionID = sqlite3_last_insert_rowid(db);
		if(asprintf(&cookie, "%lld:%s", sessionID, sessionKey) < 0) cookie = NULL;
	}
	EFSRepoDBClose(session->repo, db); db = NULL;
	FREE(&sessionKey);
	return cookie;
}

