#include <assert.h>
#include "../deps/smhasher/MurmurHash3.h"
#include "util/bcrypt.h"
#include "StrongLink.h"
#include "SLNDB.h"

#define SESSION_KEY_FMT "%32[0-9a-fA-F]"

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



SLNSessionCacheRef SLNSessionCacheCreate(SLNRepoRef const repo, uint16_t const size) {
	assert(repo);
	assert(size);
	SLNSessionCacheRef cache = calloc(1, sizeof(struct SLNSessionCache));
	if(!cache) return NULL;

	cache->repo = repo;
	cache->public = SLNSessionCreateInternal(cache, 0, NULL, 0, SLNRepoGetPublicMode(repo), NULL);
	if(!cache->public) {
		SLNSessionCacheFree(&cache);
		return NULL;
	}

	async_mutex_init(cache->lock, 0);
	cache->size = size;
	cache->ids = calloc(size, sizeof(uint64_t));
	cache->sessions = calloc(size, sizeof(*cache->sessions));
	if(!cache->ids || !cache->sessions) {
		SLNSessionCacheFree(&cache);
		return NULL;
	}

	cache->timer->data = cache;
	uv_timer_init(loop, cache->timer);
	cache->timeouts = calloc(size, sizeof(*cache->timeouts));
	cache->active = calloc(size, sizeof(*cache->active));
	cache->pos = 0;
	if(!cache->timeouts || !cache->active) {
		SLNSessionCacheFree(&cache);
		return NULL;
	}

	return cache;
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
	memset(cache->timer, 0, sizeof(cache->timer));
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
		cache->timeouts[cache->pos] = uv_now(loop) + EXPIRE_TIMEOUT;
//		cache->pos++; // TODO: Is this a ring buffer?
		// TODO: Start timer if necessary.
		return;
//	}
}


int SLNSessionCacheCreateSession(SLNSessionCacheRef const cache, strarg_t const username, strarg_t const password, SLNSessionRef *const out) {
	if(!cache) return DB_EINVAL;
	if(!username) return DB_EINVAL;
	if(!password) return DB_EINVAL;
	assert(out);

	SLNRepoRef const repo = cache->repo;
	DB_env *db = NULL;
	SLNRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(DB_SUCCESS != rc) {
		SLNRepoDBClose(repo, &db);
		return rc;
	}

	DB_val username_key[1], userID_val[1];
	SLNUserIDByNameKeyPack(username_key, txn, username);
	rc = db_get(txn, username_key, userID_val);
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		SLNRepoDBClose(repo, &db);
		return rc;
	}
	uint64_t const userID = db_read_uint64(userID_val);
	db_assert(userID);

	DB_val userID_key[1], user_val[1];
	SLNUserByIDKeyPack(userID_key, txn, userID);
	rc = db_get(txn, userID_key, user_val);
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		SLNRepoDBClose(repo, &db);
		return rc;
	}
	strarg_t u, p, ignore1;
	SLNMode mode;
	uint64_t ignore2, ignore3;
	SLNUserByIDValUnpack(user_val, txn, &u, &p, &ignore1, &mode, &ignore2, &ignore3);
	db_assert(0 == strcmp(username, u));
	str_t *passhash = strdup(p);

	db_txn_abort(txn); txn = NULL;
	SLNRepoDBClose(repo, &db);

	if(!mode) {
		FREE(&passhash);
		return DB_EACCES;
	}
	if(!checkpass(password, passhash)) {
		FREE(&passhash);
		return DB_EACCES;
	}
	FREE(&passhash);


	byte_t bin[SESSION_KEY_LEN];
	rc = async_random(bin, SESSION_KEY_LEN);
	if(rc < 0) return DB_ENOMEM; // ???
	char hex[SESSION_KEY_HEX+1];
	tohex(hex, bin, SESSION_KEY_LEN);
	hex[SESSION_KEY_HEX] = '\0';

	str_t *sessionHash = hashpass(hex);
	if(!sessionHash) return DB_ENOMEM;


	SLNRepoDBOpen(repo, &db);
	rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
	if(DB_SUCCESS != rc) {
		FREE(&sessionHash);
		return rc;
	}

	uint64_t const sessionID = db_next_id(SLNSessionByID, txn);
	DB_val sessionID_key[1], session_val[1];
	SLNSessionByIDKeyPack(sessionID_key, txn, sessionID);
	SLNSessionByIDValPack(session_val, txn, userID, sessionHash);
	FREE(&sessionHash);
	rc = db_put(txn, sessionID_key, session_val, DB_NOOVERWRITE_FAST);
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		SLNRepoDBClose(repo, &db);
		return rc;
	}

	rc = db_txn_commit(txn); txn = NULL;
	SLNRepoDBClose(repo, &db);
	if(DB_SUCCESS != rc) return rc;


	SLNSessionRef session = SLNSessionCreateInternal(cache, sessionID, bin, userID, mode, username);
	if(!session) return DB_ENOMEM;
	session_cache(cache, session);
	*out = session;
	return DB_SUCCESS;
}



