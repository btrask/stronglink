#define _GNU_SOURCE /* asprintf() */
#include <assert.h>
#include "async.h"
#include "EarthFS.h"

struct EFSRepo {
	str_t *dir;
	str_t *dataDir;
	str_t *tempDir;
	str_t *cacheDir;
	str_t *DBPath;

	EFSConnection conn[1];

	async_mutex_t *sub_mutex;
	async_cond_t *sub_cond;
	uint64_t sub_latest;

	EFSPullRef *pulls;
	count_t pull_count;
	count_t pull_size;
};

static void debug_data(EFSConnection const *const conn);

EFSRepoRef EFSRepoCreate(strarg_t const dir) {
	assertf(dir, "EFSRepo dir required");
	EFSRepoRef repo = calloc(1, sizeof(struct EFSRepo));
	if(!repo) return NULL;
	repo->dir = strdup(dir);
	// TODO: If asprintf() fails, the string pointer is undefined.
	if(
		asprintf(&repo->dataDir, "%s/data", dir) < 0 ||
		asprintf(&repo->tempDir, "%s/tmp", dir) < 0 ||
		asprintf(&repo->cacheDir, "%s/cache", dir) < 0 ||
		asprintf(&repo->DBPath, "%s/efs.db", dir) < 0
	) {
		EFSRepoFree(&repo);
		return NULL;
	}


	EFSConnection *const conn = repo->conn;
	mdb_env_create(&conn->env);
	mdb_env_set_mapsize(conn->env, 1024 * 1024 * 256);
	mdb_env_set_maxreaders(conn->env, 126); // Default
	mdb_env_set_maxdbs(conn->env, 32);
	mdb_env_open(conn->env, repo->DBPath, MDB_NOSUBDIR, 0600);
	MDB_txn *txn = NULL;
	mdb_txn_begin(conn->env, NULL, MDB_RDWR, &txn);


	// TODO: Separate "schema open" function.
	mdb_dbi_open(txn, "schema", MDB_CREATE, &conn->schema->schema);
	mdb_dbi_open(txn, "stringByID", MDB_CREATE, &conn->schema->stringByID);
	mdb_dbi_open(txn, "stringIDByValue", MDB_CREATE, &conn->schema->stringIDByValue);
	mdb_dbi_open(txn, "stringIDByHash", MDB_CREATE, &conn->schema->stringIDByHash);


	mdb_dbi_open(txn, "userByID", MDB_CREATE, &conn->userByID);
	mdb_dbi_open(txn, "userIDByName", MDB_CREATE, &conn->userIDByName);
	mdb_dbi_open(txn, "sessionByID", MDB_CREATE, &conn->sessionByID);
	mdb_dbi_open(txn, "pullByID", MDB_CREATE, &conn->pullByID);

	mdb_dbi_open(txn, "fileByID", MDB_CREATE, &conn->fileByID);
	mdb_dbi_open(txn, "fileIDByInfo", MDB_CREATE, &conn->fileIDByInfo);
	mdb_dbi_open(txn, "fileIDByType", MDB_CREATE | MDB_DUPSORT, &conn->fileIDByType);

	mdb_dbi_open(txn, "URIByFileID", MDB_CREATE | MDB_DUPSORT, &conn->URIByFileID);
	mdb_dbi_open(txn, "fileIDByURI", MDB_CREATE | MDB_DUPSORT, &conn->fileIDByURI);

	mdb_dbi_open(txn, "metaFileByID", MDB_CREATE, &conn->metaFileByID);
	mdb_dbi_open(txn, "metaFileIDByFileID", MDB_CREATE | MDB_DUPSORT, &conn->metaFileIDByFileID);
	mdb_dbi_open(txn, "metaFileIDByTargetURI", MDB_CREATE | MDB_DUPSORT, &conn->metaFileIDByTargetURI);
	mdb_dbi_open(txn, "metaFileIDByMetadata", MDB_CREATE | MDB_DUPSORT, &conn->metaFileIDByMetadata);
	mdb_dbi_open(txn, "metaFileIDByFulltext", MDB_CREATE | MDB_DUPSORT, &conn->metaFileIDByFulltext);

	mdb_dbi_open(txn, "valueByMetaFileIDField", MDB_CREATE | MDB_DUPSORT, &conn->valueByMetaFileIDField);

	mdb_txn_commit(txn);


	debug_data(conn);


	repo->sub_mutex = async_mutex_create();
	repo->sub_cond = async_cond_create();
	if(!repo->sub_mutex || !repo->sub_cond) {
		EFSRepoFree(&repo);
		return NULL;
	}
	return repo;
}
void EFSRepoFree(EFSRepoRef *const repoptr) {
	EFSRepoRef repo = *repoptr;
	if(!repo) return;

	FREE(&repo->dir);
	FREE(&repo->dataDir);
	FREE(&repo->tempDir);
	FREE(&repo->cacheDir);
	FREE(&repo->DBPath);

	EFSConnection *const conn = repo->conn;
	mdb_env_close(conn->env); conn->env = NULL;
	memset(conn, 0, sizeof(*conn));

	async_mutex_free(repo->sub_mutex); repo->sub_mutex = NULL;
	async_cond_free(repo->sub_cond); repo->sub_cond = NULL;
	repo->sub_latest = 0;

	for(index_t i = 0; i < repo->pull_count; ++i) {
		EFSPullFree(&repo->pulls[i]);
	}
	FREE(&repo->pulls);
	repo->pull_count = 0;
	repo->pull_size = 0;

	assert_zeroed(repo, 1);
	FREE(repoptr); repo = NULL;
}

