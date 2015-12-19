// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include "db/db_schema.h"

enum {
	// Note: these IDs are part of the persistent database format,
	// so don't just go changing them.

	// 0-19 are reserved for the DB layer.

	SLNUserByID = 20,
	SLNUserIDByName = 21,
	SLNSessionByID = 22,
	SLNPullByID = 23, // Also by user ID?

	SLNFileByID = 40,
	SLNFileIDByInfo = 41,
//	SLNFileIDByType = 42, // TODO
	SLNFileIDAndURI = 43,
	SLNURIAndFileID = 44,

	SLNMetaFileByID = 60,
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

	// It's expected that values less than ~240 should fit in one byte
	// Depending on the varint format, of course

	// Single value tables
	// Multi-byte table IDs aren't a big deal
	SLNLastFileURIBySessionID = 1000,
	SLNLastMetaURIBySessionID = 1001,
};


// TODO: Don't use simple assertions for data integrity checks.
// TODO: Accept NULL out parameters in unpack functions.

#define SLNUserByIDKeyPack(val, txn, userID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX + DB_VARINT_MAX); \
	db_bind_uint64((val), SLNUserByID); \
	db_bind_uint64((val), (userID)); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNUserByIDKeyRange0(range, txn) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX); \
	db_bind_uint64((range)->min, SLNUserByID); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
#define SLNUserByIDValPack(val, txn, username, passhash, token, mode, parent, time) \
	DB_VAL_STORAGE(val, DB_INLINE_MAX * 3 + DB_VARINT_MAX * 3); \
	db_bind_string((val), (username), (txn)); \
	db_bind_string((val), (passhash), (txn)); \
	db_bind_string((val), (token), (txn)); \
	db_bind_uint64((val), (mode)); \
	db_bind_uint64((val), (parent)); \
	db_bind_uint64((val), (time)); \
	DB_VAL_STORAGE_VERIFY(val);
static void SLNUserByIDValUnpack(DB_val *const val,
                                 DB_txn *const txn,
                                 strarg_t *const username,
                                 strarg_t *const passhash,
                                 strarg_t *const token,
                                 SLNMode *const mode,
                                 uint64_t *const parent,
                                 uint64_t *const time)
{
	*username = db_read_string(val, txn);
	*passhash = db_read_string(val, txn);
	*token = db_read_string(val, txn);
	*mode = (SLNMode)db_read_uint64(val);
	*parent = db_read_uint64(val);
	*time = db_read_uint64(val);
}

#define SLNUserIDByNameKeyPack(val, txn, username) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX + DB_INLINE_MAX); \
	db_bind_uint64((val), SLNUserIDByName); \
	db_bind_string((val), (username), (txn)); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNUserIDByNameValPack(val, txn, userID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX); \
	db_bind_uint64((val), (userID)); \
	DB_VAL_STORAGE_VERIFY(val);

#define SLNSessionByIDKeyPack(val, txn, sessionID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX + DB_VARINT_MAX); \
	db_bind_uint64((val), SLNSessionByID); \
	db_bind_uint64((val), sessionID); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNSessionByIDValPack(val, txn, userID, sessionHash) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX + DB_INLINE_MAX); \
	db_bind_uint64((val), (userID)); \
	db_bind_string((val), (sessionHash), (txn)); \
	DB_VAL_STORAGE_VERIFY(val);
static void SLNSessionByIDValUnpack(DB_val *const val,
                                    DB_txn *const txn,
                                    uint64_t *const userID,
                                    strarg_t *const sessionHash)
{
	*userID = db_read_uint64(val);
	*sessionHash = db_read_string(val, txn);
}

#define SLNPullByIDKeyPack(val, txn, pullID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX + DB_VARINT_MAX); \
	db_bind_uint64((val), SLNPullByID); \
	db_bind_uint64((val), (pullID)); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNPullByIDRange0(range, txn) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX); \
	db_bind_uint64((range)->min, SLNPullByID); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
static void SLNPullByIDKeyUnpack(DB_val *const val, DB_txn *const txn, uint64_t *const pullID) {
	uint64_t const table = db_read_uint64(val);
	assert(SLNPullByID == table);
	*pullID = db_read_uint64(val);
}

