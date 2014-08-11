#define _GNU_SOURCE
#include "../deps/sqlite/sqlite3.h"
#include "async.h"
#include "bcrypt.h"
#include "EarthFS.h"

#define COOKIE_CACHE_SIZE 1000

struct cached_cookie {
	int64_t sessionID;
	str_t *sessionKey;
	uint64_t atime; // TODO: Prune old entries.
};
static struct cached_cookie cookie_cache[COOKIE_CACHE_SIZE] = {};

static bool_t cookie_cache_lookup(int64_t const sessionID, strarg_t const sessionKey) {
	assert(!async_worker_get_current());
	if(sessionID <= 0 || !sessionKey) return false;
	index_t const x = sessionID+sessionKey[0] % COOKIE_CACHE_SIZE;
	if(cookie_cache[x].sessionID != sessionID) return false;
	if(!cookie_cache[x].sessionKey) return false;
	return 0 == passcmp(sessionKey, cookie_cache[x].sessionKey);
}
static void cookie_cache_store(int64_t const sessionID, strarg_t const sessionKey) {
	assert(!async_worker_get_current());
	if(sessionID <= 0 || !sessionKey) return;
	index_t const x = sessionID+sessionKey[0] % COOKIE_CACHE_SIZE;
	FREE(&cookie_cache[x].sessionKey);
	cookie_cache[x].sessionID = sessionID;
	cookie_cache[x].sessionKey = strdup(sessionKey);
	cookie_cache[x].atime = uv_now(loop);
}

struct EFSSession {
	EFSRepoRef repo;
	int64_t userID;
};

