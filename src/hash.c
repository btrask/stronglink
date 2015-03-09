#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../deps/smhasher/MurmurHash3.h"
#include "hash.h"

#define HASH_KEY(hash, x) ((hash)->keys + ((hash)->keylen * (x)))

uint32_t hash_salt = 0;

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
	if(!hash->keys) return -1;
	return 0;
}
void hash_destroy(hash_t *const hash) {
	if(!hash) return;
	hash->count = 0;
	hash->keylen = 0;
	free(hash->keys); hash->keys = NULL;
}

size_t hash_get(hash_t *const hash, char const *const key) {
	size_t const x = hash_func(hash, key);
	size_t i = x;
	for(;;) {
		if(0 == hash_bucket_match(hash, i, key)) return i;
		if(0 == hash_bucket_empty(hash, i)) break;
		i = (i + 1) % hash->count;
		if(x == i) return HASH_NOTFOUND;
	}
	return HASH_NOTFOUND;
}
size_t hash_set(hash_t *const hash, char const *const key) {
	size_t const x = hash_func(hash, key);
	size_t i = x;
	for(;;) {
		if(0 == hash_bucket_empty(hash, i)) break;
		if(0 == hash_bucket_match(hash, i, key)) return i;
		i = (i + 1) % hash->count;
		if(x == i) return HASH_NOTFOUND;
	}
	hash_set_raw(hash, i, key);
	return i;
}

size_t hash_del(hash_t *const hash, char const *const key, void *const values, size_t const len) {
	size_t const x = hash_get(hash, key);
	if(HASH_NOTFOUND == x) return x;
	hash_del_offset(hash, x, values, len);
	return 1;
}
void hash_del_offset(hash_t *const hash, size_t const x, void *const values, size_t const len) {
	assert(x < hash->count);
	size_t const moved = hash_del_keyonly(hash, x);
	if(!moved) return;
	if(!values) return;
	char *const data = values;
	size_t const part2 = (x + moved) % hash->count;
	size_t const part1 = x + moved - part2;
	memmove(data + (len * x), data + (len * (x+1)), part1 * len);
	if(!part2) return;
	memcpy(data + (len * (hash->count-1)), data + 0, len);
	memmove(data + 0, data + (len * 1), (part2-1) * len);
}

size_t hash_func(hash_t *const hash, char const *const key) {
	uint32_t x = 0;
	MurmurHash3_x86_32(key, hash->keylen, hash_salt, &x);
	return x % hash->count;
}
int hash_bucket_empty(hash_t *const hash, size_t const x) {
	assert(x < hash->count);
	return nulcmp(HASH_KEY(hash, x), hash->keylen);
}
int hash_bucket_match(hash_t *const hash, size_t const x, char const *const key) {
	assert(x < hash->count);
	assert(key);
	return memcmp(HASH_KEY(hash, x), key, hash->keylen);
}
void hash_set_raw(hash_t *const hash, size_t const x, char const *const key) {
	assert(x < hash->count);
	assert(key);
	memcpy(HASH_KEY(hash, x), key, hash->keylen);
}
size_t hash_del_keyonly(hash_t *const hash, size_t const x) {
	assert(x < hash->count);
	size_t i = x;
	for(;;) {
		size_t const next = (i + 1) % hash->count;
		if(x == next) break;
		if(0 == hash_bucket_empty(hash, next)) break;
		size_t const alt = hash_func(hash, HASH_KEY(hash, next));
		if(next == alt) break;
		memcpy(HASH_KEY(hash, i), HASH_KEY(hash, next), hash->keylen);
		i = next;
	}
	memset(HASH_KEY(hash, i), 0, hash->keylen);
	return (hash->count + i - x) % hash->count;
}

