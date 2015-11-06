// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#ifndef STRONGLINK_H
#define STRONGLINK_H

#include "db/db_base.h"
#include "async/async.h"
#include "common.h"

#define URI_MAX (1023+1)

#define SLN_META_TYPE "application/vnd.stronglink.meta"

extern uint32_t SLNSeed;

typedef uint32_t SLNMode;
enum {
	SLN_RDONLY = 1 << 0,
	SLN_WRONLY = 1 << 1,
	SLN_RDWR = SLN_RDONLY | SLN_WRONLY,
	SLN_ROOT = 0xFF,
};

typedef struct SLNRepo* SLNRepoRef;
typedef struct SLNSessionCache* SLNSessionCacheRef;
typedef struct SLNSession* SLNSessionRef;
typedef struct SLNSubmission* SLNSubmissionRef;
typedef struct SLNHasher* SLNHasherRef;
typedef struct SLNFilter* SLNFilterRef;
typedef struct SLNJSONFilterParser* SLNJSONFilterParserRef;
typedef struct SLNSync* SLNSyncRef;
typedef struct SLNPull* SLNPullRef;

// BerkeleyDB uses -30800 to -30999
// MDB uses -30600 to -30799?
#define SLN_HASHMISMATCH (-30599)
#define SLN_INVALIDTARGET (-30598)
#define SLN_NOSESSION (-30597)
#define SLN_LAST_ERRCODE SLN_INVALIDTARGET

static strarg_t sln_strerror(int const rc) {
	if(rc >= 0) return "No error";
	switch(rc) {
		case SLN_HASHMISMATCH: return "Hash mismatch";
		case SLN_INVALIDTARGET: return "Invalid meta-file target";
		case SLN_NOSESSION: return "No session";
	}
	strarg_t x = uv_strerror(rc);
	if(!x) x = db_strerror(rc);
	return x;
}

SLNRepoRef SLNRepoCreate(strarg_t const dir, strarg_t const name);
void SLNRepoFree(SLNRepoRef *const repoptr);
strarg_t SLNRepoGetDir(SLNRepoRef const repo);
strarg_t SLNRepoGetDataDir(SLNRepoRef const repo);
str_t *SLNRepoCopyInternalPath(SLNRepoRef const repo, strarg_t const internalHash);
strarg_t SLNRepoGetTempDir(SLNRepoRef const repo);
str_t *SLNRepoCopyTempPath(SLNRepoRef const repo);
strarg_t SLNRepoGetCacheDir(SLNRepoRef const repo);
strarg_t SLNRepoGetName(SLNRepoRef const repo);
SLNMode SLNRepoGetPublicMode(SLNRepoRef const repo);
SLNMode SLNRepoGetRegistrationMode(SLNRepoRef const repo);
SLNSessionCacheRef SLNRepoGetSessionCache(SLNRepoRef const repo);
void SLNRepoDBOpenUnsafe(SLNRepoRef const repo, DB_env **const dbptr);
void SLNRepoDBClose(SLNRepoRef const repo, DB_env **const dbptr);
void SLNRepoSubmissionEmit(SLNRepoRef const repo, uint64_t const sortID);
int SLNRepoSubmissionWait(SLNRepoRef const repo, uint64_t *const sortID, uint64_t const future);
void SLNRepoPullsStart(SLNRepoRef const repo);
void SLNRepoPullsStop(SLNRepoRef const repo);


// TODO: Make this private (and maybe clean it up).
#define SESSION_KEY_LEN 16
#define SESSION_KEY_HEX (SESSION_KEY_LEN*2)
#define SESSION_KEY_FMT "%32[0-9a-fA-F]"

int SLNSessionCacheCreate(SLNRepoRef const repo, uint16_t const size, SLNSessionCacheRef *const out);
void SLNSessionCacheFree(SLNSessionCacheRef *const cacheptr);
SLNRepoRef SLNSessionCacheGetRepo(SLNSessionCacheRef const cache);
int SLNSessionCacheCreateSession(SLNSessionCacheRef const cache, strarg_t const username, strarg_t const password, SLNSessionRef *const out);
int SLNSessionCacheLoadSessionUnsafe(SLNSessionCacheRef const cache, uint64_t const id, SLNSessionRef *const out);
int SLNSessionCacheLoadSession(SLNSessionCacheRef const cache, uint64_t const id, byte_t const *const key, SLNSessionRef *const out);
int SLNSessionCacheCopyActiveSession(SLNSessionCacheRef const cache, strarg_t const cookie, SLNSessionRef *const out);