#define SLNPullByIDValPack(val, txn, userID, certhash, host, path, query, sessionid) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 1 + DB_INLINE_MAX * 5); \
	db_bind_uint64((val), (userID)); \
	db_bind_string((val), (host), (txn)); \
	db_bind_string((val), (sessionid), (txn)); \
	db_bind_string((val), (query), (txn)); \
	db_bind_string((val), (certhash), (txn)); \
	db_bind_string((val), (path), (txn)); \
	DB_VAL_STORAGE_VERIFY(val);
static void SLNPullByIDValUnpack(DB_val *const val, DB_txn *const txn, uint64_t *const userID, strarg_t *const certhash, strarg_t *const host, strarg_t *const path, strarg_t *const query, strarg_t *const sessionid) {
	*userID = db_read_uint64(val);
	*host = db_read_string(val, txn);
	*sessionid = db_read_string(val, txn);
	*query = db_read_string(val, txn);
	*certhash = NULL;
	*path = "";
	if(!val->size) return;
	*certhash = db_read_string(val, txn);
	*path = db_read_string(val, txn);
}

///

#define SLNFileByIDKeyPack(val, txn, fileID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX + DB_VARINT_MAX); \
	db_bind_uint64((val), SLNFileByID); \
	db_bind_uint64((val), (fileID)); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNFileByIDRange0(range, txn) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX); \
	db_bind_uint64((range)->min, SLNFileByID); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
#define SLNFileByIDValPack(val, txn, internalHash, type, size) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 1 + DB_INLINE_MAX * 2); \
	db_bind_string((val), (internalHash), (txn)); \
	db_bind_string((val), (type), (txn)); \
	db_bind_uint64((val), (size)); \
	DB_VAL_STORAGE_VERIFY(val);

#define SLNFileIDByInfoKeyPack(val, txn, internalHash, type) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 1 + DB_INLINE_MAX * 2); \
	db_bind_uint64((val), SLNFileIDByInfo); \
	db_bind_string((val), (internalHash), (txn)); \
	db_bind_string((val), (type), (txn)); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNFileIDByInfoValPack(val, txn, fileID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX); \
	db_bind_uint64((val), (fileID)); \
	DB_VAL_STORAGE_VERIFY(val);

#define SLNFileIDAndURIKeyPack(val, txn, fileID, URI) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1); \
	db_bind_uint64((val), SLNFileIDAndURI); \
	db_bind_uint64((val), (fileID)); \
	db_bind_string((val), (URI), (txn)); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNFileIDAndURIRange1(range, txn, fileID) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX + DB_VARINT_MAX); \
	db_bind_uint64((range)->min, SLNFileIDAndURI); \
	db_bind_uint64((range)->min, (fileID)); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
static void SLNFileIDAndURIKeyUnpack(DB_val *const val, DB_txn *const txn, uint64_t *const fileID, strarg_t *const URI) {
	uint64_t const table = db_read_uint64(val);
	assert(SLNFileIDAndURI == table);
	*fileID = db_read_uint64(val);
	*URI = db_read_string(val, txn);
}

#define SLNURIAndFileIDKeyPack(val, txn, URI, fileID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1); \
	db_bind_uint64((val), SLNURIAndFileID); \
	db_bind_string((val), (URI), (txn)); \
	db_bind_uint64((val), (fileID)); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNURIAndFileIDRange1(range, txn, URI) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX + DB_INLINE_MAX); \
	db_bind_uint64((range)->min, SLNURIAndFileID); \
	db_bind_string((range)->min, (URI), (txn)); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
