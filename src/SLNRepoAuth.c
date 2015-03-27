#include "SLNRepoPrivate.h"
#include "util/bcrypt.h"

#define COOKIE_CACHE_COUNT 1000
#define COOKIE_CACHE_SEARCH 15
#define COOKIE_CACHE_TIMEOUT (1000 * 60 * 5)

#define SESSION_KEY_LEN 32
#define SESSION_KEY_FMT "%31s"

struct cookie_t {
	str_t sessionKey[SESSION_KEY_LEN];
	uint64_t time;
	uint64_t userID;
	SLNMode mode;
};

static int user_auth(SLNRepoRef const repo, strarg_t const username, strarg_t const password, uint64_t *const outUserID, SLNMode *const outMode) {
	assert(repo);
	assert(username);
	assert(password);
	assert(outUserID);
	assert(outMode);

	DB_env *db = NULL;
	SLNRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(DB_SUCCESS != rc) {
		SLNRepoDBClose(repo, &db);
		return rc;
	}

	DB_val username_key[1];
	SLNUserIDByNameKeyPack(username_key, txn, username);
	DB_val userID_val[1];
	rc = db_get(txn, username_key, userID_val);
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		SLNRepoDBClose(repo, &db);
		return rc;
	}
	uint64_t const userID = db_read_uint64(userID_val);
	db_assert(userID);

	DB_val userID_key[1];
	SLNUserByIDKeyPack(userID_key, txn, userID);
	DB_val user_val[1];
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

	*outUserID = userID;
	*outMode = mode;
	return DB_SUCCESS;
}
static int session_create(SLNRepoRef const repo, uint64_t const userID, strarg_t const sessionKey, uint64_t *const outSessionID) {
	assert(repo);
	assert(userID);
	assert(sessionKey);
	assert(outSessionID);

	str_t *sessionHash = hashpass(sessionKey);
	if(!sessionHash) {
		return DB_ENOMEM;
	}

	DB_env *db = NULL;
	SLNRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
	if(DB_SUCCESS != rc) {
		FREE(&sessionHash);
		return rc;
	}

	uint64_t const sessionID = db_next_id(SLNSessionByID, txn);
	DB_val sessionID_key[1];
	SLNSessionByIDKeyPack(sessionID_key, txn, sessionID);
	DB_val session_val[1];
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
	if(DB_SUCCESS != rc) {
		return rc;
	}

	*outSessionID = sessionID;
	return DB_SUCCESS;
}
static void cookie_cache_store(SLNRepoRef const repo, uint64_t const userID, SLNMode const mode, uint64_t const sessionID, strarg_t const sessionKey) {
	assert(repo);
	assert(userID);
	assert(mode);
	assert(sessionID);
	assert(sessionKey);
	uint64_t const now = uv_now(loop);
	uint64_t const expires = now - COOKIE_CACHE_TIMEOUT;

	size_t const x = hash_func(repo->cookie_hash, (char const *)&sessionID);
	size_t i = x;
	for(;;) {
		if(0 == hash_bucket_empty(repo->cookie_hash, i)) break;
		if(0 == hash_bucket_match(repo->cookie_hash, i, (char const *)&sessionID)) return;
		if(repo->cookie_data[i].time < expires) break;
		if((x + COOKIE_CACHE_SEARCH) % COOKIE_CACHE_COUNT == i) break;
		i = (i + 1) % COOKIE_CACHE_COUNT;
		if(x == i) break;
	}

	hash_set_raw(repo->cookie_hash, i, (char const *)&sessionID);
	memcpy(&repo->cookie_data[i].sessionKey, sessionKey, 32);
	repo->cookie_data[i].userID = userID;
	repo->cookie_data[i].mode = mode;
	repo->cookie_data[i].time = now;
}
static size_t cookie_cache_lookup(SLNRepoRef const repo, uint64_t const sessionID, strarg_t const sessionKey, uint64_t *const outUserID, SLNMode *const outMode) {
	assert(repo);
	assert(sessionID);
	assert(sessionKey);
	assert(outUserID);
	assert(outMode);

	size_t const x = hash_func(repo->cookie_hash, (char const *)&sessionID);
	size_t i = x;
	for(;;) {
		if(0 == hash_bucket_match(repo->cookie_hash, i, (char const *)&sessionID)) break;
		if(0 == hash_bucket_empty(repo->cookie_hash, i)) return HASH_NOTFOUND;
		if((x + COOKIE_CACHE_SEARCH) % COOKIE_CACHE_COUNT == i) return HASH_NOTFOUND;
		i = (i + 1) % COOKIE_CACHE_COUNT;
		if(x == i) return HASH_NOTFOUND;
	}

	if(0 != passcmp(repo->cookie_data[i].sessionKey, sessionKey)) return HASH_NOTFOUND;

	uint64_t const now = uv_now(loop);
	uint64_t const expires = now - COOKIE_CACHE_TIMEOUT;
	if(repo->cookie_data[i].time < expires) {
		repo->cookie_data[i].time = now;
	}
	*outUserID = repo->cookie_data[i].userID;
	*outMode = repo->cookie_data[i].mode;
	return i;
}
static int cookie_create(SLNRepoRef const repo, strarg_t const username, strarg_t const password, str_t **const outCookie, SLNMode *const outMode) {
	assert(repo);
	assert(username);
	assert(password);
	assert(outCookie);

	uint64_t userID = 0;
	SLNMode mode = 0;
	int rc = user_auth(repo, username, password, &userID, &mode);
	if(DB_SUCCESS != rc) return rc;
	assert(mode);

	byte_t bytes[SESSION_KEY_LEN/2];
	str_t sessionKey[SESSION_KEY_LEN] = "1111111111" "1111111111" "1111111111" "1";
	// TODO: Generate and convert to hex
	// Make sure to nul-terminate

	uint64_t sessionID = 0;
	rc = session_create(repo, userID, sessionKey, &sessionID);
	if(DB_SUCCESS != rc) return rc;

	str_t *cookie = aasprintf("%llu:%s", (unsigned long long)sessionID, sessionKey);
	if(!cookie) return -1;

	cookie_cache_store(repo, userID, mode, sessionID, sessionKey);

	*outCookie = cookie;
	*outMode = mode;
	return DB_SUCCESS;
}
static int cookie_auth(SLNRepoRef const repo, strarg_t const cookie, uint64_t *const outUserID, SLNMode *const outMode) {
	assert(repo);
	assert(cookie);
	assert(outUserID);

	unsigned long long sessionID_ULL = 0;
	str_t sessionKey[SESSION_KEY_LEN] = {};
	sscanf(cookie, "s=%llu:" SESSION_KEY_FMT, &sessionID_ULL, sessionKey);
	uint64_t const sessionID = sessionID_ULL;
	if(!sessionID) return DB_EINVAL;
	if(strlen(sessionKey) != SESSION_KEY_LEN-1) return DB_EINVAL;

	uint64_t userID = 0;
	SLNMode mode = 0;
	if(HASH_NOTFOUND != cookie_cache_lookup(repo, sessionID, sessionKey, &userID, &mode)) {
		*outUserID = userID;
		*outMode = mode;
		return DB_SUCCESS;
	}

	DB_env *db = NULL;
	SLNRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(DB_SUCCESS != rc) return rc;

	DB_val sessionID_key[1];
	SLNSessionByIDKeyPack(sessionID_key, txn, sessionID);
	DB_val session_val[1];
	rc = db_get(txn, sessionID_key, session_val);
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		SLNRepoDBClose(repo, &db);
		return rc;
	}
	strarg_t h;
	SLNSessionByIDValUnpack(session_val, txn, &userID, &h);
	db_assert(userID);
	db_assert(h);

	// This is painful... We have to do a whole extra lookup just
	// to get the mode. We can't cache it with the session either,
	// in case it goes stale.
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
	strarg_t ignore1, ignore2, ignore3;
	uint64_t ignore4, ignore5;
	SLNUserByIDValUnpack(user_val, txn, &ignore1, &ignore2, &ignore3, &mode, &ignore4, &ignore5);
	if(!mode) {
		db_txn_abort(txn); txn = NULL;
		SLNRepoDBClose(repo, &db);
		return DB_EACCES;
	}

	str_t *sessionHash = strdup(h);

	db_txn_abort(txn); txn = NULL;
	SLNRepoDBClose(repo, &db);

	if(!sessionHash) return DB_ENOMEM;

	if(!checkpass(sessionKey, sessionHash)) {
		FREE(&sessionHash);
		return DB_EACCES;
	}
	cookie_cache_store(repo, userID, mode, sessionID, sessionKey);
	FREE(&sessionHash);

	*outUserID = userID;
	*outMode = mode;
	return DB_SUCCESS;
}

