#include "SLNRepoPrivate.h"

static int createDBConnection(SLNRepoRef const repo);
static void loadPulls(SLNRepoRef const repo);

static void debug_data(DB_env *const db);

SLNRepoRef SLNRepoCreate(strarg_t const dir, strarg_t const name) {
	assert(dir);
	assert(name);
	SLNRepoRef repo = calloc(1, sizeof(struct SLNRepo));
	if(!repo) return NULL;
	repo->dir = strdup(dir);
	repo->name = strdup(name);
	if(!repo->dir || !repo->name) {
		SLNRepoFree(&repo);
		return NULL;
	}

	repo->dataDir = aasprintf("%s/data", dir);
	repo->tempDir = aasprintf("%s/tmp", dir);
	repo->cacheDir = aasprintf("%s/cache", dir);
	repo->DBPath = aasprintf("%s/efs.db", dir);
	if(!repo->dataDir || !repo->tempDir || !repo->cacheDir || !repo->DBPath) {
		SLNRepoFree(&repo);
		return NULL;
	}

	int rc = createDBConnection(repo);
	if(DB_SUCCESS != rc) {
		SLNRepoFree(&repo);
		return NULL;
	}
	debug_data(repo->db); // TODO
	loadPulls(repo);

	if(SLNRepoAuthInit(repo) < 0) {
		SLNRepoFree(&repo);
		return NULL;
	}

	async_mutex_init(repo->sub_mutex, 0);
	async_cond_init(repo->sub_cond, 0);
	return repo;
}
void SLNRepoFree(SLNRepoRef *const repoptr) {
	SLNRepoRef repo = *repoptr;
	if(!repo) return;

	SLNRepoPullsStop(repo);

	FREE(&repo->dir);
	FREE(&repo->name);

	FREE(&repo->dataDir);
	FREE(&repo->tempDir);
	FREE(&repo->cacheDir);
	FREE(&repo->DBPath);

	db_env_close(repo->db); repo->db = NULL;

	SLNRepoAuthDestroy(repo);

	async_mutex_destroy(repo->sub_mutex);
	async_cond_destroy(repo->sub_cond);
	repo->sub_latest = 0;

	for(index_t i = 0; i < repo->pull_count; ++i) {
		SLNPullFree(&repo->pulls[i]);
	}
	assert_zeroed(repo->pulls, repo->pull_count);
	FREE(&repo->pulls);
	repo->pull_count = 0;
	repo->pull_size = 0;

	assert_zeroed(repo, 1);
	FREE(repoptr); repo = NULL;
}

strarg_t SLNRepoGetDir(SLNRepoRef const repo) {
	if(!repo) return NULL;
	return repo->dir;
}
strarg_t SLNRepoGetName(SLNRepoRef const repo) {
	if(!repo) return NULL;
	return repo->name;
}

strarg_t SLNRepoGetDataDir(SLNRepoRef const repo) {
	if(!repo) return NULL;
	return repo->dataDir;
}
str_t *SLNRepoCopyInternalPath(SLNRepoRef const repo, strarg_t const internalHash) {
	if(!repo) return NULL;
	assert(repo->dataDir);
	assert(internalHash);
	return aasprintf("%s/%.2s/%s", repo->dataDir, internalHash, internalHash);
}
strarg_t SLNRepoGetTempDir(SLNRepoRef const repo) {
	if(!repo) return NULL;
	return repo->tempDir;
}
str_t *SLNRepoCopyTempPath(SLNRepoRef const repo) {
	if(!repo) return NULL;
	return async_fs_tempnam(repo->tempDir, "sln");
}
strarg_t SLNRepoGetCacheDir(SLNRepoRef const repo) {
	if(!repo) return NULL;
	return repo->cacheDir;
}

void SLNRepoDBOpen(SLNRepoRef const repo, DB_env **const dbptr) {
	assert(repo);
	assert(dbptr);
	async_pool_enter(NULL);
	*dbptr = repo->db;
}
void SLNRepoDBClose(SLNRepoRef const repo, DB_env **const dbptr) {
	assert(repo);
	async_pool_leave(NULL);
	*dbptr = NULL;
}

void SLNRepoSubmissionEmit(SLNRepoRef const repo, uint64_t const sortID) {
	assert(repo);
	async_mutex_lock(repo->sub_mutex);
	if(sortID > repo->sub_latest) {
		repo->sub_latest = sortID;
		async_cond_broadcast(repo->sub_cond);
	}
	async_mutex_unlock(repo->sub_mutex);
}
int SLNRepoSubmissionWait(SLNRepoRef const repo, uint64_t const sortID, uint64_t const future) {
	assert(repo);
	int rc = 0;
	async_mutex_lock(repo->sub_mutex);
	while(repo->sub_latest <= sortID) {
		rc = async_cond_timedwait(repo->sub_cond, repo->sub_mutex, future);
		if(rc < 0) break;
	}
	async_mutex_unlock(repo->sub_mutex);
	return rc;
}