static int cookie_parse(strarg_t const cookie, uint64_t *const sessionID, byte_t sessionKey[SESSION_KEY_LEN]) {
	unsigned long long id = 0;
	str_t key[SESSION_KEY_HEX+1];
	key[0] = '\0';
	sscanf(cookie, "s=%llu:" SESSION_KEY_FMT, &id, key);
	if(0 == id) return DB_EINVAL;
	if(strlen(key) != SESSION_KEY_HEX) return DB_EINVAL;
	*sessionID = (uint64_t)id;
	tobin(sessionKey, key, SESSION_KEY_HEX);
	return DB_SUCCESS;
}
static int session_lookup(SLNSessionCacheRef const cache, uint64_t const id, byte_t const key[SESSION_KEY_LEN], SLNSessionRef *const out) {
	uint16_t const pos = session_pos(cache, id);
	// TODO: Locking (not really necessary but just good practice).
	for(uint16_t i = pos; i < pos+SEARCH_DIST; i++) {
		uint16_t const x = i % cache->size;
		if(id != cache->ids[x]) continue;
		SLNSessionRef const s = cache->sessions[x];
		// TODO: Constant time comparison! Security-critical!
		if(0 != memcmp(key, SLNSessionGetKey(s), SESSION_KEY_LEN)) return DB_EACCES;
		*out = s;
		return DB_SUCCESS;
	}
	return DB_NOTFOUND;
}
static int session_load(SLNSessionCacheRef const cache, uint64_t const id, byte_t const key[SESSION_KEY_LEN], SLNSessionRef *const out) {
	SLNRepoRef const repo = cache->repo;
	DB_env *db = NULL;
	SLNRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(DB_SUCCESS != rc) return rc;

	DB_val sessionID_key[1];
	SLNSessionByIDKeyPack(sessionID_key, txn, id);
	DB_val session_val[1];
	rc = db_get(txn, sessionID_key, session_val);
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		SLNRepoDBClose(repo, &db);
		return rc;
	}
	uint64_t userID;
	strarg_t hash;
	SLNSessionByIDValUnpack(session_val, txn, &userID, &hash);
	db_assertf(userID > 0, "Invalid session user ID %llu", (unsigned long long)userID);
	db_assertf(hash, "Invalid session hash %s", hash);

	// This is painful... We have to do a whole extra lookup just
	// to get the mode. We could store it in the session's row, but
	// that seems like a bad idea.
	// Maybe in the future we will be able to use the username, etc.
	DB_val userID_key[1];
	SLNUserByIDKeyPack(userID_key, txn, userID);
	DB_val user_val[1];
	rc = db_get(txn, userID_key, user_val);
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		SLNRepoDBClose(repo, &db);
		return rc;
	}
	strarg_t name, ignore2, ignore3;
	uint64_t ignore4, ignore5;
	SLNMode mode;
	SLNUserByIDValUnpack(user_val, txn, &name, &ignore2,
		&ignore3, &mode, &ignore4, &ignore5);
	// TODO: Replace *Unpack with static functions and handle NULL outputs.
	if(!mode) {
		db_txn_abort(txn); txn = NULL;
		SLNRepoDBClose(repo, &db);
		return DB_EACCES;
	}

	str_t *username = strdup(name);
	str_t *sessionHash = strdup(hash);

	db_txn_abort(txn); txn = NULL;
	SLNRepoDBClose(repo, &db);

	if(!username || !sessionHash) {
		FREE(&username);
		FREE(&sessionHash);
		return DB_ENOMEM;
	}

	char hex[SESSION_KEY_HEX+1];
	tohex(hex, key, SESSION_KEY_LEN);
	hex[SESSION_KEY_HEX] = '\0';
	if(!checkpass(hex, sessionHash)) {
		FREE(&username);
		FREE(&sessionHash);
		return DB_EACCES;
	}
	FREE(&sessionHash);

	SLNSessionRef session = SLNSessionCreateInternal(cache, id, key, userID, mode, username);
	FREE(&username);
	if(!session) return DB_ENOMEM;
	session_cache(cache, session);

	*out = session;
	return DB_SUCCESS;
}
SLNSessionRef SLNSessionCacheCopyActiveSession(SLNSessionCacheRef const cache, strarg_t const cookie) {
	if(!cache) return NULL;
	if(!cookie) return SLNSessionRetain(cache->public);

	uint64_t sessionID;
	byte_t sessionKey[SESSION_KEY_LEN];
	int rc = cookie_parse(cookie, &sessionID, sessionKey);
	if(DB_SUCCESS != rc) return SLNSessionRetain(cache->public);

	SLNSessionRef session;
	rc = session_lookup(cache, sessionID, sessionKey, &session);
	if(DB_SUCCESS == rc) return session;
	if(DB_EACCES == rc) return SLNSessionRetain(cache->public);
	if(DB_NOTFOUND != rc) return NULL;
	rc = session_load(cache, sessionID, sessionKey, &session);
	if(DB_SUCCESS == rc) return session;
	if(DB_EACCES == rc) return SLNSessionRetain(cache->public);
	if(DB_NOTFOUND == rc) return SLNSessionRetain(cache->public);
	return NULL;
}

