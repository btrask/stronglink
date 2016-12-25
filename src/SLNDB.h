// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <kvstore/kvs_schema.h>

enum {
	// Note: these IDs are part of the persistent database format,
	// so don't just go changing them.

	// 0-19 are reserved for the DB layer.

	SLNUserByID = 20,
	SLNUserIDByName = 21,
	SLNSessionByID = 22,
	SLNPullByID = 23, // Every PullID is a SyncID is a SessionID.

	SLNFileByID = 40,
	SLNFileIDByInfo = 41,
//	SLNFileIDByType = 42, // TODO
	SLNFileIDAndURI = 43,
	SLNURIAndFileID = 44,

	SLNMetaFileByID = 60, // Every MetaFileID is a FileID.
//	SLNFileIDAndMetaFileID = 61, // Redundant, they're equivalent.
	SLNTargetURIAndMetaFileID = 62,
	SLNMetaFileIDFieldAndValue = 63,
	SLNFieldValueAndMetaFileID = 64,
	SLNTermMetaFileIDAndPosition = 65,
	SLNFirstUniqueMetaFileID = 66,

	SLNFileIDAndSessionID = 80, // TODO: Pending deprecation?
	SLNSessionIDAndHintIDToMetaURIAndTargetURI = 81,
	SLNMetaURIAndSessionIDToHintID = 82,
	SLNTargetURISessionIDAndHintID = 83,
	SLNSessionIDAndHintsSyncedFileID = 84, // Very similar to SLNFileIDAndSessionID...

	// It's expected that values less than ~240 should fit in one byte
	// Depending on the varint format, of course

	// Single value tables
	// Multi-byte table IDs aren't a big deal
	SLNLastFileURIBySyncID = 1000, // Every SyncID is a SessionID.
	SLNLastMetaURIBySyncID = 1001,
};


// TODO: Don't use simple assertions for data integrity checks.
// TODO: Accept NULL out parameters in unpack functions.

#define SLNUserByIDKeyPack(val, txn, userID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX + KVS_VARINT_MAX); \
	kvs_bind_uint64((val), SLNUserByID); \
	kvs_bind_uint64((val), (userID)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNUserByIDKeyRange0(range, txn) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX); \
	kvs_bind_uint64((range)->min, SLNUserByID); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
#define SLNUserByIDValPack(val, txn, username, passhash, token, mode, parent, time) \
	KVS_VAL_STORAGE(val, KVS_INLINE_MAX * 3 + KVS_VARINT_MAX * 3); \
	kvs_bind_string((val), (username), (txn)); \
	kvs_bind_string((val), (passhash), (txn)); \
	kvs_bind_string((val), (token), (txn)); \
	kvs_bind_uint64((val), (mode)); \
	kvs_bind_uint64((val), (parent)); \
	kvs_bind_uint64((val), (time)); \
	KVS_VAL_STORAGE_VERIFY(val);
static void SLNUserByIDValUnpack(KVS_val *const val,
                                 KVS_txn *const txn,
                                 strarg_t *const username,
                                 strarg_t *const passhash,
                                 strarg_t *const token,
                                 SLNMode *const mode,
                                 uint64_t *const parent,
                                 uint64_t *const time)
{
	*username = kvs_read_string(val, txn);
	*passhash = kvs_read_string(val, txn);
	*token = kvs_read_string(val, txn);
	*mode = (SLNMode)kvs_read_uint64(val);
	*parent = kvs_read_uint64(val);
	*time = kvs_read_uint64(val);
}

#define SLNUserIDByNameKeyPack(val, txn, username) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX + KVS_INLINE_MAX); \
	kvs_bind_uint64((val), SLNUserIDByName); \
	kvs_bind_string((val), (username), (txn)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNUserIDByNameValPack(val, txn, userID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX); \
	kvs_bind_uint64((val), (userID)); \
	KVS_VAL_STORAGE_VERIFY(val);

#define SLNSessionByIDKeyPack(val, txn, sessionID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX + KVS_VARINT_MAX); \
	kvs_bind_uint64((val), SLNSessionByID); \
	kvs_bind_uint64((val), sessionID); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNSessionByIDValPack(val, txn, userID, sessionHash) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX + KVS_INLINE_MAX); \
	kvs_bind_uint64((val), (userID)); \
	kvs_bind_string((val), (sessionHash), (txn)); \
	KVS_VAL_STORAGE_VERIFY(val);
