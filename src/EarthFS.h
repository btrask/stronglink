#ifndef EARTHFS_H
#define EARTHFS_H

#include "../deps/sqlite/sqlite3.h"
#include "common.h"
#include "URIList.h"

typedef struct EFSRepo* EFSRepoRef;
typedef struct EFSSession* EFSSessionRef;
typedef struct EFSSubmission* EFSSubmissionRef;
typedef struct EFSHasher* EFSHasherRef;
typedef struct EFSMetaFile* EFSMetaFileRef;
typedef struct EFSFilter* EFSFilterRef;
typedef struct EFSJSONFilterBuilder* EFSJSONFilterBuilderRef;

EFSRepoRef EFSRepoCreate(strarg_t const dir);
void EFSRepoFree(EFSRepoRef const repo);
strarg_t EFSRepoGetDir(EFSRepoRef const repo);
strarg_t EFSRepoGetDataDir(EFSRepoRef const repo);
str_t *EFSRepoCopyInternalPath(EFSRepoRef const repo, strarg_t const internalHash);
strarg_t EFSRepoGetTempDir(EFSRepoRef const repo);
str_t *EFSRepoCopyTempPath(EFSRepoRef const repo);
strarg_t EFSRepoGetCacheDir(EFSRepoRef const repo);
sqlite3 *EFSRepoDBConnect(EFSRepoRef const repo);
void EFSRepoDBClose(EFSRepoRef const repo, sqlite3 *const db);

typedef enum {
	EFS_RDONLY = 1 << 0,
	EFS_WRONLY = 1 << 1,
	EFS_RDWR = EFS_RDONLY | EFS_WRONLY,
} EFSMode;

typedef struct {
	str_t *path;
	str_t *type;
	uint64_t size;
} EFSFileInfo;

EFSSessionRef EFSRepoCreateSession(EFSRepoRef const repo, strarg_t const user, strarg_t const pass, strarg_t const cookie, EFSMode const mode);
void EFSSessionFree(EFSSessionRef const session);
EFSRepoRef const EFSSessionGetRepo(EFSSessionRef const session);
int64_t EFSSessionGetUserID(EFSSessionRef const session);
URIListRef EFSSessionCreateFilteredURIList(EFSSessionRef const session, EFSFilterRef const filter, count_t const max); // TODO: Public API?
EFSFileInfo *EFSSessionCopyFileInfo(EFSSessionRef const session, strarg_t const URI);
void EFSFileInfoFree(EFSFileInfo *const info);

EFSSubmissionRef EFSRepoCreateSubmission(EFSRepoRef const repo, strarg_t const type, ssize_t (*read)(void *, byte_t const **), void *const context);
void EFSSubmissionFree(EFSSubmissionRef const sub);
strarg_t EFSSubmissionGetPrimaryURI(EFSSubmissionRef const sub);
err_t EFSSessionAddSubmission(EFSSessionRef const session, EFSSubmissionRef const submission);

EFSHasherRef EFSHasherCreate(strarg_t const type);
void EFSHasherFree(EFSHasherRef const hasher);
void EFSHasherWrite(EFSHasherRef const hasher, byte_t const *const buf, ssize_t const len);
URIListRef EFSHasherEnd(EFSHasherRef const hasher);
strarg_t EFSHasherGetInternalHash(EFSHasherRef const hasher);

EFSMetaFileRef EFSMetaFileCreate(strarg_t const type);
void EFSMetaFileFree(EFSMetaFileRef const meta);
err_t EFSMetaFileWrite(EFSMetaFileRef const meta, byte_t const *const buf, size_t const len);
err_t EFSMetaFileEnd(EFSMetaFileRef const meta);
err_t EFSMetaFileStore(EFSMetaFileRef const meta, int64_t const fileID, strarg_t const URI, sqlite3 *const db);

typedef enum {
	EFSFilterInvalid,
	EFSNoFilter,
	EFSFileTypeFilter,
	EFSIntersectionFilter,
	EFSUnionFilter,
	EFSFullTextFilter,
	EFSBacklinkFilesFilter,
	EFSFileLinksFilter,
	EFSPermissionFilter,
} EFSFilterType;

EFSFilterRef EFSFilterCreate(EFSFilterType const type);
EFSFilterRef EFSPermissionFilterCreate(int64_t const userID);
void EFSFilterFree(EFSFilterRef const filter);
err_t EFSFilterAddStringArg(EFSFilterRef const filter, strarg_t const str, size_t const len);
err_t EFSFilterAddFilterArg(EFSFilterRef const filter, EFSFilterRef const subfilter);
sqlite3_stmt *EFSFilterCreateQuery(EFSFilterRef const filter);
void EFSFilterCreateTempTables(sqlite3 *const db); // "results"
void EFSFilterExec(EFSFilterRef const filter, sqlite3 *const db, int64_t const depth);

EFSJSONFilterBuilderRef EFSJSONFilterBuilderCreate(void);
void EFSJSONFilterBuilderFree(EFSJSONFilterBuilderRef const builder);
void EFSJSONFilterBuilderParse(EFSJSONFilterBuilderRef const builder, strarg_t const json, size_t const len);
EFSFilterRef EFSJSONFilterBuilderDone(EFSJSONFilterBuilderRef const builder);
EFSFilterType EFSFilterTypeFromString(strarg_t const type, size_t const len);

#endif
