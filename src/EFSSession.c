#include "util/bcrypt.h"
#include "StrongLink.h"
#include "EFSDB.h"

struct EFSSession {
	EFSRepoRef repo;
	uint64_t userID;
	int autherr;
};

EFSSessionRef EFSRepoCreateSession(EFSRepoRef const repo, strarg_t const cookie) {
	uint64_t userID;
	int rc = EFSRepoCookieAuth(repo, cookie, &userID);
	if(rc < 0) userID = 0; // Public access
	EFSSessionRef session = EFSRepoCreateSessionInternal(repo, userID);
	if(session) {
		session->autherr = rc;
	}
	return session;
}
EFSSessionRef EFSRepoCreateSessionInternal(EFSRepoRef const repo, uint64_t const userID) {
	EFSSessionRef const session = calloc(1, sizeof(struct EFSSession));
	if(!session) return NULL;
	session->repo = repo;
	session->userID = userID;
	session->autherr = 0;
	return session;
}
void EFSSessionFree(EFSSessionRef *const sessionptr) {
	EFSSessionRef session = *sessionptr;
	if(!session) return;
	session->repo = NULL;
	session->userID = 0;
	session->autherr = 0;
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
int EFSSessionGetAuthError(EFSSessionRef const session) {
	if(!session) return DB_EINVAL;
	return session->autherr;
}

str_t **EFSSessionCopyFilteredURIs(EFSSessionRef const session, EFSFilterRef const filter, count_t const max) { // TODO: Sort order, pagination.
	if(!session) return NULL;
	// TODO: Check session mode.

	str_t **URIs = malloc(sizeof(str_t *) * (max+1));
	if(!URIs) return NULL;

//	uint64_t const then = uv_hrtime();
	EFSRepoRef const repo = EFSSessionGetRepo(session);
	DB_env *db = NULL;
	EFSRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
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
	EFSRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(DB_SUCCESS != rc) {
		fprintf(stderr, "Transaction error %s\n", db_strerror(rc));
		EFSRepoDBClose(repo, &db);
		return rc;
	}

	DB_cursor *cursor;
	rc = db_txn_cursor(txn, &cursor);
	assert(!rc);

	DB_range fileIDs[1];
	EFSURIAndFileIDRange1(fileIDs, txn, URI);
	DB_val URIAndFileID_key[1];
	rc = db_cursor_firstr(cursor, fileIDs, URIAndFileID_key, NULL, +1);
	DB_val file_val[1];
	if(DB_SUCCESS == rc) {
		strarg_t URI2;
		uint64_t fileID;
		EFSURIAndFileIDKeyUnpack(URIAndFileID_key, txn, &URI2, &fileID);
		assert(0 == strcmp(URI, URI2));
		if(info) {
			DB_val fileID_key[1];
			EFSFileByIDKeyPack(fileID_key, txn, fileID);
			rc = db_get(txn, fileID_key, file_val);
		}
	}
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &db);
		return rc;
	}

	if(info) {
		strarg_t const internalHash = db_read_string(file_val, txn);
		strarg_t const type = db_read_string(file_val, txn);
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


int EFSSessionGetValueForField(EFSSessionRef const session, str_t value[], size_t const max, strarg_t const fileURI, strarg_t const field) {
	if(!session) return UV_EINVAL;
	if(!field) return UV_EINVAL;
	if(max) value[0] = '\0';
	int rc = DB_SUCCESS;
	DB_cursor *metafiles = NULL;
	DB_cursor *values = NULL;

	DB_env *db = NULL;
	EFSRepoDBOpen(session->repo, &db);
	DB_txn *txn = NULL;
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(DB_SUCCESS != rc) goto done;

	rc = db_cursor_open(txn, &metafiles);
	if(DB_SUCCESS != rc) goto done;
	rc = db_cursor_open(txn, &values);
	if(DB_SUCCESS != rc) goto done;

	DB_range metaFileIDs[1];
	EFSTargetURIAndMetaFileIDRange1(metaFileIDs, txn, fileURI);
	DB_val metaFileID_key[1];
	rc = db_cursor_firstr(metafiles, metaFileIDs, metaFileID_key, NULL, +1);
	if(DB_SUCCESS != rc && DB_NOTFOUND != rc) goto done;
	for(; DB_SUCCESS == rc; rc = db_cursor_nextr(metafiles, metaFileIDs, metaFileID_key, NULL, +1)) {
		strarg_t u;
		uint64_t metaFileID;
		EFSTargetURIAndMetaFileIDKeyUnpack(metaFileID_key, txn, &u, &metaFileID);
		assert(0 == strcmp(fileURI, u));
		DB_range vrange[1];
		EFSMetaFileIDFieldAndValueRange2(vrange, txn, metaFileID, field);
		DB_val value_val[1];
		rc = db_cursor_firstr(values, vrange, value_val, NULL, +1);
		if(DB_SUCCESS != rc && DB_NOTFOUND != rc) goto done;
		for(; DB_SUCCESS == rc; rc = db_cursor_nextr(values, vrange, value_val, NULL, +1)) {
			uint64_t m;
			strarg_t f, v;
			EFSMetaFileIDFieldAndValueKeyUnpack(value_val, txn, &m, &f, &v);
			assert(metaFileID == m);
			assert(0 == strcmp(field, f));
			if(!v) continue;
			if(0 == strcmp("", v)) continue;
			size_t const len = strlen(v);
			memcpy(value, v, MIN(len, max-1));
			value[MIN(len, max-1)] = '\0';
			goto done;
		}
	}

done:
	db_cursor_close(values); values = NULL;
	db_cursor_close(metafiles); metafiles = NULL;

	db_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(session->repo, &db);
	return rc;
}

