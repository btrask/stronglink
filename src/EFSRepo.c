#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "EarthFS.h"

struct EFSRepo {
	str_t *path;
	str_t *dataPath;
	str_t *DBPath; // TODO: sqlite3 permissions object? not an actual DB connection.
};

EFSRepoRef EFSRepoCreate(str_t const *const path) {
	BTAssert(path, "EFSRepo path required");
	EFSRepoRef const repo = calloc(1, sizeof(struct EFSRepo));
	repo->path = strdup(path);
	(void)BTErrno(asprintf(&repo->dataPath, "%s/data", path));
	(void)BTErrno(asprintf(&repo->DBPath, "%s/repo.db", path));
	return repo;
}
void EFSRepoFree(EFSRepoRef const repo) {
	if(!repo) return;
	free(repo->path); repo->path = NULL;
	free(repo);
}
str_t const *EFSRepoGetPath(EFSRepoRef const repo) {
	if(!repo) return NULL;
	return repo->path;
}
str_t const *EFSRepoGetDataPath(EFSRepoRef const repo) {
	if(!repo) return NULL;
	return repo->dataPath;
}
str_t const *EFSRepoGetDBPath(EFSRepoRef const repo) {
	if(!repo) return NULL;
	return repo->DBPath;
}

