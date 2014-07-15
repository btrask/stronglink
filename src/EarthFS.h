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
typedef struct EFSPull* EFSPullRef;

EFSRepoRef EFSRepoCreate(strarg_t const dir);
void EFSRepoFree(EFSRepoRef *const repoptr);
strarg_t EFSRepoGetDir(EFSRepoRef const repo);
strarg_t EFSRepoGetDataDir(EFSRepoRef const repo);
str_t *EFSRepoCopyInternalPath(EFSRepoRef const repo, strarg_t const internalHash);
strarg_t EFSRepoGetTempDir(EFSRepoRef const repo);
str_t *EFSRepoCopyTempPath(EFSRepoRef const repo);
strarg_t EFSRepoGetCacheDir(EFSRepoRef const repo);
sqlite3 *EFSRepoDBConnect(EFSRepoRef const repo);
void EFSRepoDBClose(EFSRepoRef const repo, sqlite3 **const dbptr);
void EFSRepoStartPulls(EFSRepoRef const repo);

typedef struct {
	str_t *path;
	str_t *type;
	uint64_t size;
} EFSFileInfo;

str_t *EFSRepoCreateCookie(EFSRepoRef const repo, strarg_t const username, strarg_t const password);
EFSSessionRef EFSRepoCreateSession(EFSRepoRef const repo, strarg_t const cookie);
EFSSessionRef EFSRepoCreateSessionInternal(EFSRepoRef const repo, int64_t const userID); // TODO: Private
void EFSSessionFree(EFSSessionRef *const sessionptr);
EFSRepoRef EFSSessionGetRepo(EFSSessionRef const session);
int64_t EFSSessionGetUserID(EFSSessionRef const session);
URIListRef EFSSessionCreateFilteredURIList(EFSSessionRef const session, EFSFilterRef const filter, count_t const max); // TODO: Public API?
EFSFileInfo *EFSSessionCopyFileInfo(EFSSessionRef const session, strarg_t const URI);
void EFSFileInfoFree(EFSFileInfo **const infoptr);

EFSSubmissionRef EFSSubmissionCreate(EFSSessionRef const session, strarg_t const type);
void EFSSubmissionFree(EFSSubmissionRef *const subptr);
err_t EFSSubmissionWrite(EFSSubmissionRef const sub, byte_t const *const buf, size_t const len);
err_t EFSSubmissionEnd(EFSSubmissionRef const sub);
err_t EFSSubmissionWriteFrom(EFSSubmissionRef const sub, ssize_t (*read)(void *, byte_t const **), void *const context);
strarg_t EFSSubmissionGetPrimaryURI(EFSSubmissionRef const sub);
err_t EFSSubmissionAddFile(EFSSubmissionRef const sub);
err_t EFSSubmissionStore(EFSSubmissionRef const sub, sqlite3 *const db);
// Convenience methods
EFSSubmissionRef EFSSubmissionCreateAndAdd(EFSSessionRef const session, strarg_t const type, ssize_t (*read)(void *, byte_t const **), void *const context);
EFSSubmissionRef EFSSubmissionCreateAndAddPair(EFSSessionRef const session, strarg_t const type, ssize_t (*read)(void *, byte_t const **), void *const context, strarg_t const title); // Automatically adds a basic meta-file too.

EFSHasherRef EFSHasherCreate(strarg_t const type);
void EFSHasherFree(EFSHasherRef *const hasherptr);
err_t EFSHasherWrite(EFSHasherRef const hasher, byte_t const *const buf, size_t const len);
URIListRef EFSHasherEnd(EFSHasherRef const hasher);
strarg_t EFSHasherGetInternalHash(EFSHasherRef const hasher);

EFSMetaFileRef EFSMetaFileCreate(strarg_t const type);
void EFSMetaFileFree(EFSMetaFileRef *const metaptr);
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
	EFSLinkedFromFilter,
	EFSLinksToFilter,
	EFSPermissionFilter,
} EFSFilterType;

typedef struct {
	int64_t fileID;
	bool_t more;
} EFSMatch;

EFSFilterRef EFSFilterCreate(EFSFilterType const type);
EFSFilterRef EFSPermissionFilterCreate(int64_t const userID);
void EFSFilterFree(EFSFilterRef *const filterptr);
err_t EFSFilterAddStringArg(EFSFilterRef const filter, strarg_t const str, ssize_t const len);
err_t EFSFilterAddFilterArg(EFSFilterRef const filter, EFSFilterRef const subfilter);
sqlite3_stmt *EFSFilterCreateQuery(EFSFilterRef const filter);
err_t EFSFilterPrepare(EFSFilterRef const filter, sqlite3 *const db);
EFSMatch EFSFilterMatchFile(EFSFilterRef const filter, int64_t const sortID, int64_t const lastFileID);
int64_t EFSFilterMatchAge(EFSFilterRef const filter, int64_t const fileID);

EFSJSONFilterBuilderRef EFSJSONFilterBuilderCreate(void);
void EFSJSONFilterBuilderFree(EFSJSONFilterBuilderRef *const builderptr);
void EFSJSONFilterBuilderParse(EFSJSONFilterBuilderRef const builder, strarg_t const json, size_t const len);
EFSFilterRef EFSJSONFilterBuilderDone(EFSJSONFilterBuilderRef const builder);
EFSFilterType EFSFilterTypeFromString(strarg_t const type, size_t const len);

EFSPullRef EFSRepoCreatePull(EFSRepoRef const repo, int64_t const pullID, int64_t const userID, strarg_t const host, strarg_t const username, strarg_t const password, strarg_t const cookie, strarg_t const query);
void EFSPullFree(EFSPullRef *const pullptr);
void EFSPullStart(EFSPullRef const pull);

#define EFS_ALGO_SIZE 32
#define EFS_HASH_SIZE 256
#define EFS_INTERNAL_ALGO ((strarg_t)"sha256")
static bool_t EFSParseURI(strarg_t const URI, str_t *const algo, str_t *const hash) {
	algo[0] = '\0';
	hash[0] = '\0';
	sscanf(URI, "hash://%31[a-zA-Z0-9.-]/%255[a-zA-Z0-9.%_-]", algo, hash);
	return algo[0] && hash[0];
}
static str_t *EFSFormatURI(strarg_t const algo, strarg_t const hash) {
	strarg_t const fmt = "hash://%s/%s";
	int const len = snprintf(NULL, 0, fmt, algo, hash)+1;
	if(len < 0) return NULL;
	str_t *URI = malloc(len);
	if(!URI) return NULL;
	if(snprintf(URI, len, fmt, algo, hash) < 0) {
		FREE(&URI);
		return NULL;
	}
	return URI;
}

#endif
