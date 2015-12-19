// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <openssl/sha.h>
#include "util/pass.h"
#include "StrongLink.h"
#include "SLNDB.h"

#define USER_MIN 2
#define USER_MAX 32
#define PASS_MIN 0
#define PASS_MAX 72

struct SLNSession {
	unsigned refcount; // atomic
	SLNSessionCacheRef cache;
	uint64_t sessionID;
	byte_t *sessionKeyRaw;
	byte_t *sessionKeyEnc;
	uint64_t userID;
	SLNMode mode;
	str_t *username;
};

int SLNSessionCreateInternal(SLNSessionCacheRef const cache, uint64_t const sessionID, byte_t const *const sessionKeyRaw, byte_t const *const sessionKeyEnc, uint64_t const userID, SLNMode const mode_trusted, strarg_t const username, SLNSessionRef *const out) {
	assert(cache);
	assert(out);
	// 0 == sessionID is valid for "temporary" sessions.
	if(!mode_trusted) return UV_EACCES; // Even trusted code can't create a session without perms.

	SLNSessionRef session = calloc(1, sizeof(struct SLNSession));
	if(!session) return UV_ENOMEM;
	int rc = 0;

	session->refcount = 1;
	session->cache = cache;
	session->sessionID = sessionID;

	session->sessionKeyRaw = malloc(SESSION_KEY_LEN);
	session->sessionKeyEnc = malloc(SESSION_KEY_LEN);
	if(!session->sessionKeyRaw || !session->sessionKeyEnc) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;
	if(sessionKeyRaw) {
		memcpy(session->sessionKeyRaw, sessionKeyRaw, SESSION_KEY_LEN);
	} else {
		FREE(&session->sessionKeyRaw);
	}
	if(sessionKeyEnc) {
		memcpy(session->sessionKeyEnc, sessionKeyEnc, SESSION_KEY_LEN);
	} else if(sessionKeyRaw) {
		byte_t buf[SHA256_DIGEST_LENGTH];
		SHA256(sessionKeyRaw, SESSION_KEY_LEN, buf);
		memcpy(session->sessionKeyEnc, buf, SESSION_KEY_LEN);
	} else {
		FREE(&session->sessionKeyEnc);
	}

	session->userID = userID;
	session->mode = mode_trusted;
	session->username = username ? strdup(username) : NULL;

	*out = session; session = NULL;
cleanup:
	SLNSessionRelease(&session);
	return rc;
}
SLNSessionRef SLNSessionRetain(SLNSessionRef const session) {
	if(!session) return NULL;
	assert(session->refcount);
	session->refcount++;
	return session;
}
void SLNSessionRelease(SLNSessionRef *const sessionptr) {
	SLNSessionRef session = *sessionptr;
	if(!session) return;
	assert(session->refcount);
	if(--session->refcount) {
		*sessionptr = NULL;
		return;
	}
	session->cache = NULL;
	session->sessionID = 0;
	FREE(&session->sessionKeyRaw);
	FREE(&session->sessionKeyEnc);
	session->userID = 0;
	session->mode = 0;
	FREE(&session->username);
	assert_zeroed(session, 1);
	FREE(sessionptr); session = NULL;
}

