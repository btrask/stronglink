#include "EFSRepoPrivate.h"

static void createDBConnection(EFSRepoRef const repo);
static void loadPulls(EFSRepoRef const repo);

static void debug_data(DB_env *const db);

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
	debug_data(repo->db); // TODO
	loadPulls(repo);

	if(EFSRepoAuthInit(repo) < 0) {
		EFSRepoFree(&repo);
		return NULL;
	}

	async_mutex_init(repo->sub_mutex, 0);
	async_cond_init(repo->sub_cond, 0);
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

	db_env_close(repo->db); repo->db = NULL;

	EFSRepoAuthDestroy(repo);

	async_mutex_destroy(repo->sub_mutex);
	async_cond_destroy(repo->sub_cond);
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
strarg_t EFSRepoGetName(EFSRepoRef const repo) {
	if(!repo) return NULL;
	return "unnamed repo"; // TODO: By default, just use the name of the directory?
}

void EFSRepoDBOpen(EFSRepoRef const repo, DB_env **const dbptr) {
	assert(repo);
	assert(dbptr);
	async_pool_enter(NULL);
	*dbptr = repo->db;
}
void EFSRepoDBClose(EFSRepoRef const repo, DB_env **const dbptr) {
	assert(repo);
	async_pool_leave(NULL);
	*dbptr = NULL;
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
bool EFSRepoSubmissionWait(EFSRepoRef const repo, uint64_t const sortID, uint64_t const future) {
	assert(repo);
	async_mutex_lock(repo->sub_mutex);
	while(repo->sub_latest <= sortID && async_cond_timedwait(repo->sub_cond, repo->sub_mutex, future) >= 0);
	bool const res = repo->sub_latest > sortID;
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

	int rc = db_env_create(&repo->db);
	rc = db_env_set_mapsize(repo->db, 1024 * 1024 * 256);
	assertf(!rc, "Database error %s", db_strerror(rc));
	rc = db_env_open(repo->db, repo->DBPath, 0, 0600);
	assertf(!rc, "Database error %s", db_strerror(rc));
}
static void loadPulls(EFSRepoRef const repo) {
	assert(repo);
	DB_env *db = NULL;
	EFSRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	assert(DB_SUCCESS == rc);

	DB_cursor *cur = NULL;
	rc = db_cursor_open(txn, &cur);
	assertf(DB_SUCCESS == rc, "Database error %s\n", db_strerror(rc));

	DB_range pulls[1];
	EFSPullByIDRange0(pulls, txn);
	DB_val pullID_key[1];
	DB_val pull_val[1];
	rc = db_cursor_firstr(cur, pulls, pullID_key, pull_val, +1);
	for(; DB_SUCCESS == rc; rc = db_cursor_nextr(cur, pulls, pullID_key, pull_val, +1)) {
		uint64_t pullID;
		EFSPullByIDKeyUnpack(pullID_key, txn, &pullID);
		uint64_t userID;
		strarg_t host;
		strarg_t username;
		strarg_t password;
		strarg_t cookie;
		strarg_t query;
		EFSPullByIDValUnpack(pull_val, txn, &userID, &host, &username, &password, &cookie, &query);

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
	EFSRepoDBClose(repo, &db);
}
static void debug_data(DB_env *const db) {
	int rc;
	DB_txn *txn = NULL;
	rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
	assert(!rc);
	assert(txn);

	uint64_t const userID = 1;
	char const *const username = "ben";
	char const *const passhash = "$2a$08$lhAQjgGPuwvtErV.aK.MGO1T2W0UhN1r4IngmF5FvY0LM826aF8ye";
	char const *const token = passhash;

	DB_val userID_key[1];
	EFSUserByIDKeyPack(userID_key, txn, userID);
	DB_val user_val[1];
	EFSUserByIDValPack(user_val, txn, username, passhash, token);
	rc = db_put(txn, userID_key, user_val, 0);
	assert(!rc);

	DB_val username_key[1];
	EFSUserIDByNameKeyPack(username_key, txn, username);
	DB_val userID_val[1];
	EFSUserIDByNameValPack(userID_val, txn, userID);
	rc = db_put(txn, username_key, userID_val, 0);
	assert(!rc);

	DB_val pullID_key[1];
	EFSPullByIDKeyPack(pullID_key, txn, 1);
	char const *const host = "localhost:8009";
	char const *const remote_username = "ben";
	char const *const remote_password = "testing";
	char const *const cookie = NULL;
	char const *const query = "";
	DB_val pull_val[1];
	EFSPullByIDValPack(pull_val, txn, userID, host, remote_username, remote_password, cookie, query);

	rc = db_put(txn, pullID_key, pull_val, 0);
	assert(!rc);

	rc = db_txn_commit(txn); txn = NULL;
	assert(!rc);
}

