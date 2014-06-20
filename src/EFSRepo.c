#define _GNU_SOURCE
#include "EarthFS.h"

struct EFSRepo {
	str_t *path;
	str_t *dataPath;
	str_t *tempPath;
	str_t *DBPath;
};

EFSRepoRef EFSRepoCreate(strarg_t const path) {
	BTAssert(path, "EFSRepo path required");
	EFSRepoRef const repo = calloc(1, sizeof(struct EFSRepo));
	repo->path = strdup(path);
	(void)BTErrno(asprintf(&repo->dataPath, "%s/data", path));
	(void)BTErrno(asprintf(&repo->tempPath, "%s/tmp", path));
	(void)BTErrno(asprintf(&repo->DBPath, "%s/efs.db", path));
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
	sqlite3 *db = NULL;
	(void)BTSQLiteErr(sqlite3_open_v2(
		repo->DBPath,
		&db,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, // TODO: For debugging
		NULL
	));
//	BTSQLiteErr(sqlite3_busy_timeout(db, 5));
	return db;
}
void EFSRepoDBClose(EFSRepoRef const repo, sqlite3 *const db) {
	if(!repo) return;
	(void)sqlite3_close(db);
}