void SLNRepoPullsStart(SLNRepoRef const repo) {
	if(!repo) return;
	for(index_t i = 0; i < repo->pull_count; ++i) {
		SLNPullStart(repo->pulls[i]);
	}
}
void SLNRepoPullsStop(SLNRepoRef const repo) {
	if(!repo) return;
	for(index_t i = 0; i < repo->pull_count; ++i) {
		SLNPullStop(repo->pulls[i]);
	}
}


static int createDBConnection(SLNRepoRef const repo) {
	assert(repo);
	int rc = db_env_create(&repo->db);
	rc = db_env_set_mapsize(repo->db, 1024 * 1024 * 256);
	if(DB_SUCCESS != rc) {
		fprintf(stderr, "Database setup error (%s)\n", db_strerror(rc));
		return rc;
	}
	rc = db_env_open(repo->db, repo->DBPath, 0, 0600);
	if(DB_SUCCESS != rc) {
		fprintf(stderr, "Database open error (%s)\n", db_strerror(rc));
		return rc;
	}

	DB_env *db = NULL;
	SLNRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
	if(DB_SUCCESS != rc) {
		SLNRepoDBClose(repo, &db);
		fprintf(stderr, "Database transaction error (%s)\n", db_strerror(rc));
		return rc;
	}

	rc = db_schema_verify(txn);
	if(DB_VERSION_MISMATCH == rc) {
		db_txn_abort(txn); txn = NULL;
		SLNRepoDBClose(repo, &db);
		fprintf(stderr, "Database incompatible with this software version\n");
		return rc;
	}
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		SLNRepoDBClose(repo, &db);
		fprintf(stderr, "Database schema layer error (%s)\n", db_strerror(rc));
		return rc;
	}

	// TODO: Application-level schema verification

	rc = db_txn_commit(txn); txn = NULL;
	SLNRepoDBClose(repo, &db);
	if(DB_SUCCESS != rc) {
		fprintf(stderr, "Database commit error (%s)\n", db_strerror(rc));
		return rc;
	}
	return DB_SUCCESS;
}
static void loadPulls(SLNRepoRef const repo) {
	assert(repo);
	DB_env *db = NULL;
	SLNRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	assert(DB_SUCCESS == rc);

	DB_cursor *cur = NULL;
	rc = db_cursor_open(txn, &cur);
	assertf(DB_SUCCESS == rc, "Database error %s\n", db_strerror(rc));

	DB_range pulls[1];
	SLNPullByIDRange0(pulls, txn);
	DB_val pullID_key[1];
	DB_val pull_val[1];
	rc = db_cursor_firstr(cur, pulls, pullID_key, pull_val, +1);
	for(; DB_SUCCESS == rc; rc = db_cursor_nextr(cur, pulls, pullID_key, pull_val, +1)) {
		uint64_t pullID;
		SLNPullByIDKeyUnpack(pullID_key, txn, &pullID);
		uint64_t userID;
		strarg_t host;
		strarg_t username;
		strarg_t password;
		strarg_t cookie;
		strarg_t query;
		SLNPullByIDValUnpack(pull_val, txn, &userID, &host, &username, &password, &cookie, &query);

		SLNPullRef const pull = SLNRepoCreatePull(repo, pullID, userID, host, username, password, cookie, query);
		if(repo->pull_count+1 > repo->pull_size) {
			repo->pull_size = (repo->pull_count+1) * 2;
			repo->pulls = realloc(repo->pulls, sizeof(SLNPullRef) * repo->pull_size);
			assert(repo->pulls); // TODO: Handle error
		}
		repo->pulls[repo->pull_count++] = pull;
	}

	db_cursor_close(cur); cur = NULL;
	db_txn_abort(txn); txn = NULL;
	SLNRepoDBClose(repo, &db);
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
	SLNUserByIDKeyPack(userID_key, txn, userID);
	DB_val user_val[1];
	SLNUserByIDValPack(user_val, txn, username, passhash, token);
	rc = db_put(txn, userID_key, user_val, 0);
	assert(!rc);

	DB_val username_key[1];
	SLNUserIDByNameKeyPack(username_key, txn, username);
	DB_val userID_val[1];
	SLNUserIDByNameValPack(userID_val, txn, userID);
	rc = db_put(txn, username_key, userID_val, 0);
	assert(!rc);

	DB_val pullID_key[1];
	SLNPullByIDKeyPack(pullID_key, txn, 1);
	char const *const host = "localhost:8009";
	char const *const remote_username = "ben";
	char const *const remote_password = "testing";
	char const *const cookie = NULL;
	char const *const query = "";
	DB_val pull_val[1];
	SLNPullByIDValPack(pull_val, txn, userID, host, remote_username, remote_password, cookie, query);

	rc = db_put(txn, pullID_key, pull_val, 0);
	assert(!rc);

	rc = db_txn_commit(txn); txn = NULL;
	assert(!rc);
}

