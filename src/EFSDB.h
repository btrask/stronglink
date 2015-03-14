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

	// It's expected that values less than ~240 should fit in one byte
	// Depending on the varint format, of course
};


// TODO: Don't use simple assertions for data integrity checks.
// TODO: Accept NULL out parameters in unpack macros

#define EFSUserByIDKeyPack(val, txn, userID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX + DB_VARINT_MAX); \
	db_bind_uint64((val), EFSUserByID); \
	db_bind_uint64((val), (userID));
#define EFSUserByIDValPack(val, txn, username, passhash, token) \
	DB_VAL_STORAGE(val, DB_INLINE_MAX * 3); \
	db_bind_string((val), (username), (txn)); \
	db_bind_string((val), (passhash), (txn)); \
	db_bind_string((val), (token), (txn));

#define EFSUserIDByNameKeyPack(val, txn, username) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX + DB_INLINE_MAX); \
	db_bind_uint64((val), EFSUserIDByName); \
	db_bind_string((val), (username), (txn));
#define EFSUserIDByNameValPack(val, txn, userID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX); \
	db_bind_uint64((val), (userID));

#define EFSSessionByIDKeyPack(val, txn, sessionID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX + DB_VARINT_MAX); \
	db_bind_uint64((val), EFSSessionByID); \
	db_bind_uint64((val), sessionID);
#define EFSSessionByIDValPack(val, txn, userID, sessionHash) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX + DB_INLINE_MAX); \
	db_bind_uint64((val), (userID)); \
	db_bind_string((val), (sessionHash), (txn));

#define EFSPullByIDKeyPack(val, txn, pullID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX + DB_VARINT_MAX); \
	db_bind_uint64((val), EFSPullByID); \
	db_bind_uint64((val), (pullID));
#define EFSPullByIDRange0(range, txn) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX); \
	db_bind_uint64((range)->min, EFSPullByID); \
	db_range_genmax((range));
#define EFSPullByIDKeyUnpack(val, txn, pullID) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSPullByID == table); \
	*(pullID) = db_read_uint64((val)); \
})

#define EFSPullByIDValPack(val, txn, userID, host, username, password, cookie, query) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 1 + DB_INLINE_MAX * 5); \
	db_bind_uint64((val), (userID)); \
	db_bind_string((val), (host), (txn)); \
	db_bind_string((val), (username), (txn)); \
	db_bind_string((val), (password), (txn)); \
	db_bind_string((val), (cookie), (txn)); \
	db_bind_string((val), (query), (txn));
#define EFSPullByIDValUnpack(val, txn, userID, host, username, password, cookie, query) ({ \
	*(userID) = db_read_uint64((val)); \
	*(host) = db_read_string((val), (txn)); \
	*(username) = db_read_string((val), (txn)); \
	*(password) = db_read_string((val), (txn)); \
	*(cookie) = db_read_string((val), (txn)); \
	*(query) = db_read_string((val), (txn)); \
})

#define EFSFileByIDKeyPack(val, txn, fileID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX + DB_VARINT_MAX); \
	db_bind_uint64((val), EFSFileByID); \
	db_bind_uint64((val), (fileID));
#define EFSFileByIDValPack(val, txn, internalHash, type, size) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 1 + DB_INLINE_MAX * 2); \
	db_bind_string((val), (internalHash), (txn)); \
	db_bind_string((val), (type), (txn)); \
	db_bind_uint64((val), (size));

#define EFSFileIDByInfoKeyPack(val, txn, internalHash, type) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 1 + DB_INLINE_MAX * 2); \
	db_bind_uint64((val), EFSFileIDByInfo); \
	db_bind_string((val), (internalHash), (txn)); \
	db_bind_string((val), (type), (txn));
#define EFSFileIDByInfoValPack(val, txn, fileID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX); \
	db_bind_uint64((val), (fileID));

#define EFSFileIDAndURIKeyPack(val, txn, fileID, URI) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1); \
	db_bind_uint64((val), EFSFileIDAndURI); \
	db_bind_uint64((val), (fileID)); \
	db_bind_string((val), (URI), (txn));
#define EFSFileIDAndURIRange1(range, txn, fileID) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX + DB_VARINT_MAX); \
	db_bind_uint64((range)->min, EFSFileIDAndURI); \
	db_bind_uint64((range)->min, (fileID)); \
	db_range_genmax((range));
#define EFSFileIDAndURIKeyUnpack(val, txn, fileID, URI) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSFileIDAndURI == table); \
	*(fileID) = db_read_uint64((val)); \
	*(URI) = db_read_string((val), (txn)); \
})

#define EFSURIAndFileIDKeyPack(val, txn, URI, fileID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1); \
	db_bind_uint64((val), EFSURIAndFileID); \
	db_bind_string((val), (URI), (txn)); \
	db_bind_uint64((val), (fileID));
#define EFSURIAndFileIDRange1(range, txn, URI) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX + DB_INLINE_MAX); \
	db_bind_uint64((range)->min, EFSURIAndFileID); \
	db_bind_string((range)->min, (URI), (txn)); \
	db_range_genmax((range));
#define EFSURIAndFileIDKeyUnpack(val, txn, URI, fileID) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSURIAndFileID == table); \
	*(URI) = db_read_string((val), (txn)); \
	*(fileID) = db_read_uint64((val)); \
})

#define EFSMetaFileByIDKeyPack(val, txn, metaFileID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX + DB_VARINT_MAX); \
	db_bind_uint64((val), EFSMetaFileByID); \
	db_bind_uint64((val), (metaFileID));
