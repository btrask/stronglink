#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../deps/smhasher/MurmurHash3.h"
#include "hash.h"

#define HASH_POS(hash, x) ((x) % (hash)->count)
#define HASH_KEY(hash, x) ((hash)->keys + ((hash)->keylen * HASH_POS(hash, x)))

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
	for(size_t i = x; i < x+hash->count; i++) {
		size_t const j = HASH_POS(hash, i);
		if(0 == hash_bucket_match(hash, j, key)) return j;
		if(0 == hash_bucket_empty(hash, j)) break;
	}
	return HASH_NOTFOUND;
}
size_t hash_set(hash_t *const hash, char const *const key) {
	size_t const x = hash_func(hash, key);
	for(size_t i = x; i < x+hash->count; i++) {
		size_t const j = HASH_POS(hash, i);
		if(0 == hash_bucket_empty(hash, j)) {
			hash_set_raw(hash, j, key);
			return j;
		}
		if(0 == hash_bucket_match(hash, j, key)) {
			return j;
		}
	}
	return HASH_NOTFOUND;
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
	if(HASH_NOTFOUND == moved) return;
	if(!values) return;
	assert(len);
	char *const data = values;
	memcpy(data + (len * x), data + (len * moved), len);
	memset(data + (len * moved), 0, len);
}

size_t hash_func(hash_t *const hash, char const *const key) {
	uint32_t x = 0;
	MurmurHash3_x86_32(key, hash->keylen, hash_salt, &x);
	return HASH_POS(hash, x);
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
static size_t fill_gap(hash_t *const hash, size_t const x, size_t const y) {
	assert(x < hash->count);
	size_t i = y;
	while(i-- > x+1) {
		size_t alt = hash_func(hash, HASH_KEY(hash, i));
		if((alt > x && alt <= i) || (alt < x && alt+hash->count <= i)) {
//			fprintf(stderr, "%zu in {%zu, %zu}\n", alt, x, i);
			continue;
		}
//		fprintf(stderr, "%zu not in {%zu, %zu}\n", alt, x, i);
		memcpy(HASH_KEY(hash, x), HASH_KEY(hash, i), hash->keylen);
		return fill_gap(hash, HASH_POS(hash, i), y);
	}
	memset(HASH_KEY(hash, x), 0, hash->keylen);
	return HASH_NOTFOUND;
}
size_t hash_del_keyonly(hash_t *const hash, size_t const x) {
	assert(x < hash->count);
	size_t i;
	for(i = x; i < x+hash->count; i++) {
		if(0 == hash_bucket_empty(hash, HASH_POS(hash, i))) break;
	}
	return fill_gap(hash, x, i);
}