static void SLNURIAndFileIDKeyUnpack(DB_val *const val, DB_txn *const txn, strarg_t *const URI, uint64_t *const fileID) {
	uint64_t const table = db_read_uint64(val);
	assert(SLNURIAndFileID == table);
	*URI = db_read_string(val, txn);
	*fileID = db_read_uint64(val);
}
static int SLNURIGetFileID(strarg_t const URI, DB_txn *const txn, uint64_t *const out) {
	// This function is guaranteed safe in the face of collisions,
	// meaning it always returns the oldest known file.
	assert(out);
	if(!URI) return DB_EINVAL;
	DB_cursor *cursor = NULL;
	int rc = db_txn_cursor(txn, &cursor);
	if(rc < 0) return rc;
	DB_range files[1];
	DB_val file[1];
	SLNURIAndFileIDRange1(files, txn, URI);
	rc = db_cursor_firstr(cursor, files, file, NULL, +1);
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
	DB_VAL_STORAGE(val, DB_VARINT_MAX + DB_VARINT_MAX); \
	db_bind_uint64((val), SLNMetaFileByID); \
	db_bind_uint64((val), (metaFileID)); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNMetaFileByIDRange0(range, txn) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX); \
	db_bind_uint64((range)->min, SLNMetaFileByID); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
static void SLNMetaFileByIDKeyUnpack(DB_val *const val, DB_txn *const txn, uint64_t *const metaFileID) {
	uint64_t const table = db_read_uint64(val);
	assert(SLNMetaFileByID == table);
	*metaFileID = db_read_uint64(val);
}

#define SLNMetaFileByIDValPack(val, txn, targetURI) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX + DB_INLINE_MAX); \
	db_bind_uint64((val), 0/* Obsolete file ID field. */); \
	db_bind_string((val), (targetURI), (txn)); \
	DB_VAL_STORAGE_VERIFY(val);
static void SLNMetaFileByIDValUnpack(DB_val *const val, DB_txn *const txn, strarg_t *const targetURI) {
	(void) db_read_uint64(val); // Obsolete file ID field.
	*targetURI = db_read_string(val, txn);
}

#define SLNTargetURIAndMetaFileIDKeyPack(val, txn, targetURI, metaFileID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1); \
	db_bind_uint64((val), SLNTargetURIAndMetaFileID); \
	db_bind_string((val), (targetURI), (txn)); \
	db_bind_uint64((val), (metaFileID)); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNTargetURIAndMetaFileIDRange1(range, txn, targetURI) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX + DB_INLINE_MAX); \
	db_bind_uint64((range)->min, SLNTargetURIAndMetaFileID); \
	db_bind_string((range)->min, (targetURI), (txn)); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
static void SLNTargetURIAndMetaFileIDKeyUnpack(DB_val *const val, DB_txn *const txn, strarg_t *const targetURI, uint64_t *const metaFileID) {
	uint64_t const table = db_read_uint64(val);
	assert(SLNTargetURIAndMetaFileID == table);
	*targetURI = db_read_string(val, txn);
	*metaFileID = db_read_uint64(val);
}

#define SLNMetaFileIDFieldAndValueKeyPack(val, txn, metaFileID, field, value) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 2); \
	db_bind_uint64((val), SLNMetaFileIDFieldAndValue); \
	db_bind_uint64((val), (metaFileID)); \
	db_bind_string((val), (field), (txn)); \
	db_bind_string((val), (value), (txn)); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNMetaFileIDFieldAndValueRange2(range, txn, metaFileID, field) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1); \
	db_bind_uint64((range)->min, SLNMetaFileIDFieldAndValue); \
	db_bind_uint64((range)->min, (metaFileID)); \
	db_bind_string((range)->min, (field), (txn)); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
static void SLNMetaFileIDFieldAndValueKeyUnpack(DB_val *const val, DB_txn *const txn, uint64_t *const metaFileID, strarg_t *const field, strarg_t *const value) {
	uint64_t const table = db_read_uint64(val);
	assert(SLNMetaFileIDFieldAndValue == table);
	*metaFileID = db_read_uint64(val);
	*field = db_read_string(val, txn);
	*value = db_read_string(val, txn);
}

#define SLNFieldValueAndMetaFileIDKeyPack(val, txn, field, value, metaFileID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 2); \
	db_bind_uint64((val), SLNFieldValueAndMetaFileID); \
	db_bind_string((val), field, (txn)); \
	db_bind_string((val), value, (txn)); \
	db_bind_uint64((val), (metaFileID)); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNFieldValueAndMetaFileIDRange2(range, txn, field, value) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX * 1 + DB_INLINE_MAX * 2); \
	db_bind_uint64((range)->min, SLNFieldValueAndMetaFileID); \
	db_bind_string((range)->min, (field), (txn)); \
	db_bind_string((range)->min, (value), (txn)); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
