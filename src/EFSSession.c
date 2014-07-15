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
	if(sessionID <= 0 || !sessionKey) return false;
	index_t const x = sessionID+sessionKey[0] % COOKIE_CACHE_SIZE;
	if(cookie_cache[x].sessionID != sessionID) return false;
	if(!cookie_cache[x].sessionKey) return false;
	return 0 == passcmp(sessionKey, cookie_cache[x].sessionKey);
}
static void cookie_cache_store(int64_t const sessionID, strarg_t const sessionKey) {
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

	sqlite3 *db = EFSRepoDBConnect(repo);
	if(!db) return NULL;

	sqlite3_stmt *select = QUERY(db,
		"SELECT user_id, password_hash\n"
		"FROM users WHERE username = ?");
	sqlite3_bind_text(select, 1, username, -1, SQLITE_STATIC);
	if(SQLITE_ROW != STEP(select)) {
		sqlite3_finalize(select);
		EFSRepoDBClose(repo, &db);
		return NULL;
	}
	int64_t const userID = sqlite3_column_int64(select, 0);
	str_t *passhash = strdup((char const *)sqlite3_column_text(select, 1));
	sqlite3_finalize(select); select = NULL;
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
	sqlite3_finalize(insert); insert = NULL;
	str_t *cookie = NULL;
	if(SQLITE_DONE == status) {
		long long const sessionID = sqlite3_last_insert_rowid(db);
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

	sqlite3 *db = EFSRepoDBConnect(repo);
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
		sqlite3_finalize(select); select = NULL;
		EFSRepoDBClose(repo, &db);
		return NULL;
	}

	int64_t const userID = sqlite3_column_int64(select, 0);
	if(userID <= 0) {
		FREE(&sessionKey);
		sqlite3_finalize(select); select = NULL;
		EFSRepoDBClose(repo, &db);
		return NULL;
	}

	strarg_t sessionHash = (strarg_t)sqlite3_column_text(select, 1);
	if(!cookie_cache_lookup(sessionID, sessionKey)) {
		if(!checkpass(sessionKey, sessionHash)) {
			FREE(&sessionKey);
			sqlite3_finalize(select); select = NULL;
			EFSRepoDBClose(repo, &db);
			return NULL;
		}
		cookie_cache_store(sessionID, sessionKey);
	}
	FREE(&sessionKey);
	sqlite3_finalize(select); select = NULL;
	EFSRepoDBClose(repo, &db);

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
	sqlite3 *db = EFSRepoDBConnect(repo);
	if(!db) return NULL;
	EFSFilterPrepare(filter, db);

	sqlite3_stmt *selectMax = QUERY(db,
		"SELECT MAX(file_id) FROM files");
	if(SQLITE_ROW != sqlite3_step(selectMax)) {
		sqlite3_finalize(selectMax); selectMax = NULL;
		EFSRepoDBClose(repo, &db);
		return NULL;
	}
	int64_t sortID = sqlite3_column_int64(selectMax, 0);
	sqlite3_finalize(selectMax); selectMax = NULL;

	sqlite3_stmt *selectHash = QUERY(db,
		"SELECT internal_hash\n"
		"FROM files WHERE file_id = ? LIMIT 1");
	URIListRef const URIs = URIListCreate();
	while(sortID > 0 && URIListGetCount(URIs) < max) {
		int64_t lastFileID = 0;
		while(URIListGetCount(URIs) < max) {
			EFSMatch const match = EFSFilterMatchFile(filter, sortID, lastFileID);
//			fprintf(stderr, "got match %lld of %lld, %lld\n", fileID, sortID, lastFileID);
			if(match.fileID < 0) break;
			sqlite3_bind_int64(selectHash, 1, match.fileID);
			if(SQLITE_ROW == sqlite3_step(selectHash)) {
				strarg_t const hash = (strarg_t)sqlite3_column_text(selectHash, 0);
				str_t *URI = EFSFormatURI(EFS_INTERNAL_ALGO, hash);
				URIListAddURI(URIs, URI, -1);
				FREE(&URI);
			}
			sqlite3_reset(selectHash);
			if(!match.more) break;
			lastFileID = match.fileID;
		}
		--sortID;
	}
	sqlite3_finalize(selectHash); selectHash = NULL;
	EFSRepoDBClose(repo, &db);
	return URIs;
}
EFSFileInfo *EFSSessionCopyFileInfo(EFSSessionRef const session, strarg_t const URI) {
	if(!session) return NULL;
	if(!URI) return NULL;
	// TODO: Check session mode.
	EFSRepoRef const repo = EFSSessionGetRepo(session);
	sqlite3 *db = EFSRepoDBConnect(repo);
	sqlite3_stmt *const select = QUERY(db,
		"SELECT f.internal_hash, f.file_type, f.file_size\n"
		"FROM files AS f\n"
		"INNER JOIN file_uris AS f2 ON (f.file_id = f2.file_id)\n"
		"WHERE f2.uri = ? LIMIT 1");
	sqlite3_bind_text(select, 1, URI, -1, SQLITE_STATIC);
	if(SQLITE_ROW != STEP(select)) {
		sqlite3_finalize(select);
		EFSRepoDBClose(repo, &db);
		return NULL;
	}
	EFSFileInfo *const info = calloc(1, sizeof(EFSFileInfo));
	info->path = EFSRepoCopyInternalPath(repo, (strarg_t)sqlite3_column_text(select, 0));
	info->type = strdup((strarg_t)sqlite3_column_text(select, 1));
	info->size = sqlite3_column_int64(select, 2);
	sqlite3_finalize(select);
	EFSRepoDBClose(repo, &db);
	return info;
}
void EFSFileInfoFree(EFSFileInfo **const infoptr) {
	EFSFileInfo *info = *infoptr;
	if(!info) return;
	FREE(&info->path);
	FREE(&info->type);
	FREE(infoptr); info = NULL;
}

