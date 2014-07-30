#define _GNU_SOURCE
#include "async.h"
#include "EarthFS.h"

#define CONNECTION_COUNT 24

struct EFSRepo {
	str_t *dir;
	str_t *dataDir;
	str_t *tempDir;
	str_t *cacheDir;
	str_t *DBPath;

	sqlite3f *connections[CONNECTION_COUNT];
	index_t cur;
	count_t count;
	cothread_t *thread_list;
};

static sqlite3f *openDB(strarg_t const path);

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
	for(index_t i = 0; i < CONNECTION_COUNT; ++i) {
		repo->connections[i] = openDB(repo->DBPath);
		if(!repo->connections[i]) {
			EFSRepoFree(&repo);
			return NULL;
		}
	}
	repo->count = CONNECTION_COUNT;
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
	for(index_t i = 0; i < CONNECTION_COUNT; ++i) {
		sqlite3f_close(repo->connections[i]); repo->connections[i] = NULL;
	}
	repo->count = 0;
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
sqlite3f *EFSRepoDBConnect(EFSRepoRef const repo) {
	if(!repo) return NULL;
	if(!repo->count) {
		assertf(0, "Blocking on DB connection not yet implemented");
	}
	sqlite3f *const db = repo->connections[repo->cur];
	repo->cur = (repo->cur + 1) % CONNECTION_COUNT;
	repo->count--;
	return db;
}
void EFSRepoDBClose(EFSRepoRef const repo, sqlite3f **const dbptr) {
	if(!repo) return;
	index_t const pos = (repo->cur + repo->count) % CONNECTION_COUNT;
	repo->connections[pos] = *dbptr;
	repo->count++;
	*dbptr = NULL;
}

void EFSRepoStartPulls(EFSRepoRef const repo) {
	if(!repo) return;
	sqlite3f *db = EFSRepoDBConnect(repo);

	sqlite3_stmt *select = QUERY(db,
		"SELECT\n"
		"	pull_id, user_id, host, username, password, cookie, query\n"
		"FROM pulls");
	while(SQLITE_ROW == STEP(select)) {
		int col = 0;
		int64_t const pullID = sqlite3_column_int64(select, col++);
		int64_t const userID = sqlite3_column_int64(select, col++);
		strarg_t const host = (strarg_t)sqlite3_column_text(select, col++);
		strarg_t const username = (strarg_t)sqlite3_column_text(select, col++);
		strarg_t const password = (strarg_t)sqlite3_column_text(select, col++);
		strarg_t const cookie = (strarg_t)sqlite3_column_text(select, col++);
		strarg_t const query = (strarg_t)sqlite3_column_text(select, col++);
		EFSPullRef const pull = EFSRepoCreatePull(repo, pullID, userID, host, username, password, cookie, query);
		EFSPullStart(pull);
		// TODO: Keep a list?
	}
	sqlite3f_finalize(select); select = NULL;

	EFSRepoDBClose(repo, &db);
}

static int retry(void *const ctx, int const count) {
	return 1;
}
static sqlite3f *openDB(strarg_t const path) {
	sqlite3 *conn = NULL;
	if(SQLITE_OK != sqlite3_open_v2(
		path,
		&conn,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX,
		NULL
	)) return NULL;
	sqlite3f *db = sqlite3f_create(conn);
	if(!db) {
		sqlite3_close(conn); conn = NULL;
		return NULL;
	}
	int err = 0;
	err = sqlite3_extended_result_codes(conn, 1);
	assertf(SQLITE_OK == err, "Couldn't turn on extended results codes");
	err = sqlite3_busy_handler(conn, retry, NULL);
	assertf(SQLITE_OK == err, "Couldn't set busy handler");
	EXEC(QUERY(db, "PRAGMA synchronous=NORMAL"));
	EXEC(QUERY(db, "PRAGMA wal_autocheckpoint=5000"));
	EXEC(QUERY(db, "PRAGMA cache_size=-8000"));
	EXEC(QUERY(db, "PRAGMA mmap_size=268435456")); // 256MB, as recommended.
	return db;
}