SLNSessionCacheRef SLNSessionGetCache(SLNSessionRef const session) {
	if(!session) return NULL;
	return session->cache;
}
SLNRepoRef SLNSessionGetRepo(SLNSessionRef const session) {
	if(!session) return NULL;
	return SLNSessionCacheGetRepo(session->cache);
}
uint64_t SLNSessionGetID(SLNSessionRef const session) {
	if(!session) return 0;
	return session->sessionID;
}
int SLNSessionKeyValid(SLNSessionRef const session, byte_t const *const enc) {
	if(!session) return UV_EINVAL;
	if(!enc) return UV_EINVAL;
	if(!session->sessionKeyEnc) return UV_EACCES; // ???

	// Note: We don't need to use constant-time comparison here
	// because we are comparing hashes of the key.
	if(0 != memcmp(enc, session->sessionKeyEnc, SESSION_KEY_LEN)) return UV_EACCES;
	return 0;
}
uint64_t SLNSessionGetUserID(SLNSessionRef const session) {
	if(!session) return -1;
	return session->userID;
}
bool SLNSessionHasPermission(SLNSessionRef const session, SLNMode const mask) {
	if(!session) return false;
	return (mask & session->mode) == mask;
}
strarg_t SLNSessionGetUsername(SLNSessionRef const session) {
	if(!session) return NULL;
	return session->username;
}
str_t *SLNSessionCopyCookie(SLNSessionRef const session) {
	if(!session) return NULL;
	if(!session->sessionKeyRaw) return NULL;
	str_t hex[SESSION_KEY_HEX+1];
	tohex(hex, session->sessionKeyRaw, SESSION_KEY_LEN);
	hex[SESSION_KEY_HEX] = '\0';
	return aasprintf("s=%llu:%s", (unsigned long long)session->sessionID, hex);
}

int SLNSessionDBOpen(SLNSessionRef const session, SLNMode const mode, DB_env **const dbptr) {
	if(!SLNSessionHasPermission(session, mode)) return DB_EACCES;
	assert(session);
	SLNRepoDBOpenUnsafe(SLNSessionGetRepo(session), dbptr);
	return 0;
}
void SLNSessionDBClose(SLNSessionRef const session, DB_env **const dbptr) {
	assert(session);
	SLNRepoDBClose(SLNSessionGetRepo(session), dbptr);
}


int SLNSessionCreateUser(SLNSessionRef const session, DB_txn *const txn, strarg_t const username, strarg_t const password) {
	SLNRepoRef const repo = SLNSessionGetRepo(session);
	SLNMode const mode = SLNRepoGetRegistrationMode(repo);
	return SLNSessionCreateUserInternal(session, txn, username, password, mode);
}
int SLNSessionCreateUserInternal(SLNSessionRef const session, DB_txn *const txn, strarg_t const username, strarg_t const password, SLNMode const mode_unsafe) {
	if(!session) return DB_EINVAL;
	if(!txn) return DB_EINVAL;
	if(!username) return DB_EINVAL;
	if(!password) return DB_EINVAL;
	size_t const ulen = strlen(username);
	size_t const plen = strlen(password);
	if(ulen < USER_MIN || ulen > USER_MAX) return DB_EINVAL;
	if(plen < PASS_MIN || plen > PASS_MAX) return DB_EINVAL;

	SLNMode const mode = mode_unsafe & session->mode;
	if(!mode) return DB_EINVAL;
	uint64_t const parent = session->userID;
	uint64_t const time = uv_now(async_loop); // TODO: Appropriate timestamp?

	uint64_t const userID = db_next_id(SLNUserByID, txn);
	if(!userID) return DB_EACCES;
	str_t *passhash = pass_hash(password);
	if(!passhash) return DB_ENOMEM;

	DB_val username_key[1], userID_val[1];
	SLNUserIDByNameKeyPack(username_key, txn, username);
	SLNUserIDByNameValPack(userID_val, txn, userID);
	int rc = db_put(txn, username_key, userID_val, DB_NOOVERWRITE);
	if(rc < 0) return rc;

	DB_val userID_key[1], user_val[1];
	SLNUserByIDKeyPack(userID_key, txn, userID);
	SLNUserByIDValPack(user_val, txn, username, passhash, NULL, mode, parent, time);
	rc = db_put(txn, userID_key, user_val, DB_NOOVERWRITE);
	if(rc < 0) return rc;

	return 0;
}
int SLNSessionCreateSession(SLNSessionRef const session, SLNSessionRef *const out) {
	assert(out);
	if(!session) return DB_EACCES;

	SLNSessionCacheRef const cache = session->cache;
	uint64_t const userID = session->userID;
	SLNMode const mode = session->mode;
	strarg_t const username = session->username;
	DB_env *db = NULL;
	DB_txn *txn = NULL;
	SLNSessionRef alt = NULL;

	byte_t key_raw[SESSION_KEY_LEN];
	int rc = async_random(key_raw, sizeof(key_raw));
	if(rc < 0) goto cleanup;

	byte_t key_enc[SHA256_DIGEST_LENGTH];
	SHA256(key_raw, sizeof(key_raw), key_enc);

	str_t key_str[SESSION_KEY_HEX+1];
	tohex(key_str, key_enc, SESSION_KEY_LEN);
	key_str[SESSION_KEY_HEX] = '\0';

	rc = SLNSessionDBOpen(session, SLN_RDWR, &db); // TODO: Custom permission?
	if(rc < 0) goto cleanup;
	rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
	if(rc < 0) goto cleanup;

	uint64_t const sessionID = db_next_id(SLNSessionByID, txn);
	DB_val key[1], val[1];
	SLNSessionByIDKeyPack(key, txn, sessionID);
	SLNSessionByIDValPack(val, txn, userID, key_str);
	rc = db_put(txn, key, val, DB_NOOVERWRITE_FAST);
	if(rc < 0) goto cleanup;

	rc = db_txn_commit(txn); txn = NULL;
	SLNSessionDBClose(session, &db);
	if(rc < 0) goto cleanup;

	rc = SLNSessionCreateInternal(cache, sessionID, key_raw, key_enc, userID, mode, username, &alt);
	if(rc < 0) goto cleanup;

	*out = alt; alt = NULL;

cleanup:
	db_txn_abort(txn); txn = NULL;
	SLNSessionDBClose(session, &db);
	SLNSessionRelease(&alt);
	return rc;
}