typedef struct {
	str_t *hash; // Internal hash
	str_t *path;
	str_t *type;
	uint64_t size;
} SLNFileInfo;


SLNSessionRef SLNSessionCreateInternal(SLNSessionCacheRef const cache, uint64_t const sessionID, byte_t const *const sessionKeyRaw, byte_t const *const sessionKeyEnc, uint64_t const userID, SLNMode const mode_trusted, strarg_t const username);
SLNSessionRef SLNSessionRetain(SLNSessionRef const session);
void SLNSessionRelease(SLNSessionRef *const sessionptr);
SLNSessionCacheRef SLNSessionGetCache(SLNSessionRef const session);
SLNRepoRef SLNSessionGetRepo(SLNSessionRef const session);
uint64_t SLNSessionGetID(SLNSessionRef const session);
int SLNSessionKeyValid(SLNSessionRef const session, byte_t const *const enc);
uint64_t SLNSessionGetUserID(SLNSessionRef const session);
bool SLNSessionHasPermission(SLNSessionRef const session, SLNMode const mask) __attribute__((warn_unused_result));
strarg_t SLNSessionGetUsername(SLNSessionRef const session);
str_t *SLNSessionCopyCookie(SLNSessionRef const session);
int SLNSessionDBOpen(SLNSessionRef const session, SLNMode const mode, DB_env **const dbptr) __attribute__((warn_unused_result));
void SLNSessionDBClose(SLNSessionRef const session, DB_env **const dbptr);
int SLNSessionCreateUser(SLNSessionRef const session, DB_txn *const txn, strarg_t const username, strarg_t const password);
int SLNSessionCreateUserInternal(SLNSessionRef const session, DB_txn *const txn, strarg_t const username, strarg_t const password, SLNMode const mode_unsafe);
int SLNSessionCreateSession(SLNSessionRef const session, SLNSessionRef *const out);
int SLNSessionGetFileInfo(SLNSessionRef const session, strarg_t const URI, SLNFileInfo *const info);
void SLNFileInfoCleanup(SLNFileInfo *const info);
int SLNSessionGetSubmittedFile(SLNSessionRef const session, DB_txn *const txn, strarg_t const URI); // Relevant results are DB_NOTFOUND and SLN_NOSESSION.
int SLNSessionSetSubmittedFile(SLNSessionRef const session, DB_txn *const txn, strarg_t const URI);
int SLNSessionCopyLastSubmissionURIs(SLNSessionRef const session, str_t *const outFileURI, str_t *const outMetaURI);
int SLNSessionGetValueForField(SLNSessionRef const session, DB_txn *const txn, strarg_t const fileURI, strarg_t const field, str_t *out, size_t const max);
int SLNSessionGetNextMetaMapURI(SLNSessionRef const session, strarg_t const targetURI, uint64_t *const metaMapID, str_t *out, size_t const max);
int SLNSessionAddMetaMap(SLNSessionRef const session, strarg_t const metaURI, strarg_t const targetURI);

int SLNSubmissionCreate(SLNSessionRef const session, strarg_t const knownURI, strarg_t const knownTarget, SLNSubmissionRef *const out);
int SLNSubmissionCreateQuick(SLNSessionRef const session, strarg_t const knownURI, strarg_t const knownTarget, strarg_t const type, ssize_t (*read)(void *, byte_t const **), void *const context, SLNSubmissionRef *const out);
void SLNSubmissionFree(SLNSubmissionRef *const subptr);
SLNRepoRef SLNSubmissionGetRepo(SLNSubmissionRef const sub);
strarg_t SLNSubmissionGetKnownURI(SLNSubmissionRef const sub);
strarg_t SLNSubmissionGetKnownTarget(SLNSubmissionRef const sub);
strarg_t SLNSubmissionGetType(SLNSubmissionRef const sub);
int SLNSubmissionSetType(SLNSubmissionRef const sub, strarg_t const type);
uv_file SLNSubmissionGetFile(SLNSubmissionRef const sub);
int SLNSubmissionWrite(SLNSubmissionRef const sub, byte_t const *const buf, size_t const len);
int SLNSubmissionEnd(SLNSubmissionRef const sub);
int SLNSubmissionWriteFrom(SLNSubmissionRef const sub, ssize_t (*read)(void *, byte_t const **), void *const context);
strarg_t SLNSubmissionGetPrimaryURI(SLNSubmissionRef const sub);
int SLNSubmissionGetFileInfo(SLNSubmissionRef const sub, SLNFileInfo *const info);
int SLNSubmissionStore(SLNSubmissionRef const sub, DB_txn *const txn);
int SLNSubmissionStoreBatch(SLNSubmissionRef const *const list, size_t const count);