strarg_t EFSRepoGetDir(EFSRepoRef const repo) {
	if(!repo) return NULL;
	return repo->dir;
}
strarg_t EFSRepoGetDataDir(EFSRepoRef const repo) {
	if(!repo) return NULL;
	return repo->dataDir;
}
str_t *EFSRepoCopyInternalPath(EFSRepoRef const repo, strarg_t const internalHash) {
	if(!repo) return NULL;
	str_t *str;
	if(asprintf(&str, "%s/%.2s/%s", repo->dataDir, internalHash, internalHash) < 0) return NULL;
	return str;
}
strarg_t EFSRepoGetTempDir(EFSRepoRef const repo) {
	if(!repo) return NULL;
	return repo->tempDir;
}
str_t *EFSRepoCopyTempPath(EFSRepoRef const repo) {
	if(!repo) return NULL;
	return async_fs_tempnam(repo->tempDir, "efs");
}
strarg_t EFSRepoGetCacheDir(EFSRepoRef const repo) {
	if(!repo) return NULL;
	return repo->cacheDir;
}

EFSConnection const *EFSRepoDBOpen(EFSRepoRef const repo) {
	assert(repo);
	async_pool_enter(NULL);
	return repo->conn;
}
void EFSRepoDBClose(EFSRepoRef const repo, EFSConnection const **const connptr) {
	assert(repo);
	async_pool_leave(NULL);
	*connptr = NULL;
}

void EFSRepoSubmissionEmit(EFSRepoRef const repo, uint64_t const sortID) {
	assert(repo);
	async_mutex_lock(repo->sub_mutex);
	if(sortID > repo->sub_latest) {
		repo->sub_latest = sortID;
		async_cond_broadcast(repo->sub_cond);
	}
	async_mutex_unlock(repo->sub_mutex);
}
bool_t EFSRepoSubmissionWait(EFSRepoRef const repo, uint64_t const sortID, uint64_t const future) {
	assert(repo);
	async_mutex_lock(repo->sub_mutex);
	while(repo->sub_latest <= sortID && async_cond_timedwait(repo->sub_cond, repo->sub_mutex, future) >= 0);
	bool_t const res = repo->sub_latest > sortID;
	async_mutex_unlock(repo->sub_mutex);
	return res;
}

