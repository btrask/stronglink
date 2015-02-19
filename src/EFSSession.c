#define _GNU_SOURCE
#include "async/async.h"
#include "bcrypt.h"
#include "EarthFS.h"

#define COOKIE_CACHE_SIZE 1000

struct cached_cookie {
	uint64_t sessionID;
	str_t *sessionKey;
	uint64_t atime; // TODO: Prune old entries.
};
static struct cached_cookie cookie_cache[COOKIE_CACHE_SIZE] = {};

static bool cookie_cache_lookup(uint64_t const sessionID, strarg_t const sessionKey) {
	assert(!async_pool_get_worker());
	if(sessionID <= 0 || !sessionKey) return false;
	index_t const x = sessionID+sessionKey[0] % COOKIE_CACHE_SIZE;
	if(cookie_cache[x].sessionID != sessionID) return false;
	if(!cookie_cache[x].sessionKey) return false;
	return 0 == passcmp(sessionKey, cookie_cache[x].sessionKey);
}
static void cookie_cache_store(uint64_t const sessionID, strarg_t const sessionKey) {
	assert(!async_pool_get_worker());
	if(sessionID <= 0 || !sessionKey) return;
	index_t const x = sessionID+sessionKey[0] % COOKIE_CACHE_SIZE;
	FREE(&cookie_cache[x].sessionKey);
	cookie_cache[x].sessionID = sessionID;
	cookie_cache[x].sessionKey = strdup(sessionKey);
	cookie_cache[x].atime = uv_now(loop);
}

struct EFSSession {
	EFSRepoRef repo;
	uint64_t userID;
};

