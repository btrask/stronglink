


// okay so our clever hash table design didn't work and also didnt even solve the problem we cared about
// what we really need is a memory-based cache that remembers sessions for up to say 5 minutes

// the primary goal is to avoid running bcrypt per request
// the secondary goal is to avoid hitting the database at all

// the cache should be time-based because we want to evict unused keys from memory quickly

// its probably a good idea to keep in mind the nature of storing secure information
// so we should at least keep open the option of using guard pages and secure erase
// also memlock


// in our existing implementation session keys are 31 bytes
// we're already using session ids so keys dont have to be particularly collision resistant
// and we're still using bcrypt on them so they dont have to be any stronger than the ideal password
// i guess the problem is we're storing them as hex
// we can represent them as hex but maybe we should store them in memory as binary

// fwiw a binary rep could be all zeroes, which the current hash table would choke on
// so we can fix that too


// for time-based eviction, i see basically two options
// first, have a single timer for the whole table, and scan everything every ~5 minutes
// or have separate timers for each item

// the best option would probably be to keep a list of dirty items and just scan those every 5 minutes
// then we dont even have to track the time per-item
// we could even get fancy with storing the differential encoding of the time between items to ensure that nothing gets cleared too early
// but that quickly turns into our own timer implementation...




// actually we could do a pretty good coalescing timer
// keep one pointer to current timer+buffer
// when a new request comes in, add it to the current buffer
// or if the current buffer is full, detatch it and let the timer clean it up later
// thus we gracefully handle larger numbers of timers set without growing arrays dynamically

// but that's neither here nor there




// for cleanup, we should sort the list of dirty bins first so we can access them in order
// although i'm not sure how much we care...




// i think the easist thing to do is to not handle collisions at all
// its just a cache so w/e right?


// thus we get an item layout like this:

struct session {
	uint64_t id;
	byte_t key[16];
	SLNMode mode;
};

#define CACHE_SIZE 1000
static struct session *sessions; // malloc'd with guard pages
static uint16_t dirty[CACHE_SIZE];
static size_t count;


// whats bothering me at this point is that we dont have any sort of consistency
// one var is malloced and the other isnt
// and bad naming convention



// for the record...
// i can really tell the limits of my experience at times like this
// a more experienced programmer would be able to bang this shit out in like five minutes
// of course if i were using python i'd be able to bang it out in about five seconds

// i'd complain about the python solution not having stuff like guard pages or secure erase
// but theres probably modules that do that stuff for you
// although who knows how good they are
// and most python programmers wouldnt think to use them




// okay so
// the first problem is naming
// we havent even settled on a name for this module
// session cache? cookie cache?


// oh... oh yeah
// i forgot... we cant use globals
// in case more than one repo is loaded

#define CACHE_SIZE 1000
#define CACHE_TIMEOUT (1000 * 60 * 5)

typedef struct {
	uint64_t sessionID;
	byte_t sessionKey[16];
	uint64_t userID;
	SLNMode mode;
} cookie_t;
struct SLNRepo {
	...
	cookie_t *cookie_cache;
	uint16_t *stale_cookies;
	size_t stale_count;
	uv_timer_t cookie_timer[1];
	...
};

// stale cookies... i like it, lol
// considered calling the timer "over_timer" but decided against it



// note: we can actually have a thundering herd problem with sessions too
// although i dont think we care
// the optimization not traveled, by robert frost



// does it actually make sense to store separate session and user ids?
// yes, because one user can have multiple cookies at once



// oh...
// invalidating all of the cookies at once is a terrible idea
// because we'll get load spikes every 5 minutes as all of the keys get re-hashed

// ... actually, its almost okay?
// if we didnt drop the entries that were brand new
// we wouldnt drop everything, just the stuff that was truly stale
// in the meanwhile, items might get overwritten/updated



// really dissatisfied with this now
// first of all i cant figure out the best way to do expiration
// and now i'm seeing how much of a problem collisions are
// since wikipedia pointed out that 1m buckets with ~2k items has a 95% chance of collision

// i also know that it doesnt matter that much for our demo
// but i want the code we have to be reasonably high quality


// we need at least a few things we can point to and say "this is how good all of the code will eventually be"


// should we just write a coalescing timer?
// what about the problem of canceling timeouts?


// one idea for handling collisions
// always check up to 16 items after the hashed position
// dont stop at empty values so we dont have to worry about removal
// checking 16 integers sequentially is not exactly a large price to pay
// and we can stop early if the session is found, which is probably 99% of the time

// if theyre all full, which one do we overwrite?
// the first one? the oldest one? one at random?




// whats wrong with me...


// it would be good enough to just write modular code
// so that we could improve any part of it later
// rather than trying to figure out the whole thing at once
// which is apparently beyond my abilities

