#ifndef EARTHFS_H
#define EARTHFS_H

#include "common.h"

typedef struct EFSRepo* EFSRepoRef;
typedef struct EFSSession* EFSSessionRef;
typedef struct EFSSubmission* EFSSubmissionRef;
typedef struct EFSHasher* EFSHasherRef;
typedef struct EFSURIList* EFSURIListRef;

typedef enum {
	EFS_RDONLY = 1 << 0,
	EFS_WRONLY = 1 << 1,
	EFS_RDWR = EFS_RDONLY | EFS_WRONLY,
} EFSMode;

EFSRepoRef EFSRepoCreate(strarg_t const path);
void EFSRepoFree(EFSRepoRef const repo);
strarg_t EFSRepoGetPath(EFSRepoRef const repo);
strarg_t EFSRepoGetDataPath(EFSRepoRef const repo);
strarg_t EFSRepoGetDBPath(EFSRepoRef const repo);

EFSSessionRef EFSRepoCreateSession(EFSRepoRef const repo, strarg_t const user, strarg_t const pass, strarg_t const cookie, EFSMode const mode);
void EFSSessionFree(EFSSessionRef const session);

EFSHasherRef EFSHasherCreate(strarg_t const type);
void EFSHasherFree(EFSHasherRef const hasher);
void EFSHasherWrite(EFSHasherRef const hasher, byte_t const *const buf, ssize_t const len);

EFSURIListRef EFSHasherCreateURIList(EFSHasherRef const hasher);
void EFSURIListFree(EFSURIListRef const list);
count_t EFSURIListGetCount(EFSURIListRef const list);
strarg_t EFSURIListGetURI(EFSURIListRef const list, index_t const x);

#endif

