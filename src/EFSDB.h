#include "db/db_schema.h"

enum {
	// Note: these IDs are part of the persistent database format,
	// so don't just go changing them.

	// 0-19 are reserved for the DB layer.

	EFSUserByID = 20,
	EFSUserIDByName = 21,
	EFSSessionByID = 22,
	EFSPullByID = 23, // by user ID?

	EFSFileByID = 40,
	EFSFileIDByInfo = 41,
	EFSFileIDByType = 42,
	EFSFileIDAndURI = 43,
	EFSURIAndFileID = 44,

	EFSMetaFileByID = 60,
	EFSFileIDAndMetaFileID = 61,
	EFSTargetURIAndMetaFileID = 62,
	EFSMetaFileIDFieldAndValue = 63,
	EFSFieldValueAndMetaFileID = 64,
	EFSTermMetaFileIDAndPosition = 65,
};


// TODO: Don't use simple assertions for data integrity checks.

#define EFSUserByIDKeyPack(val, txn, userID) \
	uint8_t __buf_##val[DB_VARINT_MAX + DB_VARINT_MAX]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), EFSUserByID); \
	db_bind_uint64((val), (userID));
#define EFSUserByIDValPack(val, txn, username, passhash, token) \
	uint8_t __buf_##val[DB_INLINE_MAX * 3]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_string((txn), (val), (username)); \
	db_bind_string((txn), (val), (passhash)); \
	db_bind_string((txn), (val), (token));

#define EFSUserIDByNameKeyPack(val, txn, username) \
	uint8_t __buf_##val[DB_VARINT_MAX + DB_INLINE_MAX]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), EFSUserIDByName); \
	db_bind_string((txn), (val), (username));
#define EFSUserIDByNameValPack(val, txn, userID) \
	uint8_t __buf_##val[DB_VARINT_MAX]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), (userID));


#define EFSSessionByIDKeyPack(val, txn, sessionID) \
	uint8_t __buf_##val[DB_VARINT_MAX + DB_VARINT_MAX]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), EFSSessionByID); \
	db_bind_uint64((val), sessionID);
#define EFSSessionByIDValPack(val, txn, userID, sessionHash) \
	uint8_t __buf_##val[DB_VARINT_MAX + DB_INLINE_MAX]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), (userID)); \
	db_bind_string((txn), (val), (sessionHash));


#define EFSPullByIDKeyPack(val, txn, pullID) \
	uint8_t __buf_##val[DB_VARINT_MAX + DB_VARINT_MAX]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), EFSPullByID); \
	db_bind_uint64((val), (pullID));
#define EFSPullByIDRange0(range, txn) \
	uint8_t __buf_min_##range[DB_VARINT_MAX]; \
	uint8_t __buf_max_##range[DB_VARINT_MAX]; \
	*(range) = (DB_range){ {{ 0, __buf_min_##range }}, {{ 0, __buf_max_##range }} }; \
	db_bind_uint64((range)->min, EFSPullByID); \
	db_range_genmax((range));
#define EFSPullByIDKeyUnpack(val, txn, pullID) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSPullByID == table); \
	*(pullID) = db_read_uint64((val)); \
})

#define EFSPullByIDValPack(val, txn, userID, host, username, password, cookie, query) \
	uint8_t __buf_##val[DB_VARINT_MAX * 1 + DB_INLINE_MAX * 5]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), (userID)); \
	db_bind_string((txn), (val), (host)); \
	db_bind_string((txn), (val), (username)); \
	db_bind_string((txn), (val), (password)); \
	db_bind_string((txn), (val), (cookie)); \
	db_bind_string((txn), (val), (query));
#define EFSPullByIDValUnpack(val, txn, userID, host, username, password, cookie, query) ({ \
	*(userID) = db_read_uint64((val)); \
	*(host) = db_read_string((txn), (val)); \
	*(username) = db_read_string((txn), (val)); \
	*(password) = db_read_string((txn), (val)); \
	*(cookie) = db_read_string((txn), (val)); \
	*(query) = db_read_string((txn), (val)); \
})