static void SLNFieldValueAndMetaFileIDKeyUnpack(DB_val *const val, DB_txn *const txn, strarg_t *const field, strarg_t *const value, uint64_t *const metaFileID) {
	uint64_t const table = db_read_uint64(val);
	assert(SLNFieldValueAndMetaFileID == table);
	*field = db_read_string(val, txn);
	*value = db_read_string(val, txn);
	*metaFileID = db_read_uint64(val);
}

#define SLNTermMetaFileIDAndPositionKeyPack(val, txn, token, metaFileID, position) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 3 + DB_INLINE_MAX * 1); \
	db_bind_uint64((val), SLNTermMetaFileIDAndPosition); \
	db_bind_string((val), (token), (txn)); \
	db_bind_uint64((val), (metaFileID)); \
	db_bind_uint64((val), (position)); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNTermMetaFileIDAndPositionRange1(range, txn, token) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX + DB_INLINE_MAX); \
	db_bind_uint64((range)->min, SLNTermMetaFileIDAndPosition); \
	db_bind_string((range)->min, (token), (txn)); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
#define SLNTermMetaFileIDAndPositionRange2(range, txn, token, metaFileID) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1); \
	db_bind_uint64((range)->min, SLNTermMetaFileIDAndPosition); \
	db_bind_string((range)->min, (token), (txn)); \
	db_bind_uint64((range)->min, (metaFileID)); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
static void SLNTermMetaFileIDAndPositionKeyUnpack(DB_val *const val, DB_txn *const txn, strarg_t *const token, uint64_t *const metaFileID, uint64_t *const position) {
	uint64_t const table = db_read_uint64(val);
	assert(SLNTermMetaFileIDAndPosition == table);
	*token = db_read_string(val, txn);
	*metaFileID = db_read_uint64(val);
	*position = db_read_uint64(val);
}

#define SLNFirstUniqueMetaFileIDKeyPack(val, txn, metaFileID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 2); \
	db_bind_uint64((val), SLNFirstUniqueMetaFileID); \
	db_bind_uint64((val), (metaFileID)); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNFirstUniqueMetaFileIDRange0(range, txn) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX); \
	db_bind_uint64((range)->min, SLNFirstUniqueMetaFileID); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
static void SLNFirstUniqueMetaFileIDKeyUnpack(DB_val *const val, DB_txn *const txn, uint64_t *const metaFileID) {
	uint64_t const table = db_read_uint64(val);
	assert(SLNFirstUniqueMetaFileID == table);
	*metaFileID = db_read_uint64(val);
}

///

#define SLNFileIDAndSessionIDKeyPack(val, txn, fileID, sessionID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX*3); \
	db_bind_uint64((val), SLNFileIDAndSessionID); \
	db_bind_uint64((val), (fileID)); \
	db_bind_uint64((val), (sessionID)); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNFileIDAndSessionIDRange1(range, txn, fileID) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX*2); \
	db_bind_uint64((range)->min, SLNFileIDAndSessionID); \
	db_bind_uint64((range)->min, (fileID)); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
static void SLNFileIDAndSessionIDKeyUnpack(DB_val *const val, DB_txn *const txn, uint64_t *const fileID, uint64_t *const sessionID) {
	uint64_t const table = db_read_uint64(val);
	assert(SLNFileIDAndSessionID == table);
	*fileID = db_read_uint64(val);
	*sessionID = db_read_uint64(val);
}

#define SLNSessionIDAndHintIDToMetaURIAndTargetURIKeyPack(val, txn, sessionID, hintID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX*3); \
	db_bind_uint64((val), SLNSessionIDAndHintIDToMetaURIAndTargetURI); \
	db_bind_uint64((val), (sessionID)); \
	db_bind_uint64((val), (hintID)); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNSessionIDAndHintIDToMetaURIAndTargetURIRange1(range, txn, sessionID) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX*2); \
	db_bind_uint64((range)->min, SLNSessionIDAndHintIDToMetaURIAndTargetURI); \
	db_bind_uint64((range)->min, (sessionID)); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
