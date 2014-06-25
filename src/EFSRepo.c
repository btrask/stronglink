#define _GNU_SOURCE
#include "EarthFS.h"

struct EFSRepo {
	str_t *dir;
	str_t *dataDir;
	str_t *tempDir;
	str_t *cacheDir;
	str_t *DBPath;
};

EFSRepoRef EFSRepoCreate(strarg_t const dir) {
	BTAssert(dir, "EFSRepo dir required");
	EFSRepoRef const repo = calloc(1, sizeof(struct EFSRepo));
	repo->dir = strdup(dir);
	(void)BTErrno(asprintf(&repo->dataDir, "%s/data", dir));
	(void)BTErrno(asprintf(&repo->tempDir, "%s/tmp", dir));
	(void)BTErrno(asprintf(&repo->cacheDir, "%s/cache", dir));
	(void)BTErrno(asprintf(&repo->DBPath, "%s/efs.db", dir));
	return repo;
}
void EFSRepoFree(EFSRepoRef const repo) {
	if(!repo) return;
	FREE(&repo->dir);
	FREE(&repo->dataDir);
	FREE(&repo->tempDir);
	FREE(&repo->cacheDir);
	FREE(&repo->DBPath);
	free(repo);
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
	str_t *str;
	if(asprintf(&str, "%s/efs-tmp", repo->tempDir) < 0) return NULL; // TODO: Real random filename.
	return str;
}
strarg_t EFSRepoGetCacheDir(EFSRepoRef const repo) {
	if(!repo) return NULL;
	return repo->cacheDir;
}
sqlite3 *EFSRepoDBConnect(EFSRepoRef const repo) {
	if(!repo) return NULL;
	// TODO: Connection pooling.
	sqlite3 *db = NULL;
	(void)BTSQLiteErr(sqlite3_open_v2(
		repo->DBPath,
		&db,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX,
		NULL
	));
//	BTSQLiteErr(sqlite3_busy_timeout(db, 5));
	return db;
}
void EFSRepoDBClose(EFSRepoRef const repo, sqlite3 *const db) {
	if(!repo) return;
	(void)sqlite3_close(db);
}