typedef struct {
	char const *const name;
	int (*init)(char const *const type, void **const algo);
	int (*update)(void *const ctx, byte_t const *const buf, size_t const len);
	ssize_t (*final)(void *const ctx, byte_t *const out, size_t const max);
} SLNAlgo;

SLNHasherRef SLNHasherCreate(strarg_t const type);
void SLNHasherFree(SLNHasherRef *const hasherptr);
int SLNHasherWrite(SLNHasherRef const hasher, byte_t const *const buf, size_t const len);
str_t **SLNHasherEnd(SLNHasherRef const hasher);
strarg_t SLNHasherGetInternalHash(SLNHasherRef const hasher);


typedef unsigned SLNFilterType;
enum {
	SLNFilterTypeInvalid = 0,

	// All files and meta-files
	SLNAllFilterType = 1,
	// All meta-files
	SLNMetaFileFilterType = 2,
	// All files that are targeted by at least one meta-file
	SLNVisibleFilterType = 3,
	// Files with a given MIME type
//	SLNFileTypeFilterType = 4, // TODO
	// AND operation
	SLNIntersectionFilterType = 5,
	// OR operation
	SLNUnionFilterType = 6,
	// NOT operation (Note: does not add results, only removes)
	SLNNegationFilterType = 7,
	// Direct lookup
	SLNURIFilterType = 8,
	// Meta-files with a given target
	SLNTargetURIFilterType = 9,
	// Full-text search // TODO: Phrase search
	SLNFulltextFilterType = 10,
	// Exact meta-data field and value // TODO: Case-insensitivity
	SLNMetadataFilterType = 11,
	// Backlinks (everything linking to a file with the given URI)
	SLNLinksToFilterType = 12,
	// Forward links (everything linked from a file with the given URI)
//	SLNLinkedFromFilterType = 13, // TODO
};

typedef struct {
	uint64_t min;
	uint64_t max;
} SLNAgeRange;

int SLNFilterCreate(SLNSessionRef const session, SLNFilterType const type, SLNFilterRef *const out);
SLNFilterRef SLNFilterCreateInternal(SLNFilterType const type);
void SLNFilterFree(SLNFilterRef *const filterptr);
SLNFilterType SLNFilterGetType(SLNFilterRef const filter);
SLNFilterRef SLNFilterUnwrap(SLNFilterRef const filter);
strarg_t SLNFilterGetStringArg(SLNFilterRef const filter, size_t const i);
int SLNFilterAddStringArg(SLNFilterRef const filter, strarg_t const str, ssize_t const len);
int SLNFilterAddFilterArg(SLNFilterRef const filter, SLNFilterRef const subfilter);
void SLNFilterPrintSexp(SLNFilterRef const filter, FILE *const file, size_t const depth);
void SLNFilterPrintUser(SLNFilterRef const filter, FILE *const file, size_t const depth);
int SLNFilterPrepare(SLNFilterRef const filter, DB_txn *const txn);
void SLNFilterSeek(SLNFilterRef const filter, int const dir, uint64_t const sortID, uint64_t const fileID);
void SLNFilterCurrent(SLNFilterRef const filter, int const dir, uint64_t *const sortID, uint64_t *const fileID);
void SLNFilterStep(SLNFilterRef const filter, int const dir);
SLNAgeRange SLNFilterFullAge(SLNFilterRef const filter, uint64_t const fileID);
uint64_t SLNFilterFastAge(SLNFilterRef const filter, uint64_t const fileID, uint64_t const sortID);


typedef struct {
	int dir;
	str_t *URI;
	uint64_t sortID;
	uint64_t fileID;
} SLNFilterPosition;

typedef int (*SLNFilterWriteCB)(void *ctx, uv_buf_t const parts[], unsigned int const count);
typedef int (*SLNFilterFlushCB)(void *ctx);