int SLNSessionGetFileInfo(SLNSessionRef const session, strarg_t const URI, SLNFileInfo *const info) {
	DB_env *db = NULL;
	DB_txn *txn = NULL;
	DB_cursor *cursor = NULL;
	int rc;

	rc = SLNSessionDBOpen(session, SLN_RDONLY, &db);
	if(rc < 0) goto cleanup;
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(rc < 0) goto cleanup;
	rc = db_txn_cursor(txn, &cursor);
	if(rc < 0) goto cleanup;

	uint64_t fileID = 0;
	DB_val file_val[1];
	rc = SLNURIGetFileID(URI, txn, &fileID);
	if(rc < 0) goto cleanup;
	if(!info) {
		rc = 0;
		goto cleanup;
	}

	DB_val fileID_key[1];
	SLNFileByIDKeyPack(fileID_key, txn, fileID);
	rc = db_get(txn, fileID_key, file_val);
	if(rc < 0) goto cleanup;
	strarg_t const internalHash = db_read_string(file_val, txn);
	strarg_t const type = db_read_string(file_val, txn);
	uint64_t const size = db_read_uint64(file_val);

	// Clear padding for later assert_zeroed.
	memset(info, 0, sizeof(*info));
	info->hash = strdup(internalHash);
	info->path = SLNRepoCopyInternalPath(SLNSessionGetRepo(session), internalHash);
	info->type = strdup(type);
	info->size = size;
	if(!info->hash || !info->path || !info->type) {
		SLNFileInfoCleanup(info);
		rc = DB_ENOMEM;
		goto cleanup;
	}

cleanup:
	cursor = NULL; // txn-cursor doesn't need to be closed.
	db_txn_abort(txn); txn = NULL;
	SLNSessionDBClose(session, &db);
	return rc;
}
void SLNFileInfoCleanup(SLNFileInfo *const info) {
	if(!info) return;
	FREE(&info->hash);
	FREE(&info->path);
	FREE(&info->type);
	info->size = 0;
	assert_zeroed(info, 1);
}