static void SLNSessionByIDValUnpack(KVS_val *const val,
                                    KVS_txn *const txn,
                                    uint64_t *const userID,
                                    strarg_t *const sessionHash)
{
	*userID = kvs_read_uint64(val);
	*sessionHash = kvs_read_string(val, txn);
}

#define SLNPullByIDKeyPack(val, txn, pullID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX + KVS_VARINT_MAX); \
	kvs_bind_uint64((val), SLNPullByID); \
	kvs_bind_uint64((val), (pullID)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNPullByIDRange0(range, txn) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX); \
	kvs_bind_uint64((range)->min, SLNPullByID); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void SLNPullByIDKeyUnpack(KVS_val *const val, KVS_txn *const txn, uint64_t *const pullID) {
	uint64_t const table = kvs_read_uint64(val);
	assert(SLNPullByID == table);
	*pullID = kvs_read_uint64(val);
}

#define SLNPullByIDValPack(val, txn, userID, certhash, host, path, query, sessionid) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX * 1 + KVS_INLINE_MAX * 5); \
	kvs_bind_uint64((val), (userID)); \
	kvs_bind_string((val), (host), (txn)); \
	kvs_bind_string((val), (sessionid), (txn)); \
	kvs_bind_string((val), (query), (txn)); \
	kvs_bind_string((val), (certhash), (txn)); \
	kvs_bind_string((val), (path), (txn)); \
	KVS_VAL_STORAGE_VERIFY(val);
static void SLNPullByIDValUnpack(KVS_val *const val, KVS_txn *const txn, uint64_t *const userID, strarg_t *const certhash, strarg_t *const host, strarg_t *const path, strarg_t *const query, strarg_t *const sessionid) {
	*userID = kvs_read_uint64(val);
	*host = kvs_read_string(val, txn);
	*sessionid = kvs_read_string(val, txn);
	*query = kvs_read_string(val, txn);
	*certhash = NULL;
	*path = "";
	if(!val->size) return;
	*certhash = kvs_read_string(val, txn);
	*path = kvs_read_string(val, txn);
}

///

#define SLNFileByIDKeyPack(val, txn, fileID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX + KVS_VARINT_MAX); \
	kvs_bind_uint64((val), SLNFileByID); \
	kvs_bind_uint64((val), (fileID)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNFileByIDRange0(range, txn) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX); \
	kvs_bind_uint64((range)->min, SLNFileByID); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
#define SLNFileByIDValPack(val, txn, internalHash, type, size) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX * 1 + KVS_INLINE_MAX * 2); \
	kvs_bind_string((val), (internalHash), (txn)); \
	kvs_bind_string((val), (type), (txn)); \
	kvs_bind_uint64((val), (size)); \
	KVS_VAL_STORAGE_VERIFY(val);

