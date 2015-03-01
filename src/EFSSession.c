#define _GNU_SOURCE
#include "async/async.h"
#include "bcrypt.h"
#include "EarthFS.h"

struct EFSSession {
	EFSRepoRef repo;
	uint64_t userID;
};

EFSSessionRef EFSRepoCreateSession(EFSRepoRef const repo, strarg_t const cookie) {
	uint64_t userID = 0;
	int rc = EFSRepoCookieAuth(repo, cookie, &userID);
	if(0 != rc) {
		return NULL; // TODO
	}
	if(!userID) {
		assert(0); // TODO
		return NULL;
	}
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
		if(!info->hash || !info->path || !info->type) {
			EFSFileInfoCleanup(info);
			return DB_ENOMEM;
		}
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

