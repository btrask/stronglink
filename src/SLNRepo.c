#include "StrongLink.h"
#include "SLNDB.h"

#define CACHE_SIZE 1000

struct SLNRepo {
	str_t *dir;
	str_t *name;

	str_t *dataDir;
	str_t *tempDir;
	str_t *cacheDir;
	str_t *DBPath;

	SLNMode pub_mode;
	SLNMode reg_mode;
	SLNSessionCacheRef session_cache;

	DB_env *db;

	async_mutex_t sub_mutex[1];
	async_cond_t sub_cond[1];
	uint64_t sub_latest;

	SLNPullRef *pulls;
	count_t pull_count;
	count_t pull_size;
};

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

	// TODO: Configuration
	// TODO: The ability to limit public registration
	repo->pub_mode = 0;
	repo->reg_mode = 0;
	repo->session_cache = SLNSessionCacheCreate(repo, CACHE_SIZE);
	if(!repo->session_cache) {
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

	repo->pub_mode = 0;
	repo->reg_mode = 0;
	SLNSessionCacheFree(&repo->session_cache);

	db_env_close(repo->db); repo->db = NULL;

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

SLNMode SLNRepoGetPublicMode(SLNRepoRef const repo) {
	if(!repo) return 0;
	return repo->pub_mode;
}
SLNMode SLNRepoGetRegistrationMode(SLNRepoRef const repo) {
	if(!repo) return 0;
	return repo->reg_mode;
}
SLNSessionCacheRef SLNRepoGetSessionCache(SLNRepoRef const repo) {
	if(!repo) return NULL;
	return repo->session_cache;
}

void SLNRepoDBOpen(SLNRepoRef const repo, DB_env **const dbptr) {
	assert(repo);
	assert(dbptr);
	async_pool_enter(NULL);
	*dbptr = repo->db;
}
void SLNRepoDBClose(SLNRepoRef const repo, DB_env **const dbptr) {
	assert(repo);
	assert(dbptr);
	if(!*dbptr) return;
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


#define PASS_LEN 16
static int create_admin(SLNRepoRef const repo, DB_txn *const txn) {
	SLNSessionCacheRef const cache = SLNRepoGetSessionCache(repo);
	SLNSessionRef root = SLNSessionCreateInternal(cache, 0, NULL, 0, SLN_ROOT, NULL);
	if(!root) return DB_ENOMEM;

	strarg_t username = getenv("USER"); // TODO: Portability?
	if(!username) username = "admin";

	byte_t buf[PASS_LEN/2];
	int rc = async_random(buf, sizeof(buf));
	if(rc < 0) return DB_ENOMEM; // ???
	char password[PASS_LEN+1];
	tohex(password, buf, sizeof(buf));
	password[PASS_LEN] = '\0';

	rc = SLNSessionCreateUserInternal(root, txn, username, password, SLN_ROOT);
	if(DB_SUCCESS != rc) return rc;

	fprintf(stdout, "ACCOUNT CREATED\n");
	fprintf(stdout, "  Username: %s\n", username);
	fprintf(stdout, "  Password: %s\n", password);
	fprintf(stdout, "  Please change your password after logging in\n");

	return DB_SUCCESS;
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

	DB_cursor *cursor = NULL;
	rc = db_txn_cursor(txn, &cursor);
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		SLNRepoDBClose(repo, &db);
		fprintf(stderr, "Database cursor error (%s)\n", db_strerror(rc));
		return rc;
	}

	DB_range users[1];
	SLNUserByIDKeyRange0(users, txn);
	rc = db_cursor_firstr(cursor, users, NULL, NULL, +1);
	if(DB_NOTFOUND == rc) {
		rc = create_admin(repo, txn);
	}
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		SLNRepoDBClose(repo, &db);
		fprintf(stderr, "Database user error (%s)\n", db_strerror(rc));
		return rc;
	}

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

	DB_val pullID_key[1];
	SLNPullByIDKeyPack(pullID_key, txn, 1);
	uint64_t const userID = 1;
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

