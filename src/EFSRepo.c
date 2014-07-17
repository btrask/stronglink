#define _GNU_SOURCE
#include "async.h"
#include "EarthFS.h"

struct EFSRepo {
	str_t *dir;
	str_t *dataDir;
	str_t *tempDir;
	str_t *cacheDir;
	str_t *DBPath;
};

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
sqlite3 *EFSRepoDBConnect(EFSRepoRef const repo) {
	if(!repo) return NULL;
	// TODO: Connection pooling.
	sqlite3 *db = NULL;
	int const status = sqlite3_open_v2(
		repo->DBPath,
		&db,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX,
		NULL
	);
	assertf(SQLITE_OK == status, "EFSRepo database connection error");
	assertf(db, "EFSRepo database connection error");
	// When we switch to connection pooling, it should be impossible for this method to fail because the connections will be already open and we will block until one is available.
	return db;
}
void EFSRepoDBClose(EFSRepoRef const repo, sqlite3 **const dbptr) {
	if(!repo) return;
	sqlite3_close(*dbptr); *dbptr = NULL;
}

void EFSRepoStartPulls(EFSRepoRef const repo) {
	if(!repo) return;
	sqlite3 *db = EFSRepoDBConnect(repo);

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
	sqlite3_finalize(select); select = NULL;

	EFSRepoDBClose(repo, &db);
}

