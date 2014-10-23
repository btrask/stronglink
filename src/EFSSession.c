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

static bool_t cookie_cache_lookup(uint64_t const sessionID, strarg_t const sessionKey) {
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

	EFSConnection const *conn;
	int rc;

	conn = EFSRepoDBOpen(repo);
	if(!conn) return NULL;
	MDB_txn *txn = NULL;
	rc = mdb_txn_begin(conn->env, NULL, MDB_RDONLY, &txn);
	if(MDB_SUCCESS != rc) {
		EFSRepoDBClose(repo, &conn);
		return NULL;
	}

	uint64_t const username_id = db_string_id(txn, username);
	if(!username_id) {
		EFSRepoDBClose(repo, &conn);
		return NULL;
	}

	DB_VAL(username_key, 2);
	db_bind(username_key, EFSUserIDByName);
	db_bind(username_key, username_id);
	MDB_val userID_val[1];
	rc = mdb_get(txn, MDB_MAIN_DBI, username_key, userID_val);
	uint64_t userID = 0;
	MDB_val user_val[1];
	if(MDB_SUCCESS == rc) {
		userID = db_column(userID_val, 0);
		DB_VAL(userID_key, 2);
		db_bind(userID_key, EFSUserByID);
		db_bind(userID_key, userID);
		rc = mdb_get(txn, MDB_MAIN_DBI, userID_key, user_val);
	}
	if(MDB_SUCCESS != rc) {
		mdb_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &conn);
		return NULL;
	}
	str_t *passhash = strdup(db_column_text(txn, user_val, 1));

	mdb_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(repo, &conn);

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

	conn = EFSRepoDBOpen(repo);
	if(conn) rc = mdb_txn_begin(conn->env, NULL, MDB_RDWR, &txn);
	if(!conn || MDB_SUCCESS != rc) {
		FREE(&sessionHash);
		FREE(&sessionKey);
		return NULL;
	}

	uint64_t const sessionID = db_next_id(txn, EFSSessionByID);
	uint64_t const sessionHash_id = db_string_id(txn, sessionHash);
	FREE(&sessionHash);

	DB_VAL(sessionID_key, 2);
	db_bind(sessionID_key, EFSSessionByID);
	db_bind(sessionID_key, sessionID);
	DB_VAL(session_val, 2);
	db_bind(session_val, userID);
	db_bind(session_val, sessionHash_id);
	rc = mdb_put(txn, MDB_MAIN_DBI, sessionID_key, session_val, MDB_NOOVERWRITE);
	if(MDB_SUCCESS != rc) {
		mdb_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &conn);
		FREE(&sessionKey);
		return NULL;
	}

	rc = mdb_txn_commit(txn); txn = NULL;
	EFSRepoDBClose(repo, &conn);
	if(MDB_SUCCESS != rc) {
		FREE(&sessionKey);
		return NULL;
	}

	str_t *cookie = NULL;
	if(asprintf(&cookie, "%lld:%s", sessionID, sessionKey) < 0) cookie = NULL;
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

	EFSConnection const *conn = EFSRepoDBOpen(repo);
	int rc;
	if(!conn) {
		FREE(&sessionKey);
		return NULL;
	}
	MDB_txn *txn = NULL;
	mdb_txn_begin(conn->env, NULL, MDB_RDONLY, &txn);

	DB_VAL(sessionID_key, 2);
	db_bind(sessionID_key, EFSSessionByID);
	db_bind(sessionID_key, sessionID);
	MDB_val session_val[1];
	rc = mdb_get(txn, MDB_MAIN_DBI, sessionID_key, session_val);
	if(MDB_SUCCESS != rc) {
		FREE(&sessionKey);
		mdb_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &conn);
		return NULL;
	}
	uint64_t const userID = db_column(session_val, 0);
	str_t *sessionHash = strdup(db_column_text(txn, session_val, 1));

	mdb_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(repo, &conn);

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
	EFSConnection const *conn = EFSRepoDBOpen(repo);
	if(!conn) {
		FREE(&URIs);
		return NULL;
	}

	int rc;
	MDB_txn *txn = NULL;
	rc = mdb_txn_begin(conn->env, NULL, MDB_RDONLY, &txn);
	assert(MDB_SUCCESS == rc);

	count_t count = 0;
	EFSFilterPrepare(filter, txn, conn);
	EFSFilterSeek(filter, -1, UINT64_MAX, UINT64_MAX); // TODO: Pagination
	while(count < max) {
		str_t *const URI = EFSFilterCopyNextURI(filter, -1, txn, conn);
		if(!URI) break;
		URIs[count++] = URI;
	}

	mdb_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(repo, &conn);
//	uint64_t const now = uv_hrtime();
//	fprintf(stderr, "Query in %f ms\n", (now-then) / 1000.0 / 1000.0);
	URIs[count] = NULL;
	return URIs;
}
err_t EFSSessionGetFileInfo(EFSSessionRef const session, strarg_t const URI, EFSFileInfo *const info) {
	if(!session) return -1;
	if(!URI) return -1;
	// TODO: Check session mode.
	EFSRepoRef const repo = EFSSessionGetRepo(session);
	EFSConnection const *conn = EFSRepoDBOpen(repo);
	int rc;
	MDB_txn *txn = NULL;
	rc = mdb_txn_begin(conn->env, NULL, MDB_RDONLY, &txn);
	if(MDB_SUCCESS != rc) {
		fprintf(stderr, "Transaction error %s\n", mdb_strerror(rc));
		EFSRepoDBClose(repo, &conn);
		return -1;
	}

	uint64_t const URI_id = db_string_id(txn, URI);
	if(!URI_id) {
		mdb_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &conn);
		return -1;
	}

	MDB_cursor *cursor;
	rc = mdb_cursor_open(txn, MDB_MAIN_DBI, &cursor);
	assert(!rc);
	DB_RANGE(fileIDs, 2);
	db_bind(fileIDs->min, EFSURIAndFileID);
	db_bind(fileIDs->max, EFSURIAndFileID);
	db_bind(fileIDs->min, URI_id+0);
	db_bind(fileIDs->max, URI_id+1);
	MDB_val URIAndFileID_key[1];
	rc = db_cursor_firstr(cursor, fileIDs, URIAndFileID_key, NULL, +1);
	mdb_cursor_close(cursor); cursor = NULL;
	MDB_val file_val[1];
	if(MDB_SUCCESS == rc) {
		uint64_t const fileID = db_column(URIAndFileID_key, 2);
		DB_VAL(fileID_key, 2);
		db_bind(fileID_key, EFSFileByID);
		db_bind(fileID_key, fileID);
		rc = mdb_get(txn, MDB_MAIN_DBI, fileID_key, file_val);
	}
	if(MDB_SUCCESS != rc) {
		mdb_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &conn);
		return -1;
	}

	if(info) {
		strarg_t const internalHash = db_column_text(txn, file_val, 0);
		strarg_t const type = db_column_text(txn, file_val, 1);
		info->hash = strdup(internalHash);
		info->path = EFSRepoCopyInternalPath(repo, internalHash);
		info->type = strdup(type);
		info->size = db_column(file_val, 2);
	}

	mdb_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(repo, &conn);
	return 0;
}
void EFSFileInfoCleanup(EFSFileInfo *const info) {
	if(!info) return;
	FREE(&info->hash);
	FREE(&info->path);
	FREE(&info->type);
}

