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
static str_t *createCookie(sqlite3 *const db, int64_t const userID) {
	sqlite3_stmt *const insert = QUERY(db,
		"INSERT INTO \"sessions\" (\"sessionKey\", \"userID\")\n"
		"SELECT lower(hex(randomblob(16))), ?");
	sqlite3_bind_int64(insert, 1, userID);
	int status;
	while(SQLITE_CONSTRAINT_UNIQUE == (status = sqlite3_step(insert)));
	sqlite3_finalize(insert);
	if(SQLITE_DONE != status) return NULL;
	sqlite3_stmt *const select = QUERY(db,
		"SELECT \"sessionKey\" FROM \"sessions\"\n"
		"WHERE \"sessionID\" = last_insert_rowid()");
	str_t *cookie = NULL;
	if(SQLITE_ROW == sqlite3_step(select)) cookie = strdup(sqlite3_column_text(select, 0));
	sqlite3_finalize(select);
	return cookie;
}

EFSSessionRef EFSRepoCreateSession(EFSRepoRef const repo, strarg_t const username, strarg_t const password, strarg_t const cookie, EFSMode const mode) {
	if(!repo) return NULL;
	if(!username && !cookie) return NULL;

	// TODO: Cookies are "password-equivalent," but we don't hash them for performance. That means that if our database is leaked and we don't know about it in order to clear the sessions table promptly, attackers can log in as any user. Perhaps a better tradeoff would be to hash session keys, but cache them in memory for a period of time so that repeated requests are still fast.

	sqlite3 *db = EFSRepoDBConnect(repo);
	if(!db) return NULL;

	sqlite3_stmt *select = QUERY(db,
		"SELECT u.\"userID\", u.\"passhash\", s.\"sessionKey\""
		"FROM \"users\" AS u\n"
		"LEFT JOIN \"sessions\" AS s ON (s.\"userID\" = u.\"userID\")\n"
		"WHERE (u.\"username\" = ?1 OR ?1 IS NULL)\n"
		"AND (s.\"sessionKey\" = ?2 OR ?2 IS NULL) LIMIT 1");

	// TODO: It turns out that looking up session keys like this is a terrible idea. We're so careful we wrote our own `passcmp`, but what do you think the database is going to do? We need to treat session ID like the username, and session key like the password (including hashing).

	sqlite3_bind_text(select, 1, username, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(select, 2, cookie, -1, SQLITE_TRANSIENT);
	if(SQLITE_ROW != sqlite3_step(select)) {
		fprintf(stderr, "Unrecognized user: %s, cookie: %s\n", username, cookie);
		sqlite3_finalize(select); select = NULL;
		EFSRepoDBClose(repo, db); db = NULL;
		return NULL;
	}

	int64_t const userID = sqlite3_column_int64(select, 0);
	if(userID <= 0) {
		sqlite3_finalize(select); select = NULL;
		EFSRepoDBClose(repo, db); db = NULL;
		return NULL;
	}

	if(!cookie) {
		strarg_t passhash = (strarg_t)sqlite3_column_text(select, 1);
		int size = 0;
		void *data = NULL;
		strarg_t attempt = crypt_ra(password, passhash, &data, &size);
		bool_t const success = (attempt && 0 == passcmp(attempt, passhash));
		FREE(&data); attempt = NULL;
		if(!success) {
			fprintf(stderr, "Incorrect username or password\n");
			sqlite3_finalize(select); select = NULL;
			EFSRepoDBClose(repo, db); db = NULL;
			return NULL;
		}
	}
	sqlite3_finalize(select); select = NULL;

	EFSSessionRef const session = calloc(1, sizeof(struct EFSSession));
	str_t *const ourCookie = cookie ? strdup(cookie) : createCookie(db, userID);
	if(!session || !ourCookie) {
		EFSRepoDBClose(repo, db); db = NULL;
		return NULL;
	}

	session->repo = repo;
	session->userID = userID;
	session->cookie = ourCookie;
	session->mode = mode; // TODO: Validate

	EFSRepoDBClose(repo, db); db = NULL;
	return session;
}
void EFSSessionFree(EFSSessionRef const session) {
	if(!session) return;
	session->repo = NULL;
	session->userID = -1;
	FREE(&session->cookie);
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

