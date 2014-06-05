#ifndef EARTHFS_H
#define EARTHFS_H

#include "../deps/sqlite/sqlite3.h"
#include "common.h"

typedef struct EFSRepo* EFSRepoRef;
typedef struct EFSSession* EFSSessionRef;
typedef struct EFSSubmission* EFSSubmissionRef;
typedef struct EFSHasher* EFSHasherRef;
typedef struct EFSURIList* EFSURIListRef;
typedef struct EFSFilter* EFSFilterRef;
typedef struct EFSJSONFilterBuilder* EFSJSONFilterBuilderRef;

EFSRepoRef EFSRepoCreate(strarg_t const path);
void EFSRepoFree(EFSRepoRef const repo);
strarg_t EFSRepoGetPath(EFSRepoRef const repo);
strarg_t EFSRepoGetDataPath(EFSRepoRef const repo);
strarg_t EFSRepoGetDBPath(EFSRepoRef const repo);

typedef enum {
	EFS_RDONLY = 1 << 0,
	EFS_WRONLY = 1 << 1,
	EFS_RDWR = EFS_RDONLY | EFS_WRONLY,
} EFSMode;

EFSSessionRef EFSRepoCreateSession(EFSRepoRef const repo, strarg_t const user, strarg_t const pass, strarg_t const cookie, EFSMode const mode);
void EFSSessionFree(EFSSessionRef const session);

EFSHasherRef EFSHasherCreate(strarg_t const type);
void EFSHasherFree(EFSHasherRef const hasher);
void EFSHasherWrite(EFSHasherRef const hasher, byte_t const *const buf, ssize_t const len);

EFSURIListRef EFSHasherCreateURIList(EFSHasherRef const hasher);
void EFSURIListFree(EFSURIListRef const list);
count_t EFSURIListGetCount(EFSURIListRef const list);
strarg_t EFSURIListGetURI(EFSURIListRef const list, index_t const x);

typedef enum {
	EFSFilterInvalid,
	EFSNoFilter,
	EFSIntersectionFilter,
	EFSUnionFilter,
	EFSFullTextFilter,
	EFSBacklinkFilesFilter,
	EFSFileLinksFilter,
} EFSFilterType;

EFSFilterRef EFSFilterCreate(EFSFilterType const type);
void EFSFilterFree(EFSFilterRef const filter);
err_t EFSFilterAddStringArg(EFSFilterRef const filter, strarg_t const str, size_t const len);
err_t EFSFilterAddFilterArg(EFSFilterRef const filter, EFSFilterRef const subfilter);
sqlite3_stmt *EFSFilterCreateQuery(EFSFilterRef const filter);
err_t EFSFilterAppendSQL(EFSFilterRef const filter, str_t **const sql, size_t *const len, size_t *const size, off_t const indent);
err_t EFSFilterBindQueryArgs(EFSFilterRef const filter, sqlite3_stmt *const stmt, index_t *const index);

EFSJSONFilterBuilderRef EFSJSONFilterBuilderCreate(void);
void EFSJSONFilterBuilderFree(EFSJSONFilterBuilderRef const builder);
void EFSJSONFilterBuilderParse(EFSJSONFilterBuilderRef const builder, strarg_t const json, size_t const len);
EFSFilterRef EFSJSONFilterBuilderDone(EFSJSONFilterBuilderRef const builder);
EFSFilterType EFSFilterTypeFromString(strarg_t const type, size_t const len);

#endif

