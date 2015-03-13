#include "EFSRepoPrivate.h"
#include "bcrypt.h"

// TODO: better handling of database errors
// expected errors (e.g. DB_NOTFOUND) should be handled specifically
// unexpected errors should log a message and return an appropriate UV_* error

#define COOKIE_CACHE_COUNT 1000
#define COOKIE_CACHE_SEARCH 15
#define COOKIE_CACHE_TIMEOUT (1000 * 60 * 5)

#define SESSION_KEY_LEN 32
#define SESSION_KEY_FMT "%31s"

struct cookie_t {
	str_t sessionKey[SESSION_KEY_LEN];
	uint64_t time;
	uint64_t userID;
};

static int user_auth(EFSRepoRef const repo, strarg_t const username, strarg_t const password, uint64_t *const outUserID) {
	assert(repo);
	assert(username);
	assert(password);
	assert(outUserID);

	DB_env *db = NULL;
	EFSRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(DB_SUCCESS != rc) {
		EFSRepoDBClose(repo, &db);
		return rc;
	}

	DB_val username_key[1];
	EFSUserIDByNameKeyPack(username_key, txn, username);
	DB_val userID_val[1];
	rc = db_get(txn, username_key, userID_val);
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &db);
		return rc;
	}
	uint64_t const userID = db_read_uint64(userID_val);
	if(!userID) {
		db_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &db);
		return UV_EACCES;
	}

	DB_val userID_key[1];
	EFSUserByIDKeyPack(userID_key, txn, userID);
	DB_val user_val[1];
	rc = db_get(txn, userID_key, user_val);
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &db);
		return rc;
	}
	strarg_t const u = db_read_string(txn, user_val);
	assert(0 == strcmp(username, u));
	str_t *passhash = strdup(db_read_string(txn, user_val));

	db_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(repo, &db);

	if(!checkpass(password, passhash)) {
		FREE(&passhash);
		return UV_EACCES;
	}
	FREE(&passhash);

	*outUserID = userID;
	return 0;
}
static int session_create(EFSRepoRef const repo, uint64_t const userID, strarg_t const sessionKey, uint64_t *const outSessionID) {
	assert(repo);
	assert(userID);
	assert(sessionKey);
	assert(outSessionID);

	str_t *sessionHash = hashpass(sessionKey);
	if(!sessionHash) {
		return UV_ENOMEM;
	}

	DB_env *db = NULL;
	EFSRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
	if(DB_SUCCESS != rc) {
		FREE(&sessionHash);
		return rc;
	}

	uint64_t const sessionID = db_next_id(txn, EFSSessionByID);
	DB_val sessionID_key[1];
	EFSSessionByIDKeyPack(sessionID_key, txn, sessionID);
	DB_val session_val[1];
	EFSSessionByIDValPack(session_val, txn, userID, sessionHash);
	FREE(&sessionHash);
	rc = db_put(txn, sessionID_key, session_val, DB_NOOVERWRITE_FAST);
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &db);
		return rc;
	}

	rc = db_txn_commit(txn); txn = NULL;
	EFSRepoDBClose(repo, &db);
	if(DB_SUCCESS != rc) {
		return rc;
	}

	*outSessionID = sessionID;
	return 0;
}
static void cookie_cache_store(EFSRepoRef const repo, uint64_t const userID, uint64_t const sessionID, strarg_t const sessionKey) {
	assert(repo);
	assert(userID);
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
	repo->cookie_data[i].time = now;
}
static size_t cookie_cache_lookup(EFSRepoRef const repo, uint64_t const sessionID, strarg_t const sessionKey, uint64_t *const outUserID) {
	assert(repo);
	assert(sessionID);
	assert(sessionKey);
	assert(outUserID);

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
	return i;
}
static int cookie_create(EFSRepoRef const repo, strarg_t const username, strarg_t const password, str_t **const outCookie) {
	assert(repo);
	assert(username);
	assert(password);
	assert(outCookie);

	uint64_t userID = 0;
	int rc = user_auth(repo, username, password, &userID);
	if(0 != rc) return rc;

	byte_t bytes[SESSION_KEY_LEN/2];
	str_t sessionKey[SESSION_KEY_LEN] = "1111111111" "1111111111" "1111111111" "1";
	// TODO: Generate and convert to hex
	// Make sure to nul-terminate

	uint64_t sessionID = 0;
	rc = session_create(repo, userID, sessionKey, &sessionID);
	if(0 != rc) return rc;

	str_t *cookie = NULL;
	rc = asprintf(&cookie, "%llu:%s", (unsigned long long)sessionID, sessionKey);
	if(rc < 0) return -1;

	cookie_cache_store(repo, userID, sessionID, sessionKey);

	*outCookie = cookie;
	return 0;
}
static int cookie_auth(EFSRepoRef const repo, strarg_t const cookie, uint64_t *const outUserID) {
	assert(repo);
	assert(cookie);
	assert(outUserID);

	unsigned long long sessionID_ULL = 0;
	str_t sessionKey[SESSION_KEY_LEN] = {};
	sscanf(cookie, "s=%llu:" SESSION_KEY_FMT, &sessionID_ULL, sessionKey);
	uint64_t const sessionID = sessionID_ULL;
	if(!sessionID) return UV_EACCES;
	if(strlen(sessionKey) != SESSION_KEY_LEN-1) return UV_EACCES;

	uint64_t userID = 0;
	if(HASH_NOTFOUND != cookie_cache_lookup(repo, sessionID, sessionKey, &userID)) {
		*outUserID = userID;
		return 0;
	}

	DB_env *db = NULL;
	EFSRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(DB_SUCCESS != rc) return rc;

	DB_val sessionID_key[1];
	EFSSessionByIDKeyPack(sessionID_key, txn, sessionID);
	DB_val session_val[1];
	rc = db_get(txn, sessionID_key, session_val);
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &db);
		return rc;
	}
	userID = db_read_uint64(session_val);
	if(!userID) {
		db_txn_abort(txn); txn = NULL;
		EFSRepoDBClose(repo, &db);
		return UV_EACCES;
	}
	str_t *sessionHash = strdup(db_read_string(txn, session_val));

	db_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(repo, &db);

	if(!sessionHash) return UV_ENOMEM;

	if(!checkpass(sessionKey, sessionHash)) {
		FREE(&sessionHash);
		return UV_EACCES;
	}
	cookie_cache_store(repo, userID, sessionID, sessionKey);
	FREE(&sessionHash);

	*outUserID = userID;
	return 0;
}

int EFSRepoAuthInit(EFSRepoRef const repo) {
	assert(repo);
	int rc = hash_init(repo->cookie_hash, COOKIE_CACHE_COUNT, sizeof(uint64_t));
	if(rc < 0) return rc;
	repo->cookie_data = calloc(COOKIE_CACHE_COUNT, sizeof(struct cookie_t));
	if(!repo->cookie_data) return UV_ENOMEM;
	return 0;
}
void EFSRepoAuthDestroy(EFSRepoRef const repo) {
	assert(repo);
	hash_destroy(repo->cookie_hash);
	FREE(&repo->cookie_data);
}

int EFSRepoCookieCreate(EFSRepoRef const repo, strarg_t const username, strarg_t const password, str_t **const outCookie) {
	if(!repo) return UV_EINVAL;
	if(!username) return UV_EINVAL;
	if(!password) return UV_EINVAL;
	return cookie_create(repo, username, password, outCookie);
}
int EFSRepoCookieAuth(EFSRepoRef const repo, strarg_t const cookie, uint64_t *const outUserID) {
	if(!repo) return UV_EINVAL;
	if(!cookie) return UV_EINVAL;
	return cookie_auth(repo, cookie, outUserID);
}