#define SLNFileIDByInfoKeyPack(val, txn, internalHash, type) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX * 1 + KVS_INLINE_MAX * 2); \
	kvs_bind_uint64((val), SLNFileIDByInfo); \
	kvs_bind_string((val), (internalHash), (txn)); \
	kvs_bind_string((val), (type), (txn)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNFileIDByInfoValPack(val, txn, fileID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX); \
	kvs_bind_uint64((val), (fileID)); \
	KVS_VAL_STORAGE_VERIFY(val);

#define SLNFileIDAndURIKeyPack(val, txn, fileID, URI) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX * 2 + KVS_INLINE_MAX * 1); \
	kvs_bind_uint64((val), SLNFileIDAndURI); \
	kvs_bind_uint64((val), (fileID)); \
	kvs_bind_string((val), (URI), (txn)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNFileIDAndURIRange1(range, txn, fileID) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX + KVS_VARINT_MAX); \
	kvs_bind_uint64((range)->min, SLNFileIDAndURI); \
	kvs_bind_uint64((range)->min, (fileID)); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void SLNFileIDAndURIKeyUnpack(KVS_val *const val, KVS_txn *const txn, uint64_t *const fileID, strarg_t *const URI) {
	uint64_t const table = kvs_read_uint64(val);
	assert(SLNFileIDAndURI == table);
	*fileID = kvs_read_uint64(val);
	*URI = kvs_read_string(val, txn);
}

#define SLNURIAndFileIDKeyPack(val, txn, URI, fileID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX * 2 + KVS_INLINE_MAX * 1); \
	kvs_bind_uint64((val), SLNURIAndFileID); \
	kvs_bind_string((val), (URI), (txn)); \
	kvs_bind_uint64((val), (fileID)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNURIAndFileIDRange1(range, txn, URI) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX + KVS_INLINE_MAX); \
	kvs_bind_uint64((range)->min, SLNURIAndFileID); \
	kvs_bind_string((range)->min, (URI), (txn)); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void SLNURIAndFileIDKeyUnpack(KVS_val *const val, KVS_txn *const txn, strarg_t *const URI, uint64_t *const fileID) {
	uint64_t const table = kvs_read_uint64(val);
	assert(SLNURIAndFileID == table);
	*URI = kvs_read_string(val, txn);
	*fileID = kvs_read_uint64(val);
}
static int SLNURIGetFileID(strarg_t const URI, KVS_txn *const txn, uint64_t *const out) {
	// This function is guaranteed safe in the face of collisions,
	// meaning it always returns the oldest known file.
	assert(out);
	if(!URI) return KVS_EINVAL;
	KVS_cursor *cursor = NULL;
	int rc = kvs_txn_cursor(txn, &cursor);
	if(rc < 0) return rc;
	KVS_range files[1];
	KVS_val file[1];
	SLNURIAndFileIDRange1(files, txn, URI);
	rc = kvs_cursor_firstr(cursor, files, file, NULL, +1);
	if(rc < 0) return rc;
	strarg_t u;
	uint64_t fileID;
	SLNURIAndFileIDKeyUnpack(file, txn, &u, &fileID);
	assert(0 == strcmp(URI, u));
	*out = fileID;
	return 0;
}

///

#define SLNMetaFileByIDKeyPack(val, txn, metaFileID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX + KVS_VARINT_MAX); \
	kvs_bind_uint64((val), SLNMetaFileByID); \
	kvs_bind_uint64((val), (metaFileID)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNMetaFileByIDRange0(range, txn) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX); \
	kvs_bind_uint64((range)->min, SLNMetaFileByID); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void SLNMetaFileByIDKeyUnpack(KVS_val *const val, KVS_txn *const txn, uint64_t *const metaFileID) {
	uint64_t const table = kvs_read_uint64(val);
	assert(SLNMetaFileByID == table);
	*metaFileID = kvs_read_uint64(val);
}

#define SLNMetaFileByIDValPack(val, txn, targetURI) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX + KVS_INLINE_MAX); \
	kvs_bind_uint64((val), 0/* Obsolete file ID field. */); \
	kvs_bind_string((val), (targetURI), (txn)); \
	KVS_VAL_STORAGE_VERIFY(val);
static void SLNMetaFileByIDValUnpack(KVS_val *const val, KVS_txn *const txn, strarg_t *const targetURI) {
	(void) kvs_read_uint64(val); // Obsolete file ID field.
	*targetURI = kvs_read_string(val, txn);
}

#define SLNTargetURIAndMetaFileIDKeyPack(val, txn, targetURI, metaFileID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX * 2 + KVS_INLINE_MAX * 1); \
	kvs_bind_uint64((val), SLNTargetURIAndMetaFileID); \
	kvs_bind_string((val), (targetURI), (txn)); \
	kvs_bind_uint64((val), (metaFileID)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNTargetURIAndMetaFileIDRange1(range, txn, targetURI) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX + KVS_INLINE_MAX); \
	kvs_bind_uint64((range)->min, SLNTargetURIAndMetaFileID); \
	kvs_bind_string((range)->min, (targetURI), (txn)); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void SLNTargetURIAndMetaFileIDKeyUnpack(KVS_val *const val, KVS_txn *const txn, strarg_t *const targetURI, uint64_t *const metaFileID) {
	uint64_t const table = kvs_read_uint64(val);
	assert(SLNTargetURIAndMetaFileID == table);
	*targetURI = kvs_read_string(val, txn);
	*metaFileID = kvs_read_uint64(val);
}

#define SLNMetaFileIDFieldAndValueKeyPack(val, txn, metaFileID, field, value) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX * 2 + KVS_INLINE_MAX * 2); \
	kvs_bind_uint64((val), SLNMetaFileIDFieldAndValue); \
	kvs_bind_uint64((val), (metaFileID)); \
	kvs_bind_string((val), (field), (txn)); \
	kvs_bind_string((val), (value), (txn)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNMetaFileIDFieldAndValueRange2(range, txn, metaFileID, field) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX * 2 + KVS_INLINE_MAX * 1); \
	kvs_bind_uint64((range)->min, SLNMetaFileIDFieldAndValue); \
	kvs_bind_uint64((range)->min, (metaFileID)); \
	kvs_bind_string((range)->min, (field), (txn)); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void SLNMetaFileIDFieldAndValueKeyUnpack(KVS_val *const val, KVS_txn *const txn, uint64_t *const metaFileID, strarg_t *const field, strarg_t *const value) {
	uint64_t const table = kvs_read_uint64(val);
	assert(SLNMetaFileIDFieldAndValue == table);
	*metaFileID = kvs_read_uint64(val);
	*field = kvs_read_string(val, txn);
	*value = kvs_read_string(val, txn);
}

#define SLNFieldValueAndMetaFileIDKeyPack(val, txn, field, value, metaFileID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX * 2 + KVS_INLINE_MAX * 2); \
	kvs_bind_uint64((val), SLNFieldValueAndMetaFileID); \
	kvs_bind_string((val), field, (txn)); \
	kvs_bind_string((val), value, (txn)); \
	kvs_bind_uint64((val), (metaFileID)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNFieldValueAndMetaFileIDRange2(range, txn, field, value) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX * 1 + KVS_INLINE_MAX * 2); \
	kvs_bind_uint64((range)->min, SLNFieldValueAndMetaFileID); \
	kvs_bind_string((range)->min, (field), (txn)); \
	kvs_bind_string((range)->min, (value), (txn)); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void SLNFieldValueAndMetaFileIDKeyUnpack(KVS_val *const val, KVS_txn *const txn, strarg_t *const field, strarg_t *const value, uint64_t *const metaFileID) {
	uint64_t const table = kvs_read_uint64(val);
	assert(SLNFieldValueAndMetaFileID == table);
	*field = kvs_read_string(val, txn);
	*value = kvs_read_string(val, txn);
	*metaFileID = kvs_read_uint64(val);
}

#define SLNTermMetaFileIDAndPositionKeyPack(val, txn, token, metaFileID, position) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX * 3 + KVS_INLINE_MAX * 1); \
	kvs_bind_uint64((val), SLNTermMetaFileIDAndPosition); \
	kvs_bind_string((val), (token), (txn)); \
	kvs_bind_uint64((val), (metaFileID)); \
	kvs_bind_uint64((val), (position)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNTermMetaFileIDAndPositionRange1(range, txn, token) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX + KVS_INLINE_MAX); \
	kvs_bind_uint64((range)->min, SLNTermMetaFileIDAndPosition); \
	kvs_bind_string((range)->min, (token), (txn)); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
#define SLNTermMetaFileIDAndPositionRange2(range, txn, token, metaFileID) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX * 2 + KVS_INLINE_MAX * 1); \
	kvs_bind_uint64((range)->min, SLNTermMetaFileIDAndPosition); \
	kvs_bind_string((range)->min, (token), (txn)); \
	kvs_bind_uint64((range)->min, (metaFileID)); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void SLNTermMetaFileIDAndPositionKeyUnpack(KVS_val *const val, KVS_txn *const txn, strarg_t *const token, uint64_t *const metaFileID, uint64_t *const position) {
	uint64_t const table = kvs_read_uint64(val);
	assert(SLNTermMetaFileIDAndPosition == table);
	*token = kvs_read_string(val, txn);
	*metaFileID = kvs_read_uint64(val);
	*position = kvs_read_uint64(val);
}

#define SLNFirstUniqueMetaFileIDKeyPack(val, txn, metaFileID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX * 2); \
	kvs_bind_uint64((val), SLNFirstUniqueMetaFileID); \
	kvs_bind_uint64((val), (metaFileID)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNFirstUniqueMetaFileIDRange0(range, txn) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX); \
	kvs_bind_uint64((range)->min, SLNFirstUniqueMetaFileID); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void SLNFirstUniqueMetaFileIDKeyUnpack(KVS_val *const val, KVS_txn *const txn, uint64_t *const metaFileID) {
	uint64_t const table = kvs_read_uint64(val);
	assert(SLNFirstUniqueMetaFileID == table);
	*metaFileID = kvs_read_uint64(val);
}

///

#define SLNFileIDAndSessionIDKeyPack(val, txn, fileID, sessionID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX*3); \
	kvs_bind_uint64((val), SLNFileIDAndSessionID); \
	kvs_bind_uint64((val), (fileID)); \
	kvs_bind_uint64((val), (sessionID)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNFileIDAndSessionIDRange1(range, txn, fileID) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX*2); \
	kvs_bind_uint64((range)->min, SLNFileIDAndSessionID); \
	kvs_bind_uint64((range)->min, (fileID)); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void SLNFileIDAndSessionIDKeyUnpack(KVS_val *const val, KVS_txn *const txn, uint64_t *const fileID, uint64_t *const sessionID) {
	uint64_t const table = kvs_read_uint64(val);
	assert(SLNFileIDAndSessionID == table);
	*fileID = kvs_read_uint64(val);
	*sessionID = kvs_read_uint64(val);
}

#define SLNSessionIDAndHintIDToMetaURIAndTargetURIKeyPack(val, txn, sessionID, hintID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX*3); \
	kvs_bind_uint64((val), SLNSessionIDAndHintIDToMetaURIAndTargetURI); \
	kvs_bind_uint64((val), (sessionID)); \
	kvs_bind_uint64((val), (hintID)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNSessionIDAndHintIDToMetaURIAndTargetURIRange1(range, txn, sessionID) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX*2); \
	kvs_bind_uint64((range)->min, SLNSessionIDAndHintIDToMetaURIAndTargetURI); \
	kvs_bind_uint64((range)->min, (sessionID)); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void SLNSessionIDAndHintIDToMetaURIAndTargetURIKeyUnpack(KVS_val *const val, KVS_txn *const txn, uint64_t *const sessionID, uint64_t *const hintID) {
	uint64_t const table = kvs_read_uint64(val);
	assert(SLNSessionIDAndHintIDToMetaURIAndTargetURI == table);
	*sessionID = kvs_read_uint64(val);
	*hintID = kvs_read_uint64(val);
}
static uint64_t SLNNextHintID(KVS_txn *const txn, uint64_t const sessionID) {
	KVS_cursor *cursor = NULL;
	int rc = kvs_txn_cursor(txn, &cursor);
	if(rc < 0) return 0;

	KVS_range range[1];
	KVS_val key[1];
	SLNSessionIDAndHintIDToMetaURIAndTargetURIRange1(range, txn, sessionID);
	rc = kvs_cursor_firstr(cursor, range, key, NULL, -1);
	if(KVS_NOTFOUND == rc) return 1;
	if(rc < 0) return 0;
	uint64_t s, lastID;
	SLNSessionIDAndHintIDToMetaURIAndTargetURIKeyUnpack(key, txn, &s, &lastID);
	return lastID+1;
}

#define SLNSessionIDAndHintIDToMetaURIAndTargetURIValPack(val, txn, metaURI, targetURI) \
	KVS_VAL_STORAGE(val, KVS_INLINE_MAX*2); \
	kvs_bind_string((val), (metaURI), (txn)); \
	kvs_bind_string((val), (targetURI), (txn)); \
	KVS_VAL_STORAGE_VERIFY(val);
static void SLNSessionIDAndHintIDToMetaURIAndTargetURIValUnpack(KVS_val *const val, KVS_txn *const txn, strarg_t *const metaURI, strarg_t *const targetURI) {
	*metaURI = kvs_read_string(val, txn);
	*targetURI = kvs_read_string(val, txn);
}

#define SLNMetaURIAndSessionIDToHintIDKeyPack(val, txn, metaURI, sessionID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX*2 + KVS_INLINE_MAX*1); \
	kvs_bind_uint64((val), SLNMetaURIAndSessionIDToHintID); \
	kvs_bind_string((val), (metaURI), (txn)); \
	kvs_bind_uint64((val), (sessionID)); \
	KVS_VAL_STORAGE_VERIFY(val);
static void SLNMetaURIAndSessionIDToHintIDKeyUnpack(KVS_val *const val, KVS_txn *const txn, strarg_t *const metaURI, uint64_t *const sessionID) {
	uint64_t const table = kvs_read_uint64(val);
	assert(SLNMetaURIAndSessionIDToHintID == table);
	*metaURI = kvs_read_string(val, txn);
	*sessionID = kvs_read_uint64(val);
}
#define SLNMetaURIAndSessionIDToHintIDValPack(val, txn, hintID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX); \
	kvs_bind_uint64((val), (hintID)); \
	KVS_VAL_STORAGE_VERIFY(val);


#define SLNTargetURISessionIDAndHintIDKeyPack(val, txn, targetURI, sessionID, hintID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX*3 + KVS_INLINE_MAX*1); \
	kvs_bind_uint64((val), SLNTargetURISessionIDAndHintID); \
	kvs_bind_string((val), (targetURI), (txn)); \
	kvs_bind_uint64((val), (sessionID)); \
	kvs_bind_uint64((val), (hintID)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define SLNTargetURISessionIDAndHintIDRange2(range, txn, targetURI, sessionID) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX*2 + KVS_INLINE_MAX*1); \
	kvs_bind_uint64((range)->min, SLNTargetURISessionIDAndHintID); \
	kvs_bind_string((range)->min, (targetURI), (txn)); \
	kvs_bind_uint64((range)->min, (sessionID)); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void SLNTargetURISessionIDAndHintIDKeyUnpack(KVS_val *const val, KVS_txn *const txn, strarg_t *const targetURI, uint64_t *const sessionID, uint64_t *const hintID) {
	uint64_t const table = kvs_read_uint64(val);
	assert(SLNTargetURISessionIDAndHintID == table);
	*targetURI = kvs_read_string(val, txn);
	*sessionID = kvs_read_uint64(val);
	*hintID = kvs_read_uint64(val);
}

#define SLNSessionIDAndHintsSyncedFileIDKeyPack(val, txn, sessionID, fileID) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX*3); \
	kvs_bind_uint64((val), SLNSessionIDAndHintsSyncedFileID); \
	kvs_bind_uint64((val), (sessionID)); \
	kvs_bind_uint64((val), (fileID)); \
	KVS_VAL_STORAGE_VERIFY(val);

static int kvs_cursor_renew(KVS_txn *const txn, KVS_cursor **const out) {
	assert(out);
	if(*out) return 0;
	return kvs_cursor_open(txn, out);
}

