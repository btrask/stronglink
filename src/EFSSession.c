#define _GNU_SOURCE
#include "async.h"
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
	assert(!async_worker_get_current());
	if(sessionID <= 0 || !sessionKey) return false;
	index_t const x = sessionID+sessionKey[0] % COOKIE_CACHE_SIZE;
	if(cookie_cache[x].sessionID != sessionID) return false;
	if(!cookie_cache[x].sessionKey) return false;
	return 0 == passcmp(sessionKey, cookie_cache[x].sessionKey);
}
static void cookie_cache_store(uint64_t const sessionID, strarg_t const sessionKey) {
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

	uint64_t const username_id = db_string_id(txn, conn->schema, username);
	if(!username_id) {
		EFSRepoDBClose(repo, &conn);
		return NULL;
	}

	DB_VAL(username_val, 1);
	db_bind(username_val, 0, username_id);
	MDB_val userID_val[1];
	MDB_val user_val[1];
	rc = mdb_get(txn, conn->userIDByName, username_val, userID_val);
	if(MDB_SUCCESS == rc) rc = mdb_get(txn, conn->userByID, userID_val, user_val);
	if(MDB_SUCCESS != rc) {
		mdb_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &conn);
		return NULL;
	}
	uint64_t const userID = db_column(userID_val, 0);
	str_t *passhash = strdup(db_column_text(txn, conn->schema, user_val, 1));

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

	uint64_t const sessionID = db_last_id(txn, conn->sessionByID)+1;
	uint64_t const sessionHash_id = db_string_id(txn, conn->schema, sessionHash);
	FREE(&sessionHash);

	DB_VAL(sessionID_val, 1);
	db_bind(sessionID_val, 0, sessionID);

	DB_VAL(session_val, 2);
	db_bind(session_val, 0, userID);
	db_bind(session_val, 1, sessionHash_id);
	rc = mdb_put(txn, conn->sessionByID, sessionID_val, session_val, MDB_NOOVERWRITE | MDB_APPEND);
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

	long long sessionID = -1;
	str_t *sessionKey = calloc(strlen(cookie)+1, 1);
	if(!sessionKey) return NULL;
	sscanf(cookie, "s=%lld:%s", &sessionID, sessionKey);
	if(sessionID <= 0 || '\0' == sessionKey[0]) {
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

	DB_VAL(sessionID_val, 1);
	db_bind(sessionID_val, 0, sessionID);
	MDB_val session_val[1];
	rc = mdb_get(txn, conn->sessionByID, sessionID_val, session_val);
	if(MDB_SUCCESS != rc) {
		FREE(&sessionKey);
		mdb_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &conn);
		return NULL;
	}
	uint64_t const userID = db_column(session_val, 0);
	str_t *sessionHash = strdup(db_column_text(txn, conn->schema, session_val, 1));

	mdb_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(repo, &conn);

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
	session->userID = -1;
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