// but modularity is what we tried before with hash_t and that didnt work either




extern uint32_t SLNSeed;

void SLNSessionCache(SLNSessionRef const session, uint64_t const sessionID) {
	if(!session) return;
	if(0 == session->userID) return;
	uint32_t hash;
	MurmurHash3_x86_32(&sessionID, sizeof(sessionID), SLNSeed, &hash);
	uint16_t const pos = hash % CACHE_SIZE;
	SLNRepoRef const repo = session->repo;
	cookie_t *const cache = &repo->cookie_cache[pos];
	if(cache->sessionID == sessionID) return;


	if(0 == cache->sessionID) {
		repo->stale_cookies[repo->stale_count++] = pos;
		if(1 == repo->stale_count) {
			uint64_t const future = uv_now(loop) + CACHE_TIMEOUT;
			repo->cookie_timer->data = us;
			uv_timer_init(loop, repo->cookie_timer);
			uv_timer_start(repo->cookie_timer, timeout_cb, future - now, 0);
		}
	}
	cache->sessionID = sessionID;
	memcpy(cache->sessionKey, sessionKey, 16);
	cache->userID = session->userID;
	cache->mode = session->mode;
}




// new try

struct SLNRepo {
	...
	uint64_t *sessionIDs;
	session_t *sessions;
	uint16_t *active;
	uint64_t *expirations;
	uint16_t pos;
	...
};


// this seriously needs to be made into its own object
// maybe one of the problems is that we havent been giving it enough respect

struct SLNSessionCache {
	size_t size;
	uint64_t *sessionIDs;
	session_t *sessions;
	uint16_t *active;
	uint64_t *timeouts;
	uint16_t pos;
};

SLNSessionCacheRef SLNSessionCacheCreate(SLNRepoRef const repo, size_t const size);
void SLNSessionCacheFree(SLNSessionCacheRef *const cacheptr);

// yeah, now i think we're on the right track
// supporting dynamic cache sizes is good too


str_t *SLNSessionCacheCreateCookie(SLNSessionCacheRef const cache, strarg_t const username, strarg_t const password);
SLNSessionRef SLNSessionCacheCreateSession(SLNSessionCacheRef const cache, strarg_t const cookie);


// issues:
// - why is the session cache creating cookies?
// 	- should we call it the cookie cache (or cookie jar) instead?
// - in the old api, creating a cookie also tells you the mode
// 	- what if it gave you the session id, and you could look up everything else later?



int SLNSessionCacheCreateSession(SLNSessionCacheRef const cache, strarg_t const username, strarg_t const password, uint64_t *const outSessionID);

// actually its a race condition...
// theoretically the cached info might get dropped before the caller reads it


int SLNSessionCacheCreateSession(SLNSessionCacheRef const cache, strarg_t const username, strarg_t const password, str_t **const outSessionCookie);


// or better yet... what if we kept the actual session objects in memory and you could get the cookie from them?

SLNSessionRef SLNSessionCacheGetNewSession(SLNSessionCacheRef const cache, strarg_t const username, strarg_t const password);
SLNSessionRef SLNSessionCacheGetOldSession(SLNSessionCacheRef const cache, strarg_t const cookie);

SLNSessionCacheRef SLNSessionGetSessionCache(SLNSessionRef const session);
SLNRepoRef SLNSessionGetRepo(SLNSessionRef const session);
int SLNSessionGetAuthError(SLNSessionRef const session);
uint64_t SLNSessionGetUserID(SLNSessionRef const session);
SLNMode SLNSessionGetMode(SLNSessionRef const session);
strarg_t SLNSessionGetCookie(SLNSessionRef const session);



// note that this means sessions need to support concurrent access... which is fine since i think they're basically immutable, right?
// we also need to take the thundering herd problem more seriously
// but that shouldnt be too hard




// finally breathing easier...
// aren't we glad we didnt force ourselves when we knew we were taking the wrong approach?

#define EXPIRE_TIMEOUT (1000 * 60 * 4)
#define SWEEP_DELAY (1000 * 60 * 1)

struct SLNSessionCache {
	SLNRepoRef repo;
	SNLSessionRef public;

	async_mutex_t lock[1];
	uint16_t size;
	uint16_t pos;
	uint64_t *ids;
	SLNSessionRef *sessions;
	uv_timer_t timer[1];
	uint64_t *timeouts;
	uint16_t *active;
};


// new problem....
// when clobbering an active session
// how do we reset the timeout?

// we dont even know the timeout is set, so we'd accidentally set it twice
// or if it keeps getting clobbered, dozens of times...

// oh wait
// yes, we do know its set
// because if something's there when we go to write, it must be set already

// it'd be nice to reset the expiration at that point, but its not critical if we dont