int SLNSessionCopyLastSubmissionURIs(SLNSessionRef const session, str_t *const outFileURI, str_t *const outMetaURI) {
	DB_env *db = NULL;
	DB_txn *txn = NULL;
	int rc = SLNSessionDBOpen(session, SLN_RDWR, &db);
	if(rc < 0) goto cleanup;
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(rc < 0) goto cleanup;

	if(outFileURI) {
		DB_val key[1], val[1];
		DB_VAL_STORAGE(key, DB_VARINT_MAX*2);
		db_bind_uint64(key, SLNLastFileURIBySessionID);
		db_bind_uint64(key, session->sessionID);
		DB_VAL_STORAGE_VERIFY(key);
		rc = db_get(txn, key, val);
		if(rc >= 0) {
			strarg_t const URI = db_read_string(val, txn);
			strlcpy(outFileURI, URI, SLN_URI_MAX);
		} else if(DB_NOTFOUND == rc) {
			outFileURI[0] = '\0';
			rc = 0;
		} else {
			goto cleanup;
		}
	}
	if(outMetaURI) {
		DB_val key[1], val[1];
		DB_VAL_STORAGE(key, DB_VARINT_MAX*2);
		db_bind_uint64(key, SLNLastMetaURIBySessionID);
		db_bind_uint64(key, session->sessionID);
		DB_VAL_STORAGE_VERIFY(key);
		rc = db_get(txn, key, val);
		if(rc >= 0) {
			strarg_t const URI = db_read_string(val, txn);
			strlcpy(outMetaURI, URI, SLN_URI_MAX);
		} else if(DB_NOTFOUND == rc) {
			outMetaURI[0] = '\0';
			rc = 0;
		} else {
			goto cleanup;
		}
	}

cleanup:
	db_txn_abort(txn); txn = NULL;
	SLNSessionDBClose(session, &db);
	return rc;
}


int SLNSessionGetValueForField(SLNSessionRef const session, DB_txn *const txn, strarg_t const fileURI, strarg_t const field, str_t *out, size_t const max) {
	int rc = 0;
	DB_cursor *metafiles = NULL;
	DB_cursor *values = NULL;

	rc = db_cursor_open(txn, &metafiles);
	if(rc < 0) goto done;
	rc = db_cursor_open(txn, &values);
	if(rc < 0) goto done;

	DB_range metaFileIDs[1];
	SLNTargetURIAndMetaFileIDRange1(metaFileIDs, txn, fileURI);
	DB_val metaFileID_key[1];
	rc = db_cursor_firstr(metafiles, metaFileIDs, metaFileID_key, NULL, +1);
	if(rc < 0 && DB_NOTFOUND != rc) goto done;
	for(; rc >= 0; rc = db_cursor_nextr(metafiles, metaFileIDs, metaFileID_key, NULL, +1)) {
		strarg_t u;
		uint64_t metaFileID;
		SLNTargetURIAndMetaFileIDKeyUnpack(metaFileID_key, txn, &u, &metaFileID);
		assert(0 == strcmp(fileURI, u));
		DB_range vrange[1];
		SLNMetaFileIDFieldAndValueRange2(vrange, txn, metaFileID, field);
		DB_val value_val[1];
		rc = db_cursor_firstr(values, vrange, value_val, NULL, +1);
		if(rc < 0 && DB_NOTFOUND != rc) goto done;
		for(; rc >= 0; rc = db_cursor_nextr(values, vrange, value_val, NULL, +1)) {
			uint64_t m;
			strarg_t f, v;
			SLNMetaFileIDFieldAndValueKeyUnpack(value_val, txn, &m, &f, &v);
			assert(metaFileID == m);
			assert(0 == strcmp(field, f));
			if(!v || '\0' == v[0]) continue;
			strlcpy(out, v, max);
			goto done;
		}
	}

done:
	db_cursor_close(values); values = NULL;
	db_cursor_close(metafiles); metafiles = NULL;
	return rc;
}

