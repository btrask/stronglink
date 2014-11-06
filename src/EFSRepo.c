#define _GNU_SOURCE /* asprintf() */
#include <assert.h>
#include "async/async.h"
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

static void createDBConnection(EFSRepoRef const repo);
static void loadPulls(EFSRepoRef const repo);

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

	createDBConnection(repo);
	debug_data(repo->conn); // TODO
	loadPulls(repo);

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

	EFSRepoPullsStop(repo);

	FREE(&repo->dir);
	FREE(&repo->dataDir);
	FREE(&repo->tempDir);
	FREE(&repo->cacheDir);
	FREE(&repo->DBPath);

	EFSConnection *const conn = repo->conn;
	db_env_close(conn->env); conn->env = NULL;
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
	assert(repo->dataDir);
	assert(internalHash);
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

void EFSRepoPullsStart(EFSRepoRef const repo) {
	if(!repo) return;
	for(index_t i = 0; i < repo->pull_count; ++i) {
		EFSPullStart(repo->pulls[i]);
	}
}
void EFSRepoPullsStop(EFSRepoRef const repo) {
	if(!repo) return;
	for(index_t i = 0; i < repo->pull_count; ++i) {
		EFSPullStop(repo->pulls[i]);
	}
}


static void createDBConnection(EFSRepoRef const repo) {
	assert(repo);

	EFSConnection *const conn = repo->conn;
	db_env_create(&conn->env);
	db_env_set_mapsize(conn->env, 1024 * 1024 * 256);
	db_env_open(conn->env, repo->DBPath, 0, 0600);
}
static void loadPulls(EFSRepoRef const repo) {
	assert(repo);
	EFSConnection const *conn = EFSRepoDBOpen(repo);
	int rc;
	DB_txn *txn = NULL;
	rc = db_txn_begin(conn->env, NULL, DB_RDONLY, &txn);
	assert(DB_SUCCESS == rc);

	DB_cursor *cur = NULL;
	rc = db_cursor_open(txn, &cur);
	assertf(DB_SUCCESS == rc, "Database error %s\n", db_strerror(rc));

	DB_VAL(pulls_min, 1);
	DB_VAL(pulls_max, 1);
	db_bind(pulls_min, EFSPullByID+0);
	db_bind(pulls_max, EFSPullByID+1);
	DB_range pulls = { pulls_min, pulls_max };
	DB_val pullID_key[1];
	DB_val pull_val[1];
	rc = db_cursor_firstr(cur, &pulls, pullID_key, pull_val, +1);
	for(; DB_SUCCESS == rc; rc = db_cursor_nextr(cur, &pulls, pullID_key, pull_val, +1)) {
		uint64_t const pullID = db_column(pullID_key, 1);
		uint64_t const userID = db_column(pull_val, 0);
		strarg_t const host = db_column_text(txn, pull_val, 1);
		strarg_t const username = db_column_text(txn, pull_val, 2);
		strarg_t const password = db_column_text(txn, pull_val, 3);
		strarg_t const cookie = db_column_text(txn, pull_val, 4);
		strarg_t const query = db_column_text(txn, pull_val, 5);

		EFSPullRef const pull = EFSRepoCreatePull(repo, pullID, userID, host, username, password, cookie, query);
		if(repo->pull_count+1 > repo->pull_size) {
			repo->pull_size = (repo->pull_count+1) * 2;
			repo->pulls = realloc(repo->pulls, sizeof(EFSPullRef) * repo->pull_size);
			assert(repo->pulls); // TODO: Handle error
		}
		repo->pulls[repo->pull_count++] = pull;
	}

	db_cursor_close(cur); cur = NULL;
	db_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(repo, &conn);
}
static void debug_data(EFSConnection const *const conn) {
	int rc;
	DB_txn *txn = NULL;
	rc = db_txn_begin(conn->env, NULL, DB_RDWR, &txn);
	assert(!rc);
	assert(txn);

	uint64_t const userID = 1;
	uint64_t const username_id = db_string_id(txn, "ben");
	uint64_t const passhash_id = db_string_id(txn, "$2a$08$lhAQjgGPuwvtErV.aK.MGO1T2W0UhN1r4IngmF5FvY0LM826aF8ye");
	assert(username_id);
	assert(passhash_id);

	DB_VAL(userID_key, 2);
	db_bind(userID_key, EFSUserByID);
	db_bind(userID_key, userID);
	DB_VAL(user_val, 3);
	db_bind(user_val, username_id);
	db_bind(user_val, passhash_id); // passhash
	db_bind(user_val, passhash_id); // token
	rc = db_put(txn, userID_key, user_val, 0);
	assert(!rc);

	DB_VAL(username_key, 2);
	db_bind(username_key, EFSUserIDByName);
	db_bind(username_key, username_id);
	DB_VAL(userID_val, 1);
	db_bind(userID_val, userID);
	rc = db_put(txn, username_key, userID_val, 0);
	assert(!rc);

	DB_VAL(pullID_key, 1);
	db_bind(pullID_key, EFSPullByID);
	db_bind(pullID_key, 1);

	uint64_t const host_id = db_string_id(txn, "localhost:8009");
	uint64_t const remote_username_id = db_string_id(txn, "ben");
	uint64_t const remote_password_id = db_string_id(txn, "testing");
	uint64_t const cookie_id = db_string_id(txn, "s=1:not-very-random");
	uint64_t const query_id = db_string_id(txn, "");
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

	rc = db_put(txn, pullID_key, pull_val, 0);
	assert(!rc);

	rc = db_txn_commit(txn); txn = NULL;
	assert(!rc);
}