// TODO: Separate methods for loading and starting?
void EFSRepoStartPulls(EFSRepoRef const repo) {
	if(!repo) return;
	EFSConnection const *conn = EFSRepoDBOpen(repo);
	int rc;
	MDB_txn *txn = NULL;
	rc = mdb_txn_begin(conn->env, NULL, MDB_RDONLY, &txn);
	assert(MDB_SUCCESS == rc);

	MDB_cursor *cur = NULL;
	rc = mdb_cursor_open(txn, conn->pullByID, &cur);
	assert(MDB_SUCCESS == rc);

	MDB_val pullID_val[1];
	MDB_val pull_val[1];
	rc = mdb_cursor_get(cur, pullID_val, pull_val, MDB_FIRST);
	for(; MDB_SUCCESS == rc; rc = mdb_cursor_get(cur, pullID_val, pull_val, MDB_NEXT)) {
		uint64_t const pullID = db_column(pullID_val, 0);
		uint64_t const userID = db_column(pull_val, 0);
		strarg_t const host = db_column_text(txn, conn->schema, pull_val, 1);
		strarg_t const username = db_column_text(txn, conn->schema, pull_val, 2);
		strarg_t const password = db_column_text(txn, conn->schema, pull_val, 3);
		strarg_t const cookie = db_column_text(txn, conn->schema, pull_val, 4);
		strarg_t const query = db_column_text(txn, conn->schema, pull_val, 5);

		EFSPullRef const pull = EFSRepoCreatePull(repo, pullID, userID, host, username, password, cookie, query);
		if(repo->pull_count+1 > repo->pull_size) {
			repo->pull_size = (repo->pull_count+1) * 2;
			repo->pulls = realloc(repo->pulls, sizeof(EFSPullRef) * repo->pull_size);
			assert(repo->pulls); // TODO: Handle error
		}
		repo->pulls[repo->pull_count++] = pull;
	}

	mdb_cursor_close(cur); cur = NULL;
	mdb_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(repo, &conn);

	for(index_t i = 0; i < repo->pull_count; ++i) {
		EFSPullStart(repo->pulls[i]);
	}
}

static void debug_data(EFSConnection const *const conn) {
	MDB_txn *txn = NULL;
	mdb_txn_begin(conn->env, NULL, MDB_RDWR, &txn);

	uint64_t const userID = 1;
	uint64_t const username_id = db_string_id(txn, conn->schema, "ben");
	uint64_t const passhash_id = db_string_id(txn, conn->schema, "$2a$08$lhAQjgGPuwvtErV.aK.MGO1T2W0UhN1r4IngmF5FvY0LM826aF8ye");
	assert(username_id);
	assert(passhash_id);

	DB_VAL(userID_val, 1);
	db_bind(userID_val, userID);
	DB_VAL(user_val, 3);
	db_bind(user_val, username_id);
	db_bind(user_val, passhash_id); // passhash
	db_bind(user_val, passhash_id); // token
	mdb_put(txn, conn->userByID, userID_val, user_val, MDB_NOOVERWRITE);

	DB_VAL(username_val, 1);
	db_bind(username_val, username_id);
	mdb_put(txn, conn->userIDByName, username_val, userID_val, MDB_NOOVERWRITE);

	DB_VAL(pullID_val, 1);
	db_bind(pullID_val, 1);

	uint64_t const host_id = db_string_id(txn, conn->schema, "localhost:8009");
	uint64_t const remote_username_id = db_string_id(txn, conn->schema, "ben");
	uint64_t const remote_password_id = db_string_id(txn, conn->schema, "testing");
	uint64_t const cookie_id = db_string_id(txn, conn->schema, "s=1:not-very-random");
	uint64_t const query_id = db_string_id(txn, conn->schema, "");
	assert(host_id);
	assert(remote_username_id);
	assert(query_id);

	DB_VAL(pull_val, 6);
	db_bind(pull_val, userID);
	db_bind(pull_val, host_id);
	db_bind(pull_val, remote_username_id);
	db_bind(pull_val, remote_password_id);
	db_bind(pull_val, cookie_id);
	db_bind(pull_val, query_id);

	mdb_put(txn, conn->pullByID, pullID_val, pull_val, MDB_NOOVERWRITE);

	mdb_txn_commit(txn); txn = NULL;
}