str_t *EFSRepoCreateCookie(EFSRepoRef const repo, strarg_t const username, strarg_t const password) {
	if(!repo) return NULL;
	if(!username) return NULL;
	if(!password) return NULL;

	sqlite3f *db = EFSRepoDBConnect(repo);
	if(!db) return NULL;

	sqlite3_stmt *select = QUERY(db,
		"SELECT user_id, password_hash\n"
		"FROM users WHERE username = ?");
	sqlite3_bind_text(select, 1, username, -1, SQLITE_STATIC);
	if(SQLITE_ROW != STEP(select)) {
		sqlite3f_finalize(select);
		EFSRepoDBClose(repo, &db);
		return NULL;
	}
	int64_t const userID = sqlite3_column_int64(select, 0);
	str_t *passhash = strdup((char const *)sqlite3_column_text(select, 1));
	sqlite3f_finalize(select); select = NULL;
	if(userID <= 0 && !checkpass(password, passhash)) {
		FREE(&passhash);
		EFSRepoDBClose(repo, &db);
		return NULL;
	}
	FREE(&passhash);

	str_t *sessionKey = strdup("not-very-random"); // TODO: Generate
	if(!sessionKey) {
		EFSRepoDBClose(repo, &db);
		return NULL;
	}
	str_t *sessionHash = hashpass(sessionKey);
	if(!sessionHash) {
		FREE(&sessionHash);
		FREE(&sessionKey);
		EFSRepoDBClose(repo, &db);
		return NULL;
	}

	sqlite3_stmt *insert = QUERY(db,
		"INSERT INTO sessions (session_hash, user_id)\n"
		"SELECT ?, ?");
	sqlite3_bind_text(insert, 1, sessionHash, -1, SQLITE_STATIC);
	sqlite3_bind_int64(insert, 2, userID);
	int const status = STEP(insert);
	FREE(&sessionHash);
	sqlite3f_finalize(insert); insert = NULL;
	str_t *cookie = NULL;
	if(SQLITE_DONE == status) {
		long long const sessionID = sqlite3_last_insert_rowid(db->conn);
		if(asprintf(&cookie, "%lld:%s", sessionID, sessionKey) < 0) cookie = NULL;
	}

	EFSRepoDBClose(repo, &db);
	FREE(&sessionKey);
	return cookie;
}
EFSSessionRef EFSRepoCreateSession(EFSRepoRef const repo, strarg_t const cookie) {
	if(!repo) return NULL;
	if(!cookie) return NULL;

	long long sessionID = -1;
	str_t *sessionKey = calloc(strlen(cookie)+1, 1);
	if(!sessionKey) return NULL;
	sscanf(cookie, "s=%lld:%s", &sessionID, sessionKey);
	if(sessionID <= 0 || '\0' == sessionKey[0]) {
		FREE(&sessionKey);
		return NULL;
	}

	sqlite3f *db = EFSRepoDBConnect(repo);
	if(!db) {
		FREE(&sessionKey);
		return NULL;
	}
	sqlite3_stmt *select = QUERY(db,
		"SELECT user_id, session_hash\n"
		"FROM sessions WHERE session_id = ?");
	sqlite3_bind_int64(select, 1, sessionID);
	if(SQLITE_ROW != STEP(select)) {
		FREE(&sessionKey);
		sqlite3f_finalize(select); select = NULL;
		EFSRepoDBClose(repo, &db);
		return NULL;
	}
	int64_t const userID = sqlite3_column_int64(select, 0);
	str_t *sessionHash = strdup((char const *)sqlite3_column_text(select, 1));
	sqlite3f_finalize(select); select = NULL;
	EFSRepoDBClose(repo, &db);

	if(userID <= 0) {
		FREE(&sessionKey);
		FREE(&sessionHash);
		return NULL;
	}

	if(!cookie_cache_lookup(sessionID, sessionKey)) {
		if(!checkpass(sessionKey, sessionHash)) {
			FREE(&sessionKey);
			FREE(&sessionHash);
			return NULL;
		}
		cookie_cache_store(sessionID, sessionKey);
	}
	FREE(&sessionKey);
	FREE(&sessionHash);

	return EFSRepoCreateSessionInternal(repo, userID);
}
EFSSessionRef EFSRepoCreateSessionInternal(EFSRepoRef const repo, int64_t const userID) {
	EFSSessionRef const session = calloc(1, sizeof(struct EFSSession));
	if(!session) return NULL;
	session->repo = repo;
	session->userID = userID;
	return session;
}
void EFSSessionFree(EFSSessionRef *const sessionptr) {
	EFSSessionRef session = *sessionptr;
	if(!session) return;
	session->repo = NULL;
	session->userID = -1;
	FREE(sessionptr); session = NULL;
}
EFSRepoRef EFSSessionGetRepo(EFSSessionRef const session) {
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
	sqlite3f *db = EFSRepoDBConnect(repo);
	if(!db) return NULL;

	URIListRef const URIs = URIListCreate(); // TODO: Just preallocate a regular array, since we know the maximum size. Get rid of URILists all together.

	// TODO: Pagination
	int64_t const initialSortID = INT64_MAX;
	int64_t const initialFileID = INT64_MAX;

	EFSFilterPrepare(filter, db);

	// It'd be nice to combine these two into one query, but the query optimizer was being stupid. Basically, we're just doing a manual JOIN with `WHERE (sort_id = ?1 AND file_id < ?2) OR sort_id < ?1` and `ORDER BY sort_id DESC, file_id DESC`.
	// The problems with the query optimizer are: 1. it doesn't like SELECT DISTINCT (or GROUP BY) with two args, even if it's sorted on both of them, and 2. we have to use a temp b-tree for the second ORDER BY either way, but I think it's slower in a larger query...
	sqlite3_stmt *selectMetaFiles = QUERY(db,
		"SELECT DISTINCT file_id AS sort_id\n"
		"FROM meta_files\n"
		"WHERE sort_id <= ?\n"
		"ORDER BY sort_id DESC");
	sqlite3_bind_int64(selectMetaFiles, 1, initialSortID);
	sqlite3_stmt *selectFiles = QUERY(db,
		"SELECT f.file_id\n"
		"FROM meta_files AS mf\n"
		"INNER JOIN file_uris AS f ON (f.uri = mf.target_uri)\n"
		"WHERE mf.file_id = ? AND f.file_id < ?\n"
		"ORDER BY f.file_id DESC");

	sqlite3_stmt *selectHash = QUERY(db,
		"SELECT internal_hash\n"
		"FROM files WHERE file_id = ? LIMIT 1");

	EXEC(QUERY(db, "BEGIN DEFERRED TRANSACTION"));
	while(SQLITE_ROW == STEP(selectMetaFiles)) {
		int64_t const sortID = sqlite3_column_int64(selectMetaFiles, 0);

		sqlite3_bind_int64(selectFiles, 1, sortID);
		sqlite3_bind_int64(selectFiles, 2, initialSortID == sortID ? initialFileID : INT64_MAX);
		while(SQLITE_ROW == STEP(selectFiles)) {
			int64_t const fileID = sqlite3_column_int64(selectFiles, 0);
			int64_t const age = EFSFilterMatchAge(filter, sortID, fileID);
//			fprintf(stderr, "{%lld, %lld} -> %lld\n", sortID, fileID, age);
			if(age != sortID) continue;
			sqlite3_bind_int64(selectHash, 1, fileID);
			if(SQLITE_ROW == STEP(selectHash)) {
				strarg_t const hash = (strarg_t)sqlite3_column_text(selectHash, 0);
				str_t *URI = EFSFormatURI(EFS_INTERNAL_ALGO, hash);
				URIListAddURI(URIs, URI, -1);
				FREE(&URI);
			}
			sqlite3_reset(selectHash);
			if(URIListGetCount(URIs) >= max) break;
		}
		sqlite3_reset(selectFiles);
		if(URIListGetCount(URIs) >= max) break;
	}
	EXEC(QUERY(db, "COMMIT"));

	sqlite3f_finalize(selectHash); selectHash = NULL;
	sqlite3f_finalize(selectFiles); selectFiles = NULL;
	sqlite3f_finalize(selectMetaFiles); selectMetaFiles = NULL;

	EFSRepoDBClose(repo, &db);
	return URIs;
}
err_t EFSSessionGetFileInfo(EFSSessionRef const session, strarg_t const URI, EFSFileInfo *const info) {
	if(!session) return -1;
	if(!URI) return -1;
	// TODO: Check session mode.
	EFSRepoRef const repo = EFSSessionGetRepo(session);
	sqlite3f *db = EFSRepoDBConnect(repo);
	sqlite3_stmt *const select = QUERY(db,
		"SELECT f.internal_hash, f.file_type, f.file_size\n"
		"FROM files AS f\n"
		"INNER JOIN file_uris AS f2 ON (f.file_id = f2.file_id)\n"
		"WHERE f2.uri = ? LIMIT 1");
	sqlite3_bind_text(select, 1, URI, -1, SQLITE_STATIC);
	if(SQLITE_ROW != STEP(select)) {
		sqlite3f_finalize(select);
		EFSRepoDBClose(repo, &db);
		return -1;
	}
	if(info) {
		info->path = EFSRepoCopyInternalPath(repo, (strarg_t)sqlite3_column_text(select, 0));
		info->type = strdup((strarg_t)sqlite3_column_text(select, 1));
		info->size = sqlite3_column_int64(select, 2);
	}
	sqlite3f_finalize(select);
	EFSRepoDBClose(repo, &db);
	return 0;
}
void EFSFileInfoCleanup(EFSFileInfo *const info) {
	if(!info) return;
	FREE(&info->path);
	FREE(&info->type);
}

