#define _GNU_SOURCE
#include "EarthFS.h"

struct EFSRepo {
	str_t *path;
	str_t *dataPath;
	str_t *tempPath;
	str_t *DBPath;
	sqlite3_mutex *lock;
};

EFSRepoRef EFSRepoCreate(strarg_t const path) {
	BTAssert(path, "EFSRepo path required");
	EFSRepoRef const repo = calloc(1, sizeof(struct EFSRepo));
	repo->path = strdup(path);
	(void)BTErrno(asprintf(&repo->dataPath, "%s/data", path));
	(void)BTErrno(asprintf(&repo->tempPath, "%s/tmp", path));
	(void)BTErrno(asprintf(&repo->DBPath, "%s/efs.db", path));
	repo->lock = sqlite3_mutex_alloc(0);
	return repo;
}
void EFSRepoFree(EFSRepoRef const repo) {
	if(!repo) return;
	FREE(&repo->path);
	free(repo);
}
strarg_t EFSRepoGetPath(EFSRepoRef const repo) {
	if(!repo) return NULL;
	return repo->path;
}
strarg_t EFSRepoGetDataPath(EFSRepoRef const repo) {
	if(!repo) return NULL;
	return repo->dataPath;
}
strarg_t EFSRepoGetTempPath(EFSRepoRef const repo) {
	if(!repo) return NULL;
	return repo->tempPath;
}
sqlite3 *EFSRepoDBConnect(EFSRepoRef const repo) {
	if(!repo) return NULL;
	// TODO: Connection pooling.
	sqlite3_mutex_enter(repo->lock);
	sqlite3 *db = NULL;
	(void)BTSQLiteErr(sqlite3_open_v2(
		repo->DBPath,
		&db,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
		NULL
	));
	return db;
}
void EFSRepoDBClose(EFSRepoRef const repo, sqlite3 *const db) {
	if(!repo) return;
	(void)sqlite3_close(db);
	sqlite3_mutex_leave(repo->lock);
}

