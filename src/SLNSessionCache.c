// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <openssl/sha.h>
#include "../deps/smhasher/MurmurHash3.h"
#include "util/pass.h"
#include "StrongLink.h"
#include "SLNDB.h"

#define SEARCH_DIST 16

#define EXPIRE_TIMEOUT (1000 * 60 * 4)
#define SWEEP_DELAY (1000 * 60 * 1)

uint32_t SLNSeed = 0;

struct SLNSessionCache {
	SLNRepoRef repo;
	SLNSessionRef public;

	async_mutex_t lock[1];
	uint16_t size;
	uint16_t pos;
	uint64_t *ids;
	SLNSessionRef *sessions;
	uv_timer_t timer[1];
	uint64_t *timeouts;
	uint16_t *active;
};



int SLNSessionCacheCreate(SLNRepoRef const repo, uint16_t const size, SLNSessionCacheRef *const out) {
	if(!repo) return UV_EINVAL;
	if(size < 10) return UV_EINVAL;
	SLNSessionCacheRef cache = calloc(1, sizeof(struct SLNSessionCache));
	if(!cache) return UV_ENOMEM;
	int rc = 0;

	cache->repo = repo;
	cache->public = NULL;
	SLNMode const pub_mode = SLNRepoGetPublicMode(repo);
	if(pub_mode) {
		cache->public = SLNSessionCreateInternal(cache, 0, NULL, NULL, 0, pub_mode, NULL);
		if(!cache->public) rc = UV_ENOMEM;
		if(rc < 0) goto cleanup;
	}

	async_mutex_init(cache->lock, 0);
	cache->size = size;
	cache->ids = calloc(size, sizeof(uint64_t));
	cache->sessions = calloc(size, sizeof(*cache->sessions));
	if(!cache->ids || !cache->sessions) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	cache->timer->data = cache;
	uv_timer_init(async_loop, cache->timer);
	cache->timeouts = calloc(size, sizeof(*cache->timeouts));
	cache->active = calloc(size, sizeof(*cache->active));
	cache->pos = 0;
	if(!cache->timeouts || !cache->active) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	*out = cache; cache = NULL;
cleanup:
	SLNSessionCacheFree(&cache);
	return rc;
}
void SLNSessionCacheFree(SLNSessionCacheRef *const cacheptr) {
	SLNSessionCacheRef cache = *cacheptr;
	if(!cache) return;

	cache->repo = NULL;
	SLNSessionRelease(&cache->public);

	async_mutex_destroy(cache->lock);
	FREE(&cache->ids);
	for(uint16_t i = 0; i < cache->size; i++) {
		SLNSessionRelease(&cache->sessions[i]);
	}
	assert_zeroed(cache->sessions, cache->size);
	FREE(&cache->sessions);
	cache->size = 0;

	async_close((uv_handle_t *)cache->timer);
	FREE(&cache->timeouts);
	FREE(&cache->active);
	cache->pos = 0;

	assert_zeroed(cache, 1);
	FREE(cacheptr); cache = NULL;
}

SLNRepoRef SLNSessionCacheGetRepo(SLNSessionCacheRef const cache) {
	if(!cache) return NULL;
	return cache->repo;
}

static uint16_t session_pos(SLNSessionCacheRef const cache, uint64_t const sessionID) {
	uint32_t hash;
	MurmurHash3_x86_32(&sessionID, sizeof(sessionID), SLNSeed, &hash);
	return hash % cache->size;
}
static void session_cache(SLNSessionCacheRef const cache, SLNSessionRef const session) {
	uint64_t const id = SLNSessionGetID(session);
	uint16_t const pos = session_pos(cache, id);
	uint16_t i = pos;
//	for(; i < pos+SEARCH_DIST; i++) {
		uint16_t const x = i % cache->size;
		if(id == cache->ids[x]) return;
//		if(0 != cache->ids[x]) continue; // TODO: Hack to work without session expiration.
		cache->ids[x] = id;
		SLNSessionRelease(&cache->sessions[x]);
		cache->sessions[x] = SLNSessionRetain(session);
		cache->active[cache->pos] = x;
		cache->timeouts[cache->pos] = uv_now(async_loop) + EXPIRE_TIMEOUT;
//		cache->pos++; // TODO: Is this a ring buffer?
		// TODO: Start timer if necessary.
		return;
//	}
}


