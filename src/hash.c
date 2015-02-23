#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "hash.h"

#define HASH_KEY(hash, x) ((hash)->keys + ((hash)->keylen * (x)))

char hash_salt[HASH_SALT_SIZE] = {};

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
	if(HASH_NOTFOUND == x) return x;
	size_t i = x;
	for(;;) {
		if(0 == hash_bucket_empty(hash, i)) break;
		if(0 == hash_bucket_match(hash, i, key)) return i;
		i = (i + 1) % hash->count;
		if(x == i) return HASH_NOTFOUND;
	}
	return HASH_NOTFOUND;
}
size_t hash_set(hash_t *const hash, char const *const key) {
	size_t const x = hash_func(hash, key);
	if(HASH_NOTFOUND == x) return x;
	size_t i = x;
	for(;;) {
		if(0 == hash_bucket_empty(hash, i)) break;
		if(0 == hash_bucket_match(hash, i, key)) return i;
		i = (i + 1) % hash->count;
		if(x == i) return HASH_NOTFOUND;
	}
	memcpy(HASH_KEY(hash, i), key, hash->keylen);
	return i;
}

void hash_del(hash_t *const hash, char const *const key, char *const data, size_t const dlen) {
	size_t const x = hash_get(hash, key);
	hash_del_offset(hash, x, data, dlen);
}
void hash_del_offset(hash_t *const hash, size_t const x, char *const data, size_t const dlen) {
	if(x >= hash->count) return;
	size_t const moved = hash_del_keyonly(hash, x);
	if(!data) return;
	size_t const part2 = (x + moved) % hash->count;
	size_t const part1 = x + moved - part2;
	memmove(data + (dlen * x), data + (dlen * (x+1)), part1 * dlen);
	if(part2) {
		memcpy(data + (dlen * (hash->count-1)), data + 0, dlen);
		memmove(data + 0, data + (dlen * 1), (part2-1) * dlen);
	}
}

size_t hash_func(hash_t *const hash, char const *const key) {
	return 0; // TODO
}
int hash_bucket_empty(hash_t *const hash, size_t const x) {
	assert(x < hash->count);
	return nulcmp(HASH_KEY(hash, x), hash->keylen);
}
int hash_bucket_match(hash_t *const hash, size_t const x, char const *const key) {
	assert(x < hash->count);
	return memcmp(HASH_KEY(hash, x), key, hash->keylen);
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