static void SLNSessionIDAndHintIDToMetaURIAndTargetURIKeyUnpack(DB_val *const val, DB_txn *const txn, uint64_t *const sessionID, uint64_t *const hintID) {
	uint64_t const table = db_read_uint64(val);
	assert(SLNSessionIDAndHintIDToMetaURIAndTargetURI == table);
	*sessionID = db_read_uint64(val);
	*hintID = db_read_uint64(val);
}
static uint64_t SLNNextHintID(DB_txn *const txn, uint64_t const sessionID) {
	DB_cursor *cursor = NULL;
	int rc = db_txn_cursor(txn, &cursor);
	if(rc < 0) return 0;

	DB_range range[1];
	DB_val key[1];
	SLNSessionIDAndHintIDToMetaURIAndTargetURIRange1(range, txn, sessionID);
	rc = db_cursor_firstr(cursor, range, key, NULL, -1);
	if(DB_NOTFOUND == rc) return 1;
	if(rc < 0) return 0;
	uint64_t s, lastID;
	SLNSessionIDAndHintIDToMetaURIAndTargetURIKeyUnpack(key, txn, &s, &lastID);
	return lastID+1;
}

#define SLNSessionIDAndHintIDToMetaURIAndTargetURIValPack(val, txn, metaURI, targetURI) \
	DB_VAL_STORAGE(val, DB_INLINE_MAX*2); \
	db_bind_string((val), (metaURI), (txn)); \
	db_bind_string((val), (targetURI), (txn)); \
	DB_VAL_STORAGE_VERIFY(val);
static void SLNSessionIDAndHintIDToMetaURIAndTargetURIValUnpack(DB_val *const val, DB_txn *const txn, strarg_t *const metaURI, strarg_t *const targetURI) {
	*metaURI = db_read_string(val, txn);
	*targetURI = db_read_string(val, txn);
}

#define SLNMetaURIAndSessionIDToHintIDKeyPack(val, txn, metaURI, sessionID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX*2 + DB_INLINE_MAX*1); \
	db_bind_uint64((val), SLNMetaURIAndSessionIDToHintID); \
	db_bind_string((val), (metaURI), (txn)); \
	db_bind_uint64((val), (sessionID)); \
	DB_VAL_STORAGE_VERIFY(val);
static void SLNMetaURIAndSessionIDToHintIDKeyUnpack(DB_val *const val, DB_txn *const txn, strarg_t *const metaURI, uint64_t *const sessionID) {
	uint64_t const table = db_read_uint64(val);
	assert(SLNMetaURIAndSessionIDToHintID == table);
	*metaURI = db_read_string(val, txn);
	*sessionID = db_read_uint64(val);
}
#define SLNMetaURIAndSessionIDToHintIDValPack(val, txn, hintID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX); \
	db_bind_uint64((val), (hintID)); \
	DB_VAL_STORAGE_VERIFY(val);


#define SLNTargetURISessionIDAndHintIDKeyPack(val, txn, targetURI, sessionID, hintID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX*3 + DB_INLINE_MAX*1); \
	db_bind_uint64((val), SLNTargetURISessionIDAndHintID); \
	db_bind_string((val), (targetURI), (txn)); \
	db_bind_uint64((val), (sessionID)); \
	db_bind_uint64((val), (hintID)); \
	DB_VAL_STORAGE_VERIFY(val);
#define SLNTargetURISessionIDAndHintIDRange2(range, txn, targetURI, sessionID) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX*2 + DB_INLINE_MAX*1); \
	db_bind_uint64((range)->min, SLNTargetURISessionIDAndHintID); \
	db_bind_string((range)->min, (targetURI), (txn)); \
	db_bind_uint64((range)->min, (sessionID)); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
static void SLNTargetURISessionIDAndHintIDKeyUnpack(DB_val *const val, DB_txn *const txn, strarg_t *const targetURI, uint64_t *const sessionID, uint64_t *const hintID) {
	uint64_t const table = db_read_uint64(val);
	assert(SLNTargetURISessionIDAndHintID == table);
	*targetURI = db_read_string(val, txn);
	*sessionID = db_read_uint64(val);
	*hintID = db_read_uint64(val);
}