int SLNSessionCacheCreateSession(SLNSessionCacheRef const cache, strarg_t const username, strarg_t const password, SLNSessionRef *const out) {
	assert(out);
	if(!cache) return DB_EINVAL;
	if(!username) return DB_EINVAL;
	if(!password) return DB_EINVAL;

	SLNRepoRef const repo = cache->repo;
	DB_env *db = NULL;
	DB_txn *txn = NULL;
	SLNMode mode = 0;
	str_t *passhash = NULL;
	byte_t key_raw[SESSION_KEY_LEN] = {0};
	byte_t key_enc[SHA256_DIGEST_LENGTH] = {0};
	int rc;

	SLNRepoDBOpenUnsafe(repo, &db);
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(rc < 0) goto cleanup;

	DB_val username_key[1], userID_val[1];
	SLNUserIDByNameKeyPack(username_key, txn, username);
	rc = db_get(txn, username_key, userID_val);
	if(rc < 0) goto cleanup;
	uint64_t const userID = db_read_uint64(userID_val);
	db_assert(userID);

	DB_val userID_key[1], user_val[1];
	SLNUserByIDKeyPack(userID_key, txn, userID);
	rc = db_get(txn, userID_key, user_val);
	if(rc < 0) goto cleanup;
	strarg_t u, p, ignore1;
	uint64_t ignore2, ignore3;
	SLNUserByIDValUnpack(user_val, txn, &u, &p, &ignore1, &mode, &ignore2, &ignore3);
	db_assert(0 == strcmp(username, u));
	passhash = strdup(p);

	db_txn_abort(txn); txn = NULL;
	SLNRepoDBClose(repo, &db);
	// TODO: We shouldn't need to close and reopen the DB.
	// However, async_random currently isn't thread-safe.
	// Or it might be, but we should ensure it instead of just
	// counting on LibreSSL.


	if(!mode) rc = DB_EACCES;
	if(rc < 0) goto cleanup;
	if(0 != pass_hashcmp(password, passhash)) rc = DB_EACCES;
	if(rc < 0) goto cleanup;

	FREE(&passhash);

	rc = async_random(key_raw, sizeof(key_raw));
	if(rc < 0) goto cleanup;

	SHA256(key_raw, sizeof(key_raw), key_enc);

	str_t key_str[SESSION_KEY_HEX+1];
	tohex(key_str, key_enc, SESSION_KEY_LEN);
	key_str[SESSION_KEY_HEX] = '\0';


	SLNRepoDBOpenUnsafe(repo, &db);
	rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
	if(rc < 0) goto cleanup;

	uint64_t const sessionID = db_next_id(SLNSessionByID, txn);
	DB_val sessionID_key[1], session_val[1];
	SLNSessionByIDKeyPack(sessionID_key, txn, sessionID);
	SLNSessionByIDValPack(session_val, txn, userID, key_str);
	rc = db_put(txn, sessionID_key, session_val, DB_NOOVERWRITE_FAST);
	if(rc < 0) goto cleanup;

	rc = db_txn_commit(txn); txn = NULL;
	SLNRepoDBClose(repo, &db);
	if(rc < 0) goto cleanup;

	SLNSessionRef session = SLNSessionCreateInternal(cache, sessionID, key_raw, key_enc, userID, mode, username);
	if(!session) rc = DB_ENOMEM;
	if(rc < 0) goto cleanup;

	session_cache(cache, session);
	*out = session; session = NULL;

cleanup:
	db_txn_abort(txn); txn = NULL;
	SLNRepoDBClose(repo, &db);
	FREE(&passhash);
	return rc;
}



static int cookie_parse(strarg_t const cookie, uint64_t *const sessionID, byte_t sessionKey[SESSION_KEY_LEN]) {
	unsigned long long id = 0;
	str_t key_str[SESSION_KEY_HEX+1];
	key_str[0] = '\0';
	sscanf(cookie, "s=%llu:" SESSION_KEY_FMT, &id, key_str);
	if(0 == id) return DB_EINVAL;
	if(strlen(key_str) != SESSION_KEY_HEX) return DB_EINVAL;
	*sessionID = (uint64_t)id;
	byte_t key_raw[SESSION_KEY_LEN];
	tobin(key_raw, key_str, SESSION_KEY_HEX);
	byte_t key_enc[SHA256_DIGEST_LENGTH];
	SHA256(key_raw, SESSION_KEY_LEN, key_enc);
	memcpy(sessionKey, key_enc, SESSION_KEY_LEN);
	return 0;
}
static int session_lookup(SLNSessionCacheRef const cache, uint64_t const id, byte_t const key[SESSION_KEY_LEN], SLNSessionRef *const out) {
	uint16_t const pos = session_pos(cache, id);
	// TODO: Locking (not really necessary but just good practice).
	for(uint16_t i = pos; i < pos+SEARCH_DIST; i++) {
		uint16_t const x = i % cache->size;
		if(id != cache->ids[x]) continue;
		SLNSessionRef const s = cache->sessions[x];
		int rc = SLNSessionKeyValid(s, key);
		if(rc < 0) return rc;
		*out = SLNSessionRetain(s);
		return 0;
	}
	return DB_NOTFOUND;
}