// new problem
// the cache owns the sessions, which is good
// but the sessions might be in use when they expire, which is bad
// the cache cant just free the session when it expires

// standard solution: reference counting




// if the session is shared/immutable, we cant store the auth error in it
// but thats fine, we can just return it normally

int SLNSessionCacheGetNewSession(SLNSessionCacheRef const cache, strarg_t const username, strarg_t const password, SLNSessionRef *const out);
int SLNSessionCacheGetOldSession(SLNSessionCacheRef const cache, strarg_t const cookie, SLNSessionRef *const out);


// hmmm... this is bad because we can return an error and still return a session
// or actually, maybe its fine?
// if theres an error, you didnt get the session you wanted
// and you can ignore the output if you want, since you dont need to free it

// or... exactly how many possible errors are there?
// either return the real session, or the public session, or null


// or... just fail if theres an error, and provide another way to get a public session

// or...


SLNSessionRef SLNSessionCacheGetOldSession(SLNSessionCacheRef const cache, strarg_t const cookie, int *const outerr);

SLNSessionRef SLNSessionCacheGetOldSession(SLNSessionCacheRef const cache, strarg_t const cookie);
SLNSessionRef SLNSessionCacheGetPublicSession(SLNSessionCacheRef const cache);




// alternatively to the above
// if the available slots are full simply dont clobber


// btw our session getters should return the session with a retain count of +1
// so we should probably call them "create" rather than "get"

struct SLNSession {
	SLNSessionCacheRef cache;
	uint64_t userID;
	SLNMode mode;
	str_t *cookie;
	unsigned refcount;
};

SLNSessionRef SLNSessionRetain(SLNSessionRef const session);
void SLNSessionRelease(SLNSessionRef *const sessionptr);








static uint8_t hexchar(char const c) {
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a';
	if(c >= 'A' && c <= 'F') return c - 'A';
	assert(!"hex character");
}
static void tobin(uint8_t *const bin, char const *const hex, size_t const hexlen) {
	assert(0 == hexlen % 2);
	for(size_t i = 0; i < hexlen/2; i++) {
		bin[i] = hexchar(hex[i*2+0]) << 4 | hexchar(hex[i*2+1]) << 0;
	}
}
static void tohex(char *const hex, uint8_t const *const bin, size_t const binlen) {
	char const *const map = "0123456789abcdef";
	for(size_t i = 0; i < binlen; i++) {
		hex[i*2+0] = map[0xf & (bin[i] >> 4)];
		hex[i*2+1] = map[0xf & (bin[i] >> 0)];
	}
}





#define KEY_BIN_LEN 16
#define KEY_HEX_LEN 32
#define KEY_FMT "%32[0-9a-fA-F]"

#define SEARCH_DIST 16





SLNSessionCacheRef SLNSessionCacheCreate(SLNRepoRef const repo, uint16_t const size) {
	assert(repo);
	assert(size);
	SLNSessionCacheRef cache = calloc(1, sizeof(struct SLNSessionCache));
	if(!cache) return NULL;

	cache->repo = repo;
	cache->public = SLNSessionCreateInternal(repo, 0, SLNRepoGetPublicMode(repo), NULL);
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
	cache->size = 0;

	uv_timer_destroy(cache->timer);
	FREE(&cache->timeouts);
	FREE(&cache->active);
	cache->pos = 0;

	assert_zeroed(cache, 1);
	FREE(cacheptr); cache = NULL;
}

static void session_cache(SLNSessionCacheRef const cache, SLNSessionRef const session) {
	uint64_t const id = SLNSessionGetID(session);
	uint32_t hash;
	MurmurHash3_x86_32(&id, sizeof(id), SLNSeed, &hash);
	uint16_t const pos = hash % cache->size;
	for(uint16_t i = pos; i < pos+SEARCH_DIST; i++) {
		uint16_t const x = i % cache->size;
		if(id == cache->ids[x]) return;
		if(0 != cache->ids[x]) continue;
		cache->sessions[x] = SLNSessionRetain(session);
		cache->active[cache->pos] = x;
		cache->timeout[cache->pos] = uv_now(loop) + EXPIRE_TIMEOUT;
		cache->pos++; // TODO: Is this a ring buffer?
		return;
	}
}


int SLNSessionCacheCreateSession(SLNSessionCacheRef const cache, strarg_t const username, strarg_t const password, SLNSessionRef *const out) {
	if(!cache) return DB_EINVAL;
	if(!username) return DB_EINVAL;
	if(!password) return DB_EINVAL;
	assert(out);


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


	byte_t bin[KEY_BIN_LEN];
	rc = async_rand_bytes(bin, KEY_BIN_LEN);
	if(rc < 0) return DB_ENOMEM; // ???
	char hex[KEY_HEX_LEN+1];
	tohex(hex, bin, KEY_BIN_LEN);
	hex[KEY_HEX_LEN] = '\0';

	str_t *sessionHash = hashpass(hex);
	if(!sessionHash) return DB_ENOMEM;


	DB_env *db = NULL;
	SLNRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
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


	SLNSessionRef session = SLNSessionCreateInternal(cache, sessionID, bin, userID, mode);
	if(!session) return DB_ENOMEM;
	session_cache(cache, session);
	*out = session;
	return DB_SUCCESS;
}