void SLNFilterParseOptions(strarg_t const qs, SLNFilterPosition *const start, uint64_t *const count, int *const dir, bool *const wait);
void SLNFilterPositionInit(SLNFilterPosition *const pos, int const dir);
void SLNFilterPositionCleanup(SLNFilterPosition *const pos);

int SLNFilterSeekToPosition(SLNFilterRef const filter, SLNFilterPosition const *const pos, DB_txn *const txn);
int SLNFilterGetPosition(SLNFilterRef const filter, SLNFilterPosition *const pos, DB_txn *const txn);
int SLNFilterCopyNextURI(SLNFilterRef const filter, int const dir, bool const meta, DB_txn *const txn, str_t **const out);

ssize_t SLNFilterCopyURIs(SLNFilterRef const filter, SLNSessionRef const session, SLNFilterPosition *const pos, int const dir, bool const meta, str_t *URIs[], size_t const max);
ssize_t SLNFilterWriteURIBatch(SLNFilterRef const filter, SLNSessionRef const session, SLNFilterPosition *const pos, bool const meta, uint64_t const max, SLNFilterWriteCB const writecb, void *ctx);
int SLNFilterWriteURIs(SLNFilterRef const filter, SLNSessionRef const session, SLNFilterPosition *const pos, bool const meta, uint64_t const max, bool const wait, SLNFilterWriteCB const writecb, SLNFilterFlushCB const flushcb, void *ctx);


int SLNJSONFilterParserCreate(SLNSessionRef const session, SLNJSONFilterParserRef *const out);
void SLNJSONFilterParserFree(SLNJSONFilterParserRef *const parserptr);
void SLNJSONFilterParserWrite(SLNJSONFilterParserRef const parser, strarg_t const json, size_t const len);
SLNFilterRef SLNJSONFilterParserEnd(SLNJSONFilterParserRef const parser);
SLNFilterType SLNFilterTypeFromString(strarg_t const type, size_t const len);

int SLNUserFilterParse(SLNSessionRef const session, strarg_t const query, SLNFilterRef *const out);

int SLNSyncCreate(SLNSessionRef const session, SLNSyncRef *const out);
void SLNSyncFree(SLNSyncRef *const syncptr);
int SLNSyncFileAvailable(SLNSyncRef const sync, strarg_t const URI, strarg_t const targetURI);
int SLNSyncIngestFileURI(SLNSyncRef const sync, strarg_t const fileURI);
int SLNSyncIngestMetaURI(SLNSyncRef const sync, strarg_t const metaURI, strarg_t const targetURI);
int SLNSyncWorkAwait(SLNSyncRef const sync, SLNSubmissionRef *const out);
int SLNSyncWorkDone(SLNSyncRef const sync, SLNSubmissionRef const sub);

int SLNPullCreate(SLNSessionCacheRef const cache, uint64_t const sessionID, strarg_t const certhash, strarg_t const host, strarg_t const path, strarg_t const query, strarg_t const cookie, SLNPullRef *const out);
void SLNPullFree(SLNPullRef *const pullptr);
int SLNPullStart(SLNPullRef const pull);
void SLNPullStop(SLNPullRef const pull);

#define SLN_URI_MAX (511+1) // Otherwise use URI_MAX.
#define SLN_URI_FMT "%511[a-zA-Z0-9.%_:/-]"
#define SLN_INTERNAL_ALGO "sha256" // Defines part of our on-disk format.
#define SLN_ALGO_SIZE (31+1)
#define SLN_HASH_SIZE (255+1)
#define SLN_ALGO_FMT "%31[a-zA-Z0-9.-]"
#define SLN_HASH_FMT "%255[a-zA-Z0-9.%_-]"
static int SLNParseURI(strarg_t const URI, str_t *const algo, str_t *const hash) {
	algo[0] = '\0';
	hash[0] = '\0';
	if(!URI) return UV_EINVAL;
	int len = 0;
	sscanf(URI, "hash://" SLN_ALGO_FMT "/" SLN_HASH_FMT "%n", algo, hash, &len);
	if(!algo[0]) return UV_EINVAL;
	if(!hash[0]) return UV_EINVAL;
	if('/' == URI[len]) len++;
	if('\0' != URI[len] && '?' != URI[len] && '#' != URI[len]) return UV_EINVAL;
	return 0;
}
static str_t *SLNFormatURI(strarg_t const algo, strarg_t const hash) {
	return aasprintf("hash://%s/%s", algo, hash);
}

#endif