str_t *EFSRepoCreateCookie(EFSRepoRef const repo, strarg_t const username, strarg_t const password) {
	if(!repo) return NULL;
	if(!username) return NULL;
	if(!password) return NULL;

	DB_env *db = NULL;
	int rc = EFSRepoDBOpen(repo, &db);
	if(rc < 0) return NULL;
	DB_txn *txn = NULL;
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(DB_SUCCESS != rc) {
		EFSRepoDBClose(repo, &db);
		return NULL;
	}

	DB_VAL(username_key, DB_VARINT_MAX + DB_INLINE_MAX);
	db_bind_uint64(username_key, EFSUserIDByName);
	db_bind_string(txn, username_key, username);
	DB_val userID_val[1];
	rc = db_get(txn, username_key, userID_val);
	uint64_t userID = 0;
	DB_val user_val[1];
	if(DB_SUCCESS == rc) {
		userID = db_read_uint64(userID_val);
		DB_VAL(userID_key, DB_VARINT_MAX + DB_VARINT_MAX);
		db_bind_uint64(userID_key, EFSUserByID);
		db_bind_uint64(userID_key, userID);
		rc = db_get(txn, userID_key, user_val);
	}
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &db);
		return NULL;
	}
	strarg_t const u = db_read_string(txn, user_val);
	assert(0 == strcmp(username, u));
	str_t *passhash = strdup(db_read_string(txn, user_val));

	db_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(repo, &db);

	if(userID <= 0 || !checkpass(password, passhash)) {
		FREE(&passhash);
		return NULL;
	}
	FREE(&passhash);

	str_t *sessionKey = strdup("not-very-random"); // TODO: Generate
	if(!sessionKey) {
		return NULL;
	}
	str_t *sessionHash = hashpass(sessionKey);
	if(!sessionHash) {
		FREE(&sessionHash);
		FREE(&sessionKey);
		return NULL;
	}

	rc = EFSRepoDBOpen(repo, &db);
	if(rc >= 0) rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
	if(DB_SUCCESS != rc) {
		FREE(&sessionHash);
		FREE(&sessionKey);
		return NULL;
	}

	uint64_t const sessionID = db_next_id(txn, EFSSessionByID);
	DB_VAL(sessionID_key, DB_VARINT_MAX + DB_VARINT_MAX);
	db_bind_uint64(sessionID_key, EFSSessionByID);
	db_bind_uint64(sessionID_key, sessionID);
	DB_VAL(session_val, DB_VARINT_MAX + DB_INLINE_MAX);
	db_bind_uint64(session_val, userID);
	db_bind_string(txn, session_val, sessionHash);
	FREE(&sessionHash);
	rc = db_put(txn, sessionID_key, session_val, DB_NOOVERWRITE_FAST);
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &db);
		FREE(&sessionKey);
		return NULL;
	}

	rc = db_txn_commit(txn); txn = NULL;
	EFSRepoDBClose(repo, &db);
	if(DB_SUCCESS != rc) {
		FREE(&sessionKey);
		return NULL;
	}

	str_t *cookie = NULL;
	if(asprintf(&cookie, "%llu:%s", (unsigned long long)sessionID, sessionKey) < 0) cookie = NULL;
	FREE(&sessionKey);
	return cookie;
}
EFSSessionRef EFSRepoCreateSession(EFSRepoRef const repo, strarg_t const cookie) {
	if(!repo) return NULL;
	if(!cookie) return NULL;

	unsigned long long sessionID = -1;
	str_t *sessionKey = calloc(strlen(cookie)+1, 1);
	if(!sessionKey) return NULL;
	sscanf(cookie, "s=%llu:%s", &sessionID, sessionKey);
	if(!sessionID || '\0' == sessionKey[0]) {
		FREE(&sessionKey);
		return NULL;
	}

	DB_env *db = NULL;
	int rc = EFSRepoDBOpen(repo, &db);
	if(rc < 0) {
		FREE(&sessionKey);
		return NULL;
	}
	DB_txn *txn = NULL;
	db_txn_begin(db, NULL, DB_RDONLY, &txn);

	DB_VAL(sessionID_key, DB_VARINT_MAX + DB_VARINT_MAX);
	db_bind_uint64(sessionID_key, EFSSessionByID);
	db_bind_uint64(sessionID_key, sessionID);
	DB_val session_val[1];
	rc = db_get(txn, sessionID_key, session_val);
	if(DB_SUCCESS != rc) {
		FREE(&sessionKey);
		db_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &db);
		return NULL;
	}
	uint64_t const userID = db_read_uint64(session_val);
	str_t *sessionHash = strdup(db_read_string(txn, session_val));

	db_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(repo, &db);

	if(!userID) {
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
EFSSessionRef EFSRepoCreateSessionInternal(EFSRepoRef const repo, uint64_t const userID) {
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
	session->userID = 0;
	assert_zeroed(session, 1);
	FREE(sessionptr); session = NULL;
}
EFSRepoRef EFSSessionGetRepo(EFSSessionRef const session) {
	if(!session) return NULL;
	return session->repo;
}
uint64_t EFSSessionGetUserID(EFSSessionRef const session) {
	if(!session) return -1;
	return session->userID;
}

str_t **EFSSessionCopyFilteredURIs(EFSSessionRef const session, EFSFilterRef const filter, count_t const max) { // TODO: Sort order, pagination.
	if(!session) return NULL;
	// TODO: Check session mode.

	str_t **URIs = malloc(sizeof(str_t *) * (max+1));
	if(!URIs) return NULL;

//	uint64_t const then = uv_hrtime();
	EFSRepoRef const repo = EFSSessionGetRepo(session);
	DB_env *db = NULL;
	int rc = EFSRepoDBOpen(repo, &db);
	if(rc < 0) {
		FREE(&URIs);
		return NULL;
	}

	DB_txn *txn = NULL;
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	assert(DB_SUCCESS == rc);

	count_t count = 0;
	EFSFilterPrepare(filter, txn);
	EFSFilterSeek(filter, -1, UINT64_MAX, UINT64_MAX); // TODO: Pagination
	while(count < max) {
		str_t *const URI = EFSFilterCopyNextURI(filter, -1, txn);
		if(!URI) break;
		URIs[count++] = URI;
	}

	db_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(repo, &db);
//	uint64_t const now = uv_hrtime();
//	fprintf(stderr, "Query in %f ms\n", (now-then) / 1000.0 / 1000.0);
	URIs[count] = NULL;
	return URIs;
}
int EFSSessionGetFileInfo(EFSSessionRef const session, strarg_t const URI, EFSFileInfo *const info) {
	if(!session) return DB_EINVAL;
	if(!URI) return DB_EINVAL;
	// TODO: Check session mode.
	EFSRepoRef const repo = EFSSessionGetRepo(session);
	DB_env *db = NULL;
	int rc = EFSRepoDBOpen(repo, &db);
	if(rc < 0) {
		fprintf(stderr, "Database error %s\n", uv_strerror(rc));
		return rc;
	}
	DB_txn *txn = NULL;
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(DB_SUCCESS != rc) {
		fprintf(stderr, "Transaction error %s\n", db_strerror(rc));
		EFSRepoDBClose(repo, &db);
		return rc;
	}

	DB_cursor *cursor;
	rc = db_txn_cursor(txn, &cursor);
	assert(!rc);
	DB_RANGE(fileIDs, DB_VARINT_MAX + DB_INLINE_MAX);
	db_bind_uint64(fileIDs->min, EFSURIAndFileID);
	db_bind_string(txn, fileIDs->min, URI);
	db_range_genmax(fileIDs);
	DB_val URIAndFileID_key[1];
	rc = db_cursor_firstr(cursor, fileIDs, URIAndFileID_key, NULL, +1);
	DB_val file_val[1];
	if(DB_SUCCESS == rc) {
		uint64_t const table = db_read_uint64(URIAndFileID_key);
		assert(EFSURIAndFileID == table);
		strarg_t const URI2 = db_read_string(txn, URIAndFileID_key);
		assert(0 == strcmp(URI, URI2));
		if(info) {
			uint64_t const fileID = db_read_uint64(URIAndFileID_key);
			DB_VAL(fileID_key, DB_VARINT_MAX + DB_VARINT_MAX);
			db_bind_uint64(fileID_key, EFSFileByID);
			db_bind_uint64(fileID_key, fileID);
			rc = db_get(txn, fileID_key, file_val);
		}
	}
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &db);
		return rc;
	}

	if(info) {
		strarg_t const internalHash = db_read_string(txn, file_val);
		strarg_t const type = db_read_string(txn, file_val);
		uint64_t const size = db_read_uint64(file_val);
		info->hash = strdup(internalHash);
		info->path = EFSRepoCopyInternalPath(repo, internalHash);
		info->type = strdup(type);
		info->size = size;
	}

	db_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(repo, &db);
	return DB_SUCCESS;
}
void EFSFileInfoCleanup(EFSFileInfo *const info) {
	if(!info) return;
	FREE(&info->hash);
	FREE(&info->path);
	FREE(&info->type);
}