#define EFSFileByIDKeyPack(val, txn, fileID) \
	uint8_t __buf_##val[DB_VARINT_MAX + DB_VARINT_MAX]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), EFSFileByID); \
	db_bind_uint64((val), (fileID));

#define EFSFileByIDValPack(val, txn, internalHash, type, size) \
	uint8_t __buf_##val[DB_VARINT_MAX * 1 + DB_INLINE_MAX * 2]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_string((txn), (val), (internalHash)); \
	db_bind_string((txn), (val), (type)); \
	db_bind_uint64((val), (size));


#define EFSFileIDByInfoKeyPack(val, txn, internalHash, type) \
	uint8_t __buf_##val[DB_VARINT_MAX * 1 + DB_INLINE_MAX * 2]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), EFSFileIDByInfo); \
	db_bind_string((txn), (val), (internalHash)); \
	db_bind_string((txn), (val), (type));
#define EFSFileIDByInfoValPack(val, txn, fileID) \
	uint8_t __buf_##val[DB_VARINT_MAX]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), (fileID));


#define EFSFileIDAndURIKeyPack(val, txn, fileID, URI) \
	uint8_t __buf_##val[DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), EFSFileIDAndURI); \
	db_bind_uint64((val), (fileID)); \
	db_bind_string((txn), (val), (URI));
