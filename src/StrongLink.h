#ifndef EARTHFS_H
#define EARTHFS_H

#include "db/db_base.h"
#include "async/async.h"
#include "common.h"

#define URI_MAX 1024

typedef struct EFSRepo* EFSRepoRef;
typedef struct EFSSession* EFSSessionRef;
typedef struct EFSSubmission* EFSSubmissionRef;
typedef struct EFSHasher* EFSHasherRef;
typedef struct EFSFilter* EFSFilterRef;
typedef struct EFSJSONFilterParser* EFSJSONFilterParserRef;
typedef struct EFSPull* EFSPullRef;

EFSRepoRef EFSRepoCreate(strarg_t const dir, strarg_t const name);
void EFSRepoFree(EFSRepoRef *const repoptr);
strarg_t EFSRepoGetDir(EFSRepoRef const repo);
strarg_t EFSRepoGetDataDir(EFSRepoRef const repo);
str_t *EFSRepoCopyInternalPath(EFSRepoRef const repo, strarg_t const internalHash);
strarg_t EFSRepoGetTempDir(EFSRepoRef const repo);
str_t *EFSRepoCopyTempPath(EFSRepoRef const repo);
strarg_t EFSRepoGetCacheDir(EFSRepoRef const repo);
strarg_t EFSRepoGetName(EFSRepoRef const repo);
void EFSRepoDBOpen(EFSRepoRef const repo, DB_env **const dbptr);
void EFSRepoDBClose(EFSRepoRef const repo, DB_env **const dbptr);
void EFSRepoSubmissionEmit(EFSRepoRef const repo, uint64_t const sortID);
bool EFSRepoSubmissionWait(EFSRepoRef const repo, uint64_t const sortID, uint64_t const future);
void EFSRepoPullsStart(EFSRepoRef const repo);
void EFSRepoPullsStop(EFSRepoRef const repo);

int EFSRepoCookieCreate(EFSRepoRef const repo, strarg_t const username, strarg_t const password, str_t **const outCookie);
int EFSRepoCookieAuth(EFSRepoRef const repo, strarg_t const cookie, uint64_t *const outUserID);


typedef struct {
	str_t *hash; // Internal hash
	str_t *path;
	str_t *type;
	uint64_t size;
} EFSFileInfo;

EFSSessionRef EFSRepoCreateSession(EFSRepoRef const repo, strarg_t const cookie);
EFSSessionRef EFSRepoCreateSessionInternal(EFSRepoRef const repo, uint64_t const userID); // TODO: Private
void EFSSessionFree(EFSSessionRef *const sessionptr);
EFSRepoRef EFSSessionGetRepo(EFSSessionRef const session);
uint64_t EFSSessionGetUserID(EFSSessionRef const session);
int EFSSessionGetAuthError(EFSSessionRef const session);
str_t **EFSSessionCopyFilteredURIs(EFSSessionRef const session, EFSFilterRef const filter, count_t const max); // TODO: Public API?
int EFSSessionGetFileInfo(EFSSessionRef const session, strarg_t const URI, EFSFileInfo *const info);
void EFSFileInfoCleanup(EFSFileInfo *const info);
int EFSSessionGetValueForField(EFSSessionRef const session, str_t value[], size_t const max, strarg_t const fileURI, strarg_t const field);

EFSSubmissionRef EFSSubmissionCreate(EFSSessionRef const session, strarg_t const type);
void EFSSubmissionFree(EFSSubmissionRef *const subptr);
EFSRepoRef EFSSubmissionGetRepo(EFSSubmissionRef const sub);
strarg_t EFSSubmissionGetType(EFSSubmissionRef const sub);
uv_file EFSSubmissionGetFile(EFSSubmissionRef const sub);
int EFSSubmissionWrite(EFSSubmissionRef const sub, byte_t const *const buf, size_t const len);
int EFSSubmissionEnd(EFSSubmissionRef const sub);
int EFSSubmissionWriteFrom(EFSSubmissionRef const sub, ssize_t (*read)(void *, byte_t const **), void *const context);
strarg_t EFSSubmissionGetPrimaryURI(EFSSubmissionRef const sub);
int EFSSubmissionStore(EFSSubmissionRef const sub, DB_txn *const txn);
// Convenience methods
EFSSubmissionRef EFSSubmissionCreateQuick(EFSSessionRef const session, strarg_t const type, ssize_t (*read)(void *, byte_t const **), void *const context);
int EFSSubmissionBatchStore(EFSSubmissionRef const *const list, count_t const count);

