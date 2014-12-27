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
	int rc = db_env_create(&conn->env);
	assertf(!rc, "Database error %s", db_strerror(rc));
	rc = db_env_set_mapsize(conn->env, 1024 * 1024 * 256);
	assertf(!rc, "Database error %s", db_strerror(rc));
	rc = db_env_open(conn->env, repo->DBPath, 0, 0600);
	assertf(!rc, "Database error %s", db_strerror(rc));
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

	DB_RANGE(pulls, DB_VARINT_MAX);
	db_bind_uint64(pulls->min, EFSPullByID);
	db_range_genmax(pulls);
	DB_val pullID_key[1];
	DB_val pull_val[1];
	rc = db_cursor_firstr(cur, pulls, pullID_key, pull_val, +1);
	for(; DB_SUCCESS == rc; rc = db_cursor_nextr(cur, pulls, pullID_key, pull_val, +1)) {
		uint64_t const table = db_read_uint64(pullID_key);
		assert(EFSPullByID == table);
		uint64_t const pullID = db_read_uint64(pullID_key);
		uint64_t const userID = db_read_uint64(pull_val);
		strarg_t const host = db_read_string(txn, pull_val);
		strarg_t const username = db_read_string(txn, pull_val);
		strarg_t const password = db_read_string(txn, pull_val);
		strarg_t const cookie = db_read_string(txn, pull_val);
		strarg_t const query = db_read_string(txn, pull_val);

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
	char const *const username = "ben";
	char const *const passhash = "$2a$08$lhAQjgGPuwvtErV.aK.MGO1T2W0UhN1r4IngmF5FvY0LM826aF8ye";
	char const *const token = passhash;

	DB_VAL(userID_key, DB_VARINT_MAX + DB_VARINT_MAX);
	db_bind_uint64(userID_key, EFSUserByID);
	db_bind_uint64(userID_key, userID);
	DB_VAL(user_val, DB_INLINE_MAX * 3);
	db_bind_string(txn, user_val, username);
	db_bind_string(txn, user_val, passhash);
	db_bind_string(txn, user_val, token);
	rc = db_put(txn, userID_key, user_val, 0);
	assert(!rc);

	DB_VAL(username_key, DB_VARINT_MAX + DB_INLINE_MAX);
	db_bind_uint64(username_key, EFSUserIDByName);
	db_bind_string(txn, username_key, username);
	DB_VAL(userID_val, DB_VARINT_MAX);
	db_bind_uint64(userID_val, userID);
	rc = db_put(txn, username_key, userID_val, 0);
	assert(!rc);

	DB_VAL(pullID_key, DB_VARINT_MAX + DB_VARINT_MAX);
	db_bind_uint64(pullID_key, EFSPullByID);
	db_bind_uint64(pullID_key, 1);

	char const *const host = "localhost:8009";
	char const *const remote_username = "ben";
	char const *const remote_password = "testing";
	char const *const cookie = "s=1:not-very-random";
	char const *const query = "";

	DB_VAL(pull_val, DB_VARINT_MAX * 1 + DB_INLINE_MAX * 5);
	db_bind_uint64(pull_val, userID);
	db_bind_string(txn, pull_val, host);
	db_bind_string(txn, pull_val, remote_username);
	db_bind_string(txn, pull_val, remote_password);
	db_bind_string(txn, pull_val, cookie);
	db_bind_string(txn, pull_val, query);

	rc = db_put(txn, pullID_key, pull_val, 0);
	assert(!rc);

	rc = db_txn_commit(txn); txn = NULL;
	assert(!rc);
}

