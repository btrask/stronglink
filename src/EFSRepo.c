#define _GNU_SOURCE /* asprintf() */
#include <assert.h>
#include "async.h"
#include "EarthFS.h"

#define CONNECTION_COUNT 4

struct EFSRepo {
	str_t *dir;
	str_t *dataDir;
	str_t *tempDir;
	str_t *cacheDir;
	str_t *DBPath;

	EFSConnection conn;
	async_worker_t *workers[CONNECTION_COUNT];
	count_t conn_count;
	async_sem_t *conn_sem;

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


	EFSConnection *const conn = &repo->conn;
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
	mdb_dbi_open(txn, "fileIDByURI", MDB_CREATE, &conn->fileIDByURI);
	mdb_dbi_open(txn, "fileIDByType", MDB_CREATE, &conn->fileIDByType);

	mdb_dbi_open(txn, "metaFileByID", MDB_CREATE, &conn->metaFileByID);
//	mdb_dbi_open(txn, "targetFileIDByMetaFileID", MDB_CREATE, &conn->targetFileIDByMetaFileID);
	mdb_dbi_open(txn, "metadataByMetaFileID", MDB_CREATE | MDB_DUPSORT, &conn->metadataByMetaFileID);
	mdb_dbi_open(txn, "metaFileIDByMetadata", MDB_CREATE | MDB_DUPSORT, &conn->metaFileIDByMetadata);

	mdb_dbi_open(txn, "metaFileIDByFulltext", MDB_CREATE | MDB_DUPSORT, &conn->metaFileIDByFulltext);

	mdb_txn_commit(txn);


	debug_data(conn);


	for(index_t i = 0; i < CONNECTION_COUNT; ++i) {
		repo->workers[i] = async_worker_create();
		if(!repo->workers[i]) {
			EFSRepoFree(&repo);
			return NULL;
		}
	}
	repo->conn_count = CONNECTION_COUNT;
	repo->conn_sem = async_sem_create(CONNECTION_COUNT);
	if(!repo->conn_sem) {
		EFSRepoFree(&repo);
		return NULL;
	}
	return repo;
}
void EFSRepoFree(EFSRepoRef *const repoptr) {
	EFSRepoRef repo = *repoptr;
	if(!repo) return;

	for(index_t i = 0; i < repo->pull_count; ++i) {
		EFSPullFree(&repo->pulls[i]);
	}
	FREE(&repo->pulls);

	EFSConnection *const conn = &repo->conn;
	mdb_env_close(conn->env); conn->env = NULL;
	for(index_t i = 0; i < CONNECTION_COUNT; ++i) {
		async_worker_free(repo->workers[i]); repo->workers[i] = NULL;
	}
	repo->conn_count = 0;
	async_sem_free(repo->conn_sem); repo->conn_sem = NULL;

	FREE(&repo->dir);
	FREE(&repo->dataDir);
	FREE(&repo->tempDir);
	FREE(&repo->cacheDir);
	FREE(&repo->DBPath);
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
	if(!repo) return NULL;
	async_sem_wait(repo->conn_sem);
	assert(repo->conn_count > 0);
	--repo->conn_count;
	async_worker_t *const worker = repo->workers[repo->conn_count];
	repo->workers[repo->conn_count] = NULL;
	async_worker_enter(worker);
	return &repo->conn;
}
void EFSRepoDBClose(EFSRepoRef const repo, EFSConnection const **const connptr) {
	if(!repo) return;
	async_worker_t *const worker = async_worker_get_current();
	async_worker_leave(worker);
	assert(repo->conn_count < CONNECTION_COUNT);
	repo->workers[repo->conn_count] = worker;
	++repo->conn_count;
	async_sem_post(repo->conn_sem);
	*connptr = NULL;
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
		strarg_t const host = db_string(txn, conn->schema, db_column(pull_val, 1));
		strarg_t const username = db_string(txn, conn->schema, db_column(pull_val, 2));
		strarg_t const password = db_string(txn, conn->schema, db_column(pull_val, 3));
		strarg_t const cookie = db_string(txn, conn->schema, db_column(pull_val, 4));
		strarg_t const query = db_string(txn, conn->schema, db_column(pull_val, 5));

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
	db_bind(userID_val, 0, userID);
	DB_VAL(user_val, 3);
	db_bind(user_val, 0, username_id);
	db_bind(user_val, 1, passhash_id); // passhash
	db_bind(user_val, 2, passhash_id); // token
	mdb_put(txn, conn->userByID, userID_val, user_val, MDB_NOOVERWRITE);

	DB_VAL(username_val, 1);
	db_bind(username_val, 0, username_id);
	mdb_put(txn, conn->userIDByName, username_val, userID_val, MDB_NOOVERWRITE);

	DB_VAL(pullID_val, 1);
	db_bind(pullID_val, 0, 1);

	uint64_t const host_id = db_string_id(txn, conn->schema, "localhost:8009");
	uint64_t const remote_username_id = db_string_id(txn, conn->schema, "ben");
	uint64_t const remote_password_id = db_string_id(txn, conn->schema, "testing");
	uint64_t const cookie_id = db_string_id(txn, conn->schema, "s=1892%3A4qKSMlVOtdrWXXjpE6CnQvckLjs%3D");
	uint64_t const query_id = db_string_id(txn, conn->schema, "");
	assert(host_id);
	assert(query_id);

	DB_VAL(pull_val, 6);
	db_bind(pull_val, 0, userID);
	db_bind(pull_val, 1, host_id);
	db_bind(pull_val, 2, remote_username_id);
	db_bind(pull_val, 3, remote_password_id);
	db_bind(pull_val, 4, cookie_id);
	db_bind(pull_val, 5, query_id);

	mdb_put(txn, conn->pullByID, pullID_val, pull_val, MDB_NOOVERWRITE);

	mdb_txn_commit(txn); txn = NULL;
}