int SLNSessionCacheLoadSessionUnsafe(SLNSessionCacheRef const cache, uint64_t const id, SLNSessionRef *const out) {
	SLNRepoRef const repo = cache->repo;
	DB_env *db = NULL;
	DB_txn *txn = NULL;
	SLNMode mode = 0;
	str_t *username = NULL;
	byte_t key_enc[SESSION_KEY_LEN] = {0};
	int rc;

	SLNRepoDBOpenUnsafe(repo, &db);
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(rc < 0) goto cleanup;

	DB_val sessionID_key[1];
	SLNSessionByIDKeyPack(sessionID_key, txn, id);
	DB_val session_val[1];
	rc = db_get(txn, sessionID_key, session_val);
	if(rc < 0) goto cleanup;
	uint64_t userID;
	strarg_t key_str;
	SLNSessionByIDValUnpack(session_val, txn, &userID, &key_str);
	db_assertf(userID > 0, "Invalid session user ID %llu", (unsigned long long)userID);
	db_assertf(key_str, "Invalid session hash %s", key_str);

	DB_val userID_key[1];
	SLNUserByIDKeyPack(userID_key, txn, userID);
	DB_val user_val[1];
	rc = db_get(txn, userID_key, user_val);
	if(rc < 0) goto cleanup;
	strarg_t name, ignore2, ignore3;
	uint64_t ignore4, ignore5;
	SLNUserByIDValUnpack(user_val, txn, &name, &ignore2,
		&ignore3, &mode, &ignore4, &ignore5);
	// TODO: Handle NULL outputs.

	if(!mode) rc = DB_EACCES;
	if(rc < 0) goto cleanup;

	username = strdup(name);
	if(!username) rc = DB_ENOMEM;
	if(rc < 0) goto cleanup;

	tobin(key_enc, key_str, SESSION_KEY_HEX);

	db_txn_abort(txn); txn = NULL;
	SLNRepoDBClose(repo, &db);

	SLNSessionRef session = SLNSessionCreateInternal(cache, id, NULL, key_enc, userID, mode, username);
	if(!session) rc = DB_ENOMEM;
	if(rc < 0) goto cleanup;

	session_cache(cache, session);
	*out = session; session = NULL;

cleanup:
	db_txn_abort(txn); txn = NULL;
	SLNRepoDBClose(repo, &db);
	FREE(&username);
	return rc;
}
int SLNSessionCacheLoadSession(SLNSessionCacheRef const cache, uint64_t const id, byte_t const *const key, SLNSessionRef *const out) {
	SLNSessionRef session = NULL;
	int rc = SLNSessionCacheLoadSessionUnsafe(cache, id, &session);
	if(rc < 0) goto cleanup;
	rc = SLNSessionKeyValid(session, key);
	if(rc < 0) goto cleanup;
	*out = session; session = NULL;
cleanup:
	SLNSessionRelease(&session);
	return rc;
}

int SLNSessionCacheCopyActiveSession(SLNSessionCacheRef const cache, strarg_t const cookie, SLNSessionRef *const out) {
	assert(out);
	if(!cache) {
		*out = NULL;
		return UV_EINVAL;
	}
	if(!cookie) {
		*out = SLNSessionRetain(cache->public);
		return 0;
	}

	uint64_t sessionID;
	byte_t sessionKey[SESSION_KEY_LEN];
	int rc = cookie_parse(cookie, &sessionID, sessionKey);
	if(rc < 0) {
		*out = SLNSessionRetain(cache->public);
		return 0;
	}

	SLNSessionRef session = NULL;
	rc = session_lookup(cache, sessionID, sessionKey, &session);
	if(rc >= 0) {
		*out = session; session = NULL;
		return 0;
	}
	if(DB_EACCES == rc) {
		*out = SLNSessionRetain(cache->public);
		return 0;
	}
	if(DB_NOTFOUND != rc) {
		*out = NULL;
		return rc;
	}

	rc = SLNSessionCacheLoadSession(cache, sessionID, sessionKey, &session);
	if(rc >= 0) {
		*out = session; session = NULL;
		return 0;
	}
	if(DB_EACCES == rc || DB_NOTFOUND == rc) {
		*out = SLNSessionRetain(cache->public);
		return 0;
	}
	*out = NULL;
	return rc;
}