#define EFSMetaFileByIDRange0(range, txn) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX); \
	db_bind_uint64((range)->min, EFSMetaFileByID); \
	db_range_genmax((range));
#define EFSMetaFileByIDKeyUnpack(val, txn, metaFileID) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSMetaFileByID == table); \
	*(metaFileID) = db_read_uint64((val)); \
})

#define EFSMetaFileByIDValPack(val, txn, fileID, targetURI) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX + DB_INLINE_MAX); \
	db_bind_uint64((val), (fileID)); \
	db_bind_string((val), (targetURI), (txn));
#define EFSMetaFileByIDValUnpack(val, txn, fileID, targetURI) ({ \
	*(fileID) = db_read_uint64((val)); \
	*(targetURI) = db_read_string((val), (txn)); \
})

#define EFSFileIDAndMetaFileIDKeyPack(val, txn, fileID, metaFileID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 3); \
	db_bind_uint64((val), EFSFileIDAndMetaFileID); \
	db_bind_uint64((val), (fileID)); \
	db_bind_uint64((val), (metaFileID));
#define EFSFileIDAndMetaFileIDRange1(range, txn, fileID) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX + DB_VARINT_MAX); \
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
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1); \
	db_bind_uint64((val), EFSTargetURIAndMetaFileID); \
	db_bind_string((val), (targetURI), (txn)); \
	db_bind_uint64((val), (metaFileID));
#define EFSTargetURIAndMetaFileIDRange1(range, txn, targetURI) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX + DB_INLINE_MAX); \
	db_bind_uint64((range)->min, EFSTargetURIAndMetaFileID); \
	db_bind_string((range)->min, (targetURI), (txn)); \
	db_range_genmax((range));
#define EFSTargetURIAndMetaFileIDKeyUnpack(val, txn, targetURI, metaFileID) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSTargetURIAndMetaFileID == table); \
	*(targetURI) = db_read_string((val), (txn)); \
	*(metaFileID) = db_read_uint64((val)); \
})

#define EFSMetaFileIDFieldAndValueKeyPack(val, txn, metaFileID, field, value) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 2); \
	db_bind_uint64((val), EFSMetaFileIDFieldAndValue); \
	db_bind_uint64((val), (metaFileID)); \
	db_bind_string((val), (field), (txn)); \
	db_bind_string((val), (value), (txn));
#define EFSMetaFileIDFieldAndValueRange2(range, txn, metaFileID, field) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1); \
	db_bind_uint64((range)->min, EFSMetaFileIDFieldAndValue); \
	db_bind_uint64((range)->min, (metaFileID)); \
	db_bind_string((range)->min, (field), (txn)); \
	db_range_genmax((range));
#define EFSMetaFileIDFieldAndValueKeyUnpack(val, txn, metaFileID, field, value) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSMetaFileIDFieldAndValue == table); \
	*(metaFileID) = db_read_uint64((val)); \
	*(field) = db_read_string((val), (txn)); \
	*(value) = db_read_string((val), (txn)); \
})

#define EFSFieldValueAndMetaFileIDKeyPack(val, txn, field, value, metaFileID) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 2); \
	db_bind_uint64((val), EFSFieldValueAndMetaFileID); \
	db_bind_string((val), field, (txn)); \
	db_bind_string((val), value, (txn)); \
	db_bind_uint64((val), (metaFileID));
#define EFSFieldValueAndMetaFileIDRange2(range, txn, field, value) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX * 1 + DB_INLINE_MAX * 2); \
	db_bind_uint64((range)->min, EFSFieldValueAndMetaFileID); \
	db_bind_string((range)->min, (field), (txn)); \
	db_bind_string((range)->min, (value), (txn)); \
	db_range_genmax((range));
#define EFSFieldValueAndMetaFileIDKeyUnpack(val, txn, field, value, metaFileID) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSFieldValueAndMetaFileID == table); \
	*(field) = db_read_string((val), (txn)); \
	*(value) = db_read_string((val), (txn)); \
	*(metaFileID) = db_read_uint64((val)); \
})

#define EFSTermMetaFileIDAndPositionKeyPack(val, txn, token, metaFileID, position) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX * 3 + DB_INLINE_MAX * 1); \
	db_bind_uint64((val), EFSTermMetaFileIDAndPosition); \
	db_bind_string((val), (token), (txn)); \
	db_bind_uint64((val), (metaFileID)); \
	db_bind_uint64((val), (position));
#define EFSTermMetaFileIDAndPositionRange1(range, txn, token) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX + DB_INLINE_MAX); \
	db_bind_uint64((range)->min, EFSTermMetaFileIDAndPosition); \
	db_bind_string((range)->min, (token), (txn)); \
	db_range_genmax((range));
#define EFSTermMetaFileIDAndPositionRange2(range, txn, token, metaFileID) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1); \
	db_bind_uint64((range)->min, EFSTermMetaFileIDAndPosition); \
	db_bind_string((range)->min, (token), (txn)); \
	db_bind_uint64((range)->min, (metaFileID)); \
	db_range_genmax((range));
#define EFSTermMetaFileIDAndPositionKeyUnpack(val, txn, token, metaFileID, position) ({ \
	uint64_t const table = db_read_uint64((val)); \
	assert(EFSTermMetaFileIDAndPosition == table); \
	*(token) = db_read_string((val), (txn)); \
	*(metaFileID) = db_read_uint64((val)); \
	*(position) = db_read_uint64((val)); \
})