static int cookie_parse(strarg_t const cookie, uint64_t *const sessionID, byte_t sessionKey[KEY_BIN_LEN]) {
	unsigned long long id = 0;
	str_t key[KEY_HEX_LEN+1];
	key[0] = '\0';
	sscanf(cookie, "s=%llu:" KEY_FMT, &id, key);
	if(0 == id) return DB_EINVAL;
	if(strlen(key) != KEY_HEX_LEN) return DB_EINVAL;
	*sessionID = (uint64_t)id;
	tobin(sessionKey, key, KEY_HEX_LEN);
	return DB_SUCCESS;
}
static int session_lookup(SLNSessionCacheRef const cache, uint64_t const id, byte_t const key[KEY_BIN_LEN], SLNSessionRef *const out) {
	uint32_t hash;
	MurmurHash3_x86_32(&id, sizeof(id), SLNSeed, &hash);
	uint16_t const pos = hash % cache->size;
	// TODO: Locking (not really necessary but just good practice).
	for(uint16_t i = pos; i < pos+SEARCH_DIST; i++) {
		uint16_t const x = i % cache->size;
		if(id != cache->ids[x]) continue;
		SLNSessionRef const s = cache->sessions[x];
		// TODO: Constant time comparison! Security-critical!
		if(0 != memcmp(key, SLNSessionGetKey(s), KEY_BIN_LEN) return DB_EACCES;
		*out = s;
		return DB_SUCCESS;
	}
	return DB_NOTFOUND;
}
static int session_load(SLNSessionCacheRef const cache, uint64_t const id, byte_t const key[KEY_BIN_LEN], SLNSessionRef *const out) {
	DB_env *db = NULL;
	SLNRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(DB_SUCCESS != rc) return NULL;

	DB_val sessionID_key[1];
	SLNSessionByIDKeyPack(sessionID_key, txn, id);
	DB_val session_val[1];
	rc = db_get(txn, sessionID_key, session_val);
	if(DB_SUCCESS != rc) {
		db_txn_abort(txn); txn = NULL;
		SLNRepoDBClose(repo, &db);
		return rc;
	}
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
	strarg_t ignore1, ignore2, ignore3;
	uint64_t ignore4, ignore5;
	SLNUserByIDValUnpack(user_val, txn, &ignore1, &ignore2,
		&ignore3, &mode, &ignore4, &ignore5);
	// TODO: Replace *Unpack with static functions and handle NULL outputs.
	if(!mode) {
		db_txn_abort(txn); txn = NULL;
		SLNRepoDBClose(repo, &db);
		return DB_EACCES;
	}

	str_t *sessionHash = strdup(hash);

	db_txn_abort(txn); txn = NULL;
	SLNRepoDBClose(repo, &db);

	if(!sessionHash) return DB_ENOMEM;

	if(!checkpass(sessionKey, sessionHash)) {
		FREE(&sessionHash);
		return DB_EACCES;
	}
	FREE(&sessionHash);

	SLNSessionRef session = SLNSessionCreateInternal(cache, id, key, userID, mode);
	if(!session) return DB_ENOMEM;
	session_cache(cache, session);

	*out = session;
	return DB_SUCCESS;
}
SLNSessionRef SLNSessionCacheCopyActiveSession(SLNSessionRef const cache, strarg_t const cookie) {
	if(!cache) return NULL;
	if(!cookie) return SLNSessionRetain(cache->public);

	uint64_t sessionID;
	byte_t sessionKey[KEY_BIN_LEN];
	int rc = cookie_parse(cookie, &sessionID, sessionKey);
	if(DB_SUCCESS != rc) return rc;

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



/*
SLNSessionRef SLNSessionCreateInternal(SLNSessionCacheRef const cache, uint64_t const sessionID, byte_t const sessionKey[KEY_BIN_LEN], uint64_t const userID, SLNMode const mode);

str_t *SLNSessionCopyCookie(SLNSessionRef const session) {
	if(!session) return NULL;
	str_t hex[KEY_HEX_LEN+1];
	tohex(hex, session->sessionKey, KEY_BIN_LEN);
	hex[KEY_HEX_LEN] = '\0';
	return aasprintf("s=%llu:%s", (unsigned long long)session->sessionID, hex);
}
*/


