EFSHasherRef EFSHasherCreate(strarg_t const type);
void EFSHasherFree(EFSHasherRef *const hasherptr);
int EFSHasherWrite(EFSHasherRef const hasher, byte_t const *const buf, size_t const len);
str_t **EFSHasherEnd(EFSHasherRef const hasher);
strarg_t EFSHasherGetInternalHash(EFSHasherRef const hasher);

typedef enum {
	EFSFilterTypeInvalid = 0,
	EFSAllFilterType,
	EFSFileTypeFilterType,
	EFSIntersectionFilterType,
	EFSUnionFilterType,
	EFSFulltextFilterType,
	EFSMetadataFilterType,
	EFSLinksToFilterType,
	EFSLinkedFromFilterType,
	EFSPermissionFilterType,
	EFSMetaFileFilterType,
} EFSFilterType;

EFSFilterRef EFSFilterCreate(EFSFilterType const type);
EFSFilterRef EFSPermissionFilterCreate(uint64_t const userID);
void EFSFilterFree(EFSFilterRef *const filterptr);
EFSFilterType EFSFilterGetType(EFSFilterRef const filter);
EFSFilterRef EFSFilterUnwrap(EFSFilterRef const filter);
strarg_t EFSFilterGetStringArg(EFSFilterRef const filter, index_t const i);
int EFSFilterAddStringArg(EFSFilterRef const filter, strarg_t const str, ssize_t const len);
int EFSFilterAddFilterArg(EFSFilterRef const filter, EFSFilterRef const subfilter);
void EFSFilterPrint(EFSFilterRef const filter, count_t const depth);
size_t EFSFilterToUserFilterString(EFSFilterRef const filter, str_t *const data, size_t const size, count_t const depth);
int EFSFilterPrepare(EFSFilterRef const filter, DB_txn *const txn);
void EFSFilterSeek(EFSFilterRef const filter, int const dir, uint64_t const sortID, uint64_t const fileID);
void EFSFilterCurrent(EFSFilterRef const filter, int const dir, uint64_t *const sortID, uint64_t *const fileID);
void EFSFilterStep(EFSFilterRef const filter, int const dir);
uint64_t EFSFilterAge(EFSFilterRef const filter, uint64_t const sortID, uint64_t const fileID);
str_t *EFSFilterCopyNextURI(EFSFilterRef const filter, int const dir, DB_txn *const txn);

EFSJSONFilterParserRef EFSJSONFilterParserCreate(void);
void EFSJSONFilterParserFree(EFSJSONFilterParserRef *const parserptr);
void EFSJSONFilterParserWrite(EFSJSONFilterParserRef const parser, strarg_t const json, size_t const len);
EFSFilterRef EFSJSONFilterParserEnd(EFSJSONFilterParserRef const parser);
EFSFilterType EFSFilterTypeFromString(strarg_t const type, size_t const len);

EFSFilterRef EFSUserFilterParse(strarg_t const query);

EFSPullRef EFSRepoCreatePull(EFSRepoRef const repo, uint64_t const pullID, uint64_t const userID, strarg_t const host, strarg_t const username, strarg_t const password, strarg_t const cookie, strarg_t const query);
void EFSPullFree(EFSPullRef *const pullptr);
int EFSPullStart(EFSPullRef const pull);
void EFSPullStop(EFSPullRef const pull);

#define EFS_URI_MAX 512 // Otherwise use URI_MAX
#define EFS_INTERNAL_ALGO "sha256"
#define EFS_ALGO_SIZE 32
#define EFS_HASH_SIZE 256
#define EFS_ALGO_FMT "%31[a-zA-Z0-9.-]"
#define EFS_HASH_FMT "%255[a-zA-Z0-9.%_-]"
static int EFSParseURI(strarg_t const URI, str_t *const algo, str_t *const hash) {
	int len = 0;
	algo[0] = '\0';
	hash[0] = '\0';
	sscanf(URI, "hash://" EFS_ALGO_FMT "/" EFS_HASH_FMT "%n", algo, hash, &len);
	if(!algo[0]) return -1;
	if(!hash[0]) return -1;
	if('/' == URI[len]) len++;
	if('\0' != URI[len] && '?' != URI[len]) return -1;
	return 0;
}
static str_t *EFSFormatURI(strarg_t const algo, strarg_t const hash) {
	return aasprintf("hash://%s/%s", algo, hash);
}

#endif