URIListRef EFSSessionCreateFilteredURIList(EFSSessionRef const session, EFSFilterRef const filter, count_t const max) { // TODO: Sort order, pagination.
	if(!session) return NULL;
	// TODO: Check session mode.
	EFSRepoRef const repo = EFSSessionGetRepo(session);
	EFSConnection const *conn = EFSRepoDBOpen(repo);
	if(!conn) return NULL;

	URIListRef const URIs = URIListCreate(); // TODO: Just preallocate a regular array, since we know the maximum size. Get rid of URILists all together.

	// TODO: Pagination
	uint64_t const initialSortID = UINT64_MAX;
	uint64_t const initialFileID = UINT64_MAX;
	int rc;

	MDB_txn *txn = NULL;
	rc = mdb_txn_begin(conn->env, NULL, MDB_RDONLY, &txn);
	assert(MDB_SUCCESS == rc);

	EFSFilterPrepare(filter, conn, txn);

	MDB_cursor *sortIDs = NULL;
	rc = mdb_cursor_open(txn, conn->metaFileIDByFileID, &sortIDs);
	assert(MDB_SUCCESS == rc);
	// TODO: EVERY file must have an associated meta-file, pointing at itself?

	MDB_cursor *fileIDs = NULL;
	rc = mdb_cursor_open(txn, conn->fileIDByURI, &fileIDs);
	assert(MDB_SUCCESS == rc);

	DB_VAL(sortID_val, 1);
	db_bind(sortID_val, 0, initialSortID);
	MDB_val metaFileID_val[1];
	rc = mdb_cursor_get(sortIDs, sortID_val, metaFileID_val, MDB_SET_RANGE);
	if(MDB_SUCCESS == rc) {
		// We want MDB_SET_RANGE_LT, but it doesn't exist.
		rc = mdb_cursor_get(sortIDs, sortID_val, metaFileID_val, MDB_PREV);
	} else if(MDB_NOTFOUND == rc) {
		rc = mdb_cursor_get(sortIDs, sortID_val, metaFileID_val, MDB_LAST);
	} else assert(0);
	for(; MDB_SUCCESS == rc; rc = mdb_cursor_get(sortIDs, sortID_val, metaFileID_val, MDB_PREV)) {
		MDB_val metaFile_val[1];
		rc = mdb_get(txn, conn->metaFileByID, metaFileID_val, metaFile_val);
		assert(MDB_SUCCESS == rc);
		uint64_t const metaFileID = db_column(metaFileID_val, 0);
		uint64_t const targetURI_id = db_column(metaFile_val, 1);

		DB_VAL(URI_val, 1);
		db_bind(URI_val, 0, targetURI_id);
		MDB_val fileID_val[1];
		rc = mdb_cursor_get(fileIDs, URI_val, fileID_val, MDB_SET);
		assert(MDB_SUCCESS == rc || MDB_NOTFOUND == rc);
		for(; MDB_SUCCESS == rc; rc = mdb_cursor_get(fileIDs, URI_val, fileID_val, MDB_PREV_DUP)) {
			assert(targetURI_id == db_column(URI_val, 0)); // Check for bug with MDB_PREV_DUP.

			uint64_t const sortID = db_column(sortID_val, 0);
			uint64_t const fileID = db_column(fileID_val, 0);

			if(sortID == initialSortID && fileID >= initialFileID) continue;

			uint64_t const age = EFSFilterMatchAge(filter, fileID, sortID, conn, txn);
//			fprintf(stderr, "{%llu, %llu, %llu} -> %llu\n", sortID, metaFileID, fileID, age);
			if(age != sortID) continue;

			MDB_val file_val[1];
			rc = mdb_get(txn, conn->fileByID, fileID_val, file_val);
			if(MDB_NOTFOUND == rc) continue;
			assert(MDB_SUCCESS == rc);

			strarg_t const hash = db_column_text(txn, conn->schema, file_val, 0);
			assert(hash);

			str_t *URI = EFSFormatURI(EFS_INTERNAL_ALGO, hash);
			URIListAddURI(URIs, URI, -1);
			FREE(&URI);
			if(URIListGetCount(URIs) >= max) break;

		}

		if(URIListGetCount(URIs) >= max) break;		

	}

	mdb_cursor_close(sortIDs); sortIDs = NULL;
	mdb_cursor_close(fileIDs); fileIDs = NULL;
	mdb_txn_abort(txn); txn = NULL;

	EFSRepoDBClose(repo, &conn);
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

	uint64_t const URI_id = db_string_id(txn, conn->schema, URI);
	if(!URI_id) {
		mdb_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &conn);
		return -1;
	}

	DB_VAL(URI_val, 1);
	db_bind(URI_val, 0, URI_id);
	MDB_val fileID_val[1];
	MDB_val file_val[1];
	rc = mdb_get(txn, conn->fileIDByURI, URI_val, fileID_val);
	if(MDB_SUCCESS == rc) rc = mdb_get(txn, conn->fileByID, fileID_val, file_val);
	if(MDB_SUCCESS != rc) {
		mdb_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &conn);
		return -1;
	}

	if(info) {
		strarg_t const internalHash = db_column_text(txn, conn->schema, file_val, 0);
		strarg_t const type = db_column_text(txn, conn->schema, file_val, 1);
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