int SLNRepoAuthInit(SLNRepoRef const repo) {
	assert(repo);
	int rc = hash_init(repo->cookie_hash, COOKIE_CACHE_COUNT, sizeof(uint64_t));
	if(rc < 0) return DB_ENOMEM;
	repo->cookie_data = calloc(COOKIE_CACHE_COUNT, sizeof(struct cookie_t));
	if(!repo->cookie_data) return DB_ENOMEM;
	return DB_SUCCESS;
}
void SLNRepoAuthDestroy(SLNRepoRef const repo) {
	assert(repo);
	hash_destroy(repo->cookie_hash);
	FREE(&repo->cookie_data);
}

int SLNRepoCookieCreate(SLNRepoRef const repo, strarg_t const username, strarg_t const password, str_t **const outCookie, SLNMode *const outMode) {
	if(!repo) return DB_EINVAL;
	if(!username) return DB_EINVAL;
	if(!password) return DB_EINVAL;
	return cookie_create(repo, username, password, outCookie, outMode);
}
int SLNRepoCookieAuth(SLNRepoRef const repo, strarg_t const cookie, uint64_t *const outUserID, SLNMode *const outMode) {
	if(!repo) return DB_EINVAL;
	if(!cookie) return DB_EINVAL;
	return cookie_auth(repo, cookie, outUserID, outMode);
}