#define EFSFileIDAndURIRange1(range, txn, fileID) \
	uint8_t __buf_min_##range[DB_VARINT_MAX + DB_VARINT_MAX]; \
	uint8_t __buf_max_##range[DB_VARINT_MAX + DB_VARINT_MAX]; \
	*(range) = (DB_range){ {{ 0, __buf_min_##range }}, {{ 0, __buf_max_##range }} }; \
	db_bind_uint64((range)->min, EFSFileIDAndURI); \
	db_bind_uint64((range)->min, (fileID)); \
	db_range_genmax((range));
#define EFSFileIDAndURIKeyUnpack(val, txn, fileID, URI) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSFileIDAndURI == table); \
	*(fileID) = db_read_uint64((val)); \
	*(URI) = db_read_string((txn), (val)); \
})


#define EFSURIAndFileIDKeyPack(val, txn, URI, fileID) \
	uint8_t __buf_##val[DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), EFSURIAndFileID); \
	db_bind_string((txn), (val), (URI)); \
	db_bind_uint64((val), (fileID));
#define EFSURIAndFileIDRange1(range, txn, URI) \
	uint8_t __buf_min_##range[DB_VARINT_MAX + DB_INLINE_MAX]; \
	uint8_t __buf_max_##range[DB_VARINT_MAX + DB_INLINE_MAX]; \
	*(range) = (DB_range){ {{ 0, __buf_min_##range }}, {{ 0, __buf_max_##range }} }; \
	db_bind_uint64((range)->min, EFSURIAndFileID); \
	db_bind_string((txn), (range)->min, (URI)); \
	db_range_genmax((range));
#define EFSURIAndFileIDKeyUnpack(val, txn, URI, fileID) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSURIAndFileID == table); \
	*(URI) = db_read_string((txn), (val)); \
	*(fileID) = db_read_uint64((val)); \
})



#define EFSMetaFileByIDKeyPack(val, txn, metaFileID) \
	uint8_t __buf_##val[DB_VARINT_MAX + DB_VARINT_MAX]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), EFSMetaFileByID); \
	db_bind_uint64((val), (metaFileID));
#define EFSMetaFileByIDRange0(range, txn) \
	uint8_t __buf_min_##range[DB_VARINT_MAX]; \
	uint8_t __buf_max_##range[DB_VARINT_MAX]; \
	*(range) = (DB_range){ {{ 0, __buf_min_##range }}, {{ 0, __buf_max_##range }} }; \
	db_bind_uint64((range)->min, EFSMetaFileByID); \
	db_range_genmax((range));
#define EFSMetaFileByIDKeyUnpack(val, txn, metaFileID) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSMetaFileByID == table); \
	*(metaFileID) = db_read_uint64((val)); \
})


#define EFSMetaFileByIDValPack(val, txn, fileID, targetURI) \
	uint8_t __buf_##val[DB_VARINT_MAX + DB_INLINE_MAX]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), (fileID)); \
	db_bind_string((txn), (val), (targetURI));
#define EFSMetaFileByIDValUnpack(val, txn, fileID, targetURI) ({ \
	*(fileID) = db_read_uint64((val)); \
	*(targetURI) = db_read_string((txn), (val)); \
})


#define EFSFileIDAndMetaFileIDKeyPack(val, txn, fileID, metaFileID) \
	uint8_t __buf_##val[DB_VARINT_MAX * 3]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), EFSFileIDAndMetaFileID); \
	db_bind_uint64((val), (fileID)); \
	db_bind_uint64((val), (metaFileID));
#define EFSFileIDAndMetaFileIDRange1(range, txn, fileID) \
	uint8_t __buf_min_##range[DB_VARINT_MAX + DB_VARINT_MAX]; \
	uint8_t __buf_max_##range[DB_VARINT_MAX + DB_VARINT_MAX]; \
	*(range) = (DB_range){ {{ 0, __buf_min_##range }}, {{ 0, __buf_max_##range }} }; \
	db_bind_uint64((range)->min, EFSFileIDAndMetaFileID); \
	db_bind_uint64((range)->min, (fileID)); \
	db_range_genmax((range));
#define EFSFileIDAndMetaFileIDKeyUnpack(val, txn, fileID, metaFileID) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSFileIDAndMetaFileID == table); \
	*(fileID) = db_read_uint64((val)); \
	*(metaFileID) = db_read_uint64((val)); \
})


#define EFSTargetURIAndMetaFileIDKeyPack(val, txn, targetURI, metaFileID) \
	uint8_t __buf_##val[DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), EFSTargetURIAndMetaFileID); \
	db_bind_string((txn), (val), (targetURI)); \
	db_bind_uint64((val), (metaFileID));
#define EFSTargetURIAndMetaFileIDRange1(range, txn, targetURI) \
	uint8_t __buf_min_##range[DB_VARINT_MAX + DB_INLINE_MAX]; \
	uint8_t __buf_max_##range[DB_VARINT_MAX + DB_INLINE_MAX]; \
	*(range) = (DB_range){ {{ 0, __buf_min_##range }}, {{ 0, __buf_max_##range }} }; \
	db_bind_uint64((range)->min, EFSTargetURIAndMetaFileID); \
	db_bind_string((txn), (range)->min, (targetURI)); \
	db_range_genmax((range));
#define EFSTargetURIAndMetaFileIDKeyUnpack(val, txn, targetURI, metaFileID) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSTargetURIAndMetaFileID == table); \
	*(targetURI) = db_read_string((txn), (val)); \
	*(metaFileID) = db_read_uint64((val)); \
})



#define EFSMetaFileIDFieldAndValueKeyPack(val, txn, metaFileID, field, value) \
	uint8_t __buf_##val[DB_VARINT_MAX * 2 + DB_INLINE_MAX * 2]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), EFSMetaFileIDFieldAndValue); \
	db_bind_uint64((val), (metaFileID)); \
	db_bind_string((txn), (val), (field)); \
	db_bind_string((txn), (val), (value));
#define EFSMetaFileIDFieldAndValueRange2(range, txn, metaFileID, field) \
	uint8_t __buf_min_##range[DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1]; \
	uint8_t __buf_max_##range[DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1]; \
	*(range) = (DB_range){ {{ 0, __buf_min_##range }}, {{ 0, __buf_max_##range }} }; \
	db_bind_uint64((range)->min, EFSMetaFileIDFieldAndValue); \
	db_bind_uint64((range)->min, (metaFileID)); \
	db_bind_string((txn), (range)->min, (field)); \
	db_range_genmax((range));
#define EFSMetaFileIDFieldAndValueKeyUnpack(val, txn, metaFileID, field, value) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSMetaFileIDFieldAndValue == table); \
	*(metaFileID) = db_read_uint64((val)); \
	*(field) = db_read_string(txn, (val)); \
	*(value) = db_read_string(txn, (val)); \
})



#define EFSFieldValueAndMetaFileIDKeyPack(val, txn, field, value, metaFileID) \
	uint8_t __buf_##val[DB_VARINT_MAX * 2 + DB_INLINE_MAX * 2]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), EFSFieldValueAndMetaFileID); \
	db_bind_string((txn), (val), field); \
	db_bind_string((txn), (val), value); \
	db_bind_uint64((val), (metaFileID));

#define EFSFieldValueAndMetaFileIDRange2(range, txn, field, value) \
	uint8_t __buf_min_##range[DB_VARINT_MAX * 1 + DB_INLINE_MAX * 2]; \
	uint8_t __buf_max_##range[DB_VARINT_MAX * 1 + DB_INLINE_MAX * 2]; \
	*(range) = (DB_range){ {{ 0, __buf_min_##range }}, {{ 0, __buf_max_##range }} }; \
	db_bind_uint64((range)->min, EFSFieldValueAndMetaFileID); \
	db_bind_string((txn), (range)->min, (field)); \
	db_bind_string((txn), (range)->min, (value)); \
	db_range_genmax((range));

#define EFSFieldValueAndMetaFileIDKeyUnpack(val, txn, field, value, metaFileID) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSFieldValueAndMetaFileID == table); \
	*(field) = db_read_string((txn), (val)); \
	*(value) = db_read_string((txn), (val)); \
	*(metaFileID) = db_read_uint64((val)); \
})



#define EFSTermMetaFileIDAndPositionKeyPack(val, txn, token, metaFileID, position) \
	uint8_t __buf_##val[DB_VARINT_MAX * 3 + DB_INLINE_MAX * 1]; \
	*(val) = (DB_val){ 0, __buf_##val }; \
	db_bind_uint64((val), EFSTermMetaFileIDAndPosition); \
	db_bind_string((txn), (val), (token)); \
	db_bind_uint64((val), (metaFileID)); \
	db_bind_uint64((val), (position));
#define EFSTermMetaFileIDAndPositionRange1(range, txn, token) \
	uint8_t __buf_min_##range[DB_VARINT_MAX + DB_INLINE_MAX]; \
	uint8_t __buf_max_##range[DB_VARINT_MAX + DB_INLINE_MAX]; \
	*(range) = (DB_range){ {{ 0, __buf_min_##range }}, {{ 0, __buf_max_##range }} }; \
	db_bind_uint64((range)->min, EFSTermMetaFileIDAndPosition); \
	db_bind_string((txn), (range)->min, (token)); \
	db_range_genmax((range));
#define EFSTermMetaFileIDAndPositionRange2(range, txn, token, metaFileID) \
	uint8_t __buf_min_##range[DB_VARINT_MAX + DB_INLINE_MAX]; \
	uint8_t __buf_max_##range[DB_VARINT_MAX + DB_INLINE_MAX]; \
	*(range) = (DB_range){ {{ 0, __buf_min_##range }}, {{ 0, __buf_max_##range }} }; \
	db_bind_uint64((range)->min, EFSTermMetaFileIDAndPosition); \
	db_bind_string((txn), (range)->min, (token)); \
	db_bind_uint64((range)->min, (metaFileID)); \
	db_range_genmax((range));
#define EFSTermMetaFileIDAndPositionKeyUnpack(val, txn, token, metaFileID, position) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSTermMetaFileIDAndPosition == table); \
	*(token) = db_read_string((txn), (val)); \
	*(metaFileID) = db_read_uint64((val)); \
	*(position) = db_read_uint64((val)); \
})





















