/*


- http headers hash table
- pushes hash table?
- also incidentally the session cookie hash table...

slightly different requirements
pushes and sessions are persistent/global (well per-repo)
headers are extremely temporary

pushes and sessions also need to be quite large
like lets say 1000 concurrent pushes
10,000 concurrent sessions
it would be nice for these to not be hard caps

technically the sessions table is just a cache
so if it gets dropped thats slower but fine

the pushes table...?
well it isnt a cache
each pushed file needs to find the push object for coordination


okay so for the headers hash table
- we can have a fixed size list of 20 pointers
- plus a growable buffer for bump-allocating the actual fields and values
- actually we dont even need pointers, just use 16-bit offsets
- gotta store the hash or part of the key inline too though, otherwise whats the point?
- we could have inline keys at 14 bytes each, plus two bytes for payload offset
- thats 320 bytes... fair amount per connection
- actually not really if its on the stack...?
- well even if its not on the stack its still not that much

okay so thats our quick and dirty hash table
is it something we can reuse?


okay next up
pushes
we can definitely cap the push id length

the payload should just be a pointer to the push/sync object
and it'd be nice not to have a layer of indirection

thus dynamic payload sizing

these hash tables need to support deletion...
but then how do we know to keep searching subsequent buckets?

also what about total hash size?

16-byte push ids, 8 byte pointers
times 1000 simultaneous pushes max
24k?

thats not bad

also, i now see that the pointers should be stored separately, not interleaved
thats fine of course

or, instead of storing payloads at all
what if the hash table just returned an index?

okay, when deleting an item
rehash the subsequent item and move it if it was in the wrong place
then keep going until we find an empty slot or a hash doesnt move



*/



#define HASH_NOTFOUND (SIZE_MAX-1)

#define SALT_SIZE 16
extern char hash_salt[SALT_SIZE]; /* Set this first */

typedef struct {
	size_t count;
	size_t keylen;
	char *keys;
} hash_t;

int hash_init(hash_t *const hash, size_t const count, size_t const keylen);
void hash_destroy(hash_t *const hash);

/* Returns index of key (bring your own payload storage) */
size_t hash_get(hash_t *const hash, char const *const key);
size_t hash_set(hash_t *const hash, char const *const key);

/* Updates external data array for you (elements must be fixed size) */
void hash_del(hash_t *const hash, char const *const key, char const *const data, size_t const dlen);

/* Returns number of subsequent elements moved */
size_t hash_del_internal(hash_t *const hash, size_t const x);





#define HASH_KEY(hash, x) ((hash)->keys + ((hash)->keylen * (x)))

char hash_salt[SALT_SIZE] = {};
static size_t hashfunc(hash_t *const hash, char const *const key) {
	return 0; // TODO
}

static int nulcmp(char const *const buf, size_t const len) {
	for(size_t i = 0; i < len; ++i) {
		if(buf[i]) return -1;
	}
	return 0;
}

int hash_init(hash_t *const hash, size_t const count, size_t const keylen) {
	assert(hash);
	hash->count = count;
	hash->keylen = keylen;
	hash->keys = calloc(count, keylen);
	return hash;
}
void hash_destroy(hash_t *const hash) {
	if(!hash) return;
	hash->count = 0;
	hash->keylen = 0;
	free(hash->keys); hash->keys = NULL;
}

size_t hash_get(hash_t *const hash, char const *const key) {
	size_t const x = hashfunc(hash, key);
	if(HASH_NOTFOUND == x) return x;
	size_t i = x;
	for(;;) {
		char const *const k = HASH_KEY(hash, i);
		if(0 == nulcmp(k, hash->keylen)) break;
		if(0 == memcmp(k, key, hash->keylen)) return i;
		i = (i + 1) % hash->count;
		if(x == i) return HASH_NOTFOUND;
	}
	return HASH_NOTFOUND;
}
size_t hash_set(hash_t *const hash, char const *const key) {
	size_t const x = hashfunc(hash, key);
	if(HASH_NOTFOUND == x) return x;
	size_t i = x;
	for(;;) {
		char const *const k = HASH_KEY(hash, i);
		if(0 == nulcmp(k, hash->keylen)) break;
		if(0 == memcmp(k, key, hash->keylen)) return i;
		i = (i + 1) % hash->count;
		if(x == i) return HASH_NOTFOUND;
	}
	memcpy(HASH_KEY(hash, i), key, hash->keylen);
	return i;
}
void hash_del(hash_t *const hash, char const *const key, char const *const data, size_t const dlen) {
	size_t const x = hash_get(hash, key);
	if(HASH_NOTFOUND == x) return;
	size_t const moved = hash_del(hash, x);
	if(!data) return;
	size_t const part2 = (x + moved) % hash->count;
	size_t const part1 = x + moved - part2;
	memmove(data + (dlen * x), data + (dlen * (x+1)), part1 * dlen);
	if(part2) {
		memcpy(data + (dlen * (hash->count-1)), data + 0, dlen);
		memmove(data + 0, data + (dlen * 1), (part2-1) * dlen);
	}
}
size_t hash_del_internal(hash_t *const hash, size_t const x) {
	if(x >= hash->count) return 0;
	size_t i = x;
	for(;;) {
		size_t const next = (i + 1) % hash->count;
		if(x == next) break;
		char const *const k = HASH_KEY(hash, next);
		if(0 == nulcmp(k, hash->keylen)) break;
		size_t const alt = hashfunc(hash, k);
		if(next == alt) break;
		memcpy(HASH_KEY(hash, i), k, hash->keylen);
		i = next;
	}
	memset(HASH_KEY(hash, i), 0, hash->keylen);
	return (hash->count + i - x) % hash->count;
}


// does this need to be a counted hash?
// well, counts can be maintained outside of the hash itself, if the user needs it
// but we also need to track which buckets are empty
// and we cant do that without either some sort of flag or defining all zeroes as empty

// uh oh
// delete can move keys
// but the values are stored externally

// we should redefine delete to take a key position
// and then return the number of subsequent keys that moved


























