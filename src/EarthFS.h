#ifndef EARTHFS_H
#define EARTHFS_H

#include "common.h"

typedef struct EFSRepo* EFSRepoRef;
typedef struct EFSSession* EFSSessionRef;
typedef struct EFSSubmission* EFSSubmissionRef;
typedef struct EFSHasher* EFSHasherRef;
typedef struct EFSURIList* EFSURIListRef;

EFSHasherRef EFSHasherCreate(str_t const *const type);
void EFSHasherFree(EFSHasherRef const hasher);
void EFSHasherWrite(EFSHasherRef const hasher, byte_t const *const buf, ssize_t const len);

EFSURIListRef EFSHasherCreateURIList(EFSHasherRef const hasher);
void EFSURIListFree(EFSURIListRef const list);
count_t EFSURIListGetCount(EFSURIListRef const list);
str_t const *EFSURIListGetURI(EFSURIListRef const list, index_t const x);

#endif