int SLNSessionGetNextMetaMapURI(SLNSessionRef const session, strarg_t const targetURI, uint64_t *const metaMapID, str_t *out, size_t const max) {
	// TODO: We should handle URI synonyms.
	// That might mean accepting a fileID instead of targetURI...

	assert(metaMapID);
	assert(out);

	uint64_t const sessionID = SLNSessionGetID(session);
	DB_env *db = NULL;
	DB_txn *txn = NULL;
	DB_cursor *cursor = NULL;
	int rc = 0;
	size_t count = 0;

	rc = SLNSessionDBOpen(session, SLN_RDONLY, &db);
	if(rc < 0) goto cleanup;
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(rc < 0) goto cleanup;

	rc = db_cursor_open(txn, &cursor);
	if(rc < 0) goto cleanup;

	DB_range range[1];
	DB_val key[1];
	SLNTargetURISessionIDAndMetaMapIDRange2(range, txn, targetURI, sessionID);
	SLNTargetURISessionIDAndMetaMapIDKeyPack(key, txn, targetURI, sessionID, *metaMapID);
	rc = db_cursor_seekr(cursor, range, key, NULL, +1);
	if(rc < 0) goto cleanup;

	strarg_t u;
	uint64_t s;
	SLNTargetURISessionIDAndMetaMapIDKeyUnpack(key, txn, &u, &s, metaMapID);

	DB_val row[1], val[1];
	SLNSessionIDAndMetaMapIDToMetaURIAndTargetURIKeyPack(row, txn, sessionID, *metaMapID);
	rc = db_get(txn, row, val);
	if(rc < 0) goto cleanup;

	strarg_t metaURI, t;
	SLNSessionIDAndMetaMapIDToMetaURIAndTargetURIValUnpack(val, txn, &metaURI, &t);
	db_assert(metaURI);

	strlcpy(out, metaURI, max); // TODO: Handle err

cleanup:
	db_cursor_close(cursor); cursor = NULL;
	db_txn_abort(txn); txn = NULL;
	SLNSessionDBClose(session, &db);
	if(rc < 0) return rc;
	return count;
}
int SLNSessionAddMetaMap(SLNSessionRef const session, strarg_t const metaURI, strarg_t const targetURI) {
	uint64_t const sessionID = SLNSessionGetID(session);

	DB_env *db = NULL;
	DB_txn *txn = NULL;
	int rc = SLNSessionDBOpen(session, SLN_RDWR, &db);
	if(rc < 0) goto cleanup;
	rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
	if(rc < 0) goto cleanup;

	uint64_t nextID = SLNNextMetaMapID(txn, sessionID);
	if(!nextID) rc = DB_EIO;
	if(rc < 0) goto cleanup;

	DB_val mainkey[1], mainval[1];
	SLNSessionIDAndMetaMapIDToMetaURIAndTargetURIKeyPack(mainkey, txn, sessionID, nextID);
	SLNSessionIDAndMetaMapIDToMetaURIAndTargetURIValPack(mainval, txn, metaURI, targetURI);
	rc = db_put(txn, mainkey, mainval, DB_NOOVERWRITE_FAST);
	if(rc < 0) goto cleanup;

	DB_val fwdkey[1], fwdval[1];
	SLNMetaURIAndSessionIDToMetaMapIDKeyPack(fwdkey, txn, metaURI, sessionID);
	SLNMetaURIAndSessionIDToMetaMapIDValPack(fwdval, txn, nextID);
	rc = db_put(txn, fwdkey, fwdval, DB_NOOVERWRITE_FAST);
	if(rc < 0) goto cleanup;

	DB_val revkey[1], revval[1];
	SLNTargetURISessionIDAndMetaMapIDKeyPack(revkey, txn, targetURI, sessionID, nextID);
	db_nullval(revval);
	rc = db_put(txn, revkey, revval, DB_NOOVERWRITE_FAST);
	if(rc < 0) goto cleanup;

	rc = db_txn_commit(txn); txn = NULL;
cleanup:
	db_txn_abort(txn); txn = NULL;
	SLNSessionDBClose(session, &db);
	return rc;
}

