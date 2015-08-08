// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <openssl/sha.h>
#include "StrongLink.h"

#define HASHLEN_MIN 8 // Sanity check.
#define HASHLEN_SHORT 12 // Safe against accidental collisions.
#define HASHLEN_MEDIUM 24 // Safe against malicious collisions.
#define HASHLEN_LONG 32 // Longest reasonable, hex form fits 80-char line.
#define HASHLEN_MAX 128 // Should be enough for anyone.

// Note: Support for old/weak algorithms is important for old files
// that have links using those algorithms. The algorithm we use
// internally is defined by SLN_INTERNAL_ALGO.
static SLNAlgo const *const algos[];
static size_t const algocount;

struct SLNHasher {
	str_t *type;
	size_t count;
	void **algos;
	str_t *internalHash;
};

SLNHasherRef SLNHasherCreate(strarg_t const type) {
	if(!type) return NULL;
	SLNHasherRef hasher = calloc(1, sizeof(struct SLNHasher));
	if(!hasher) return NULL;

	hasher->type = strdup(type);
	hasher->count = algocount;
	hasher->algos = calloc(hasher->count, sizeof(hasher->algos[0]));
	if(!hasher->type || !hasher->algos) {
		SLNHasherFree(&hasher);
		return NULL;
	}

	for(size_t i = 0; i < hasher->count; i++) {
		int rc = algos[i]->init(type, &hasher->algos[i]);
		if(rc < 0) {
			SLNHasherFree(&hasher);
			return NULL;
		}
	}

	return hasher;
}
void SLNHasherFree(SLNHasherRef *const hasherptr) {
	SLNHasherRef hasher = *hasherptr;
	if(!hasher) return;
	FREE(&hasher->type);
	if(hasher->algos) {
		for(size_t i = 0; i < hasher->count; i++) {
			(void) algos[i]->final(hasher->algos[i], NULL, 0);
			hasher->algos[i] = NULL;
		}
		assert_zeroed(hasher->algos, hasher->count);
		FREE(&hasher->algos);
	}
	hasher->count = 0;
	FREE(&hasher->internalHash);
	assert_zeroed(hasher, 1);
	FREE(hasherptr); hasher = NULL;
}
int SLNHasherWrite(SLNHasherRef const hasher, byte_t const *const buf, size_t const len) {
	if(!hasher) return 0;
	if(!len) return 0;
	assert(buf);
	for(size_t i = 0; i < hasher->count; i++) {
		int rc = algos[i]->update(hasher->algos[i], buf, len);
		if(rc < 0) return -1;
	}
	return 0;
}

str_t **SLNHasherEnd(SLNHasherRef const hasher) {
	if(!hasher) return NULL;

	size_t x = 0;
	size_t const count = hasher->count * 4;
	str_t **URIs = calloc(count+1, sizeof(str_t *));
	if(!URIs) return NULL;

	for(size_t i = 0; i < hasher->count; i++) {
		byte_t bin[HASHLEN_MAX];
		ssize_t len = algos[i]->final(hasher->algos[i], bin, sizeof(bin));
		hasher->algos[i] = NULL;
		if(len < 0) goto cleanup;
		if(len < HASHLEN_MIN) goto cleanup; // Not allowed.

		str_t hex[sizeof(bin)*2+1];
		tohex(hex, bin, len);
		hex[len*2] = '\0';
		if(0 == strcmp(SLN_INTERNAL_ALGO, algos[i]->name)) {
			if(hasher->internalHash) goto cleanup; // Duplicate...?
			char const c = hex[HASHLEN_LONG*2];
			hex[HASHLEN_LONG*2] = '\0';
			hasher->internalHash = strdup(hex);
			hex[HASHLEN_LONG*2] = c;
			if(!hasher->internalHash) goto cleanup;
		}

		if(len >= HASHLEN_LONG) {
			char const c = hex[HASHLEN_LONG*2];
			hex[HASHLEN_LONG*2] = '\0';
			URIs[x] = SLNFormatURI(algos[i]->name, hex);
			hex[HASHLEN_LONG*2] = c;
			if(!URIs[x]) goto cleanup;
			x++;
		}
		if(len >= HASHLEN_MEDIUM) {
			char const c = hex[HASHLEN_MEDIUM*2];
			hex[HASHLEN_MEDIUM*2] = '\0';
			URIs[x] = SLNFormatURI(algos[i]->name, hex);
			hex[HASHLEN_MEDIUM*2] = c;
			if(!URIs[x]) goto cleanup;
			x++;
		}
		if(len >= HASHLEN_SHORT) {
			char const c = hex[HASHLEN_SHORT*2];
			hex[HASHLEN_SHORT*2] = '\0';
			URIs[x] = SLNFormatURI(algos[i]->name, hex);
			hex[HASHLEN_SHORT*2] = c;
			if(!URIs[x]) goto cleanup;
			x++;
		}
		if(len >= 0) {
			char const c = hex[HASHLEN_MAX*2];
			hex[HASHLEN_MAX*2] = '\0';
			URIs[x] = SLNFormatURI(algos[i]->name, hex);
			hex[HASHLEN_MAX*2] = c;
			if(!URIs[x]) goto cleanup;
			x++;
		}

		// TODO: base-64 support.
	}
	if(!hasher->internalHash) goto cleanup;

	URIs[x] = NULL;

	return URIs;

cleanup:
	for(size_t i = 0; i < count; i++) {
		FREE(&URIs[i]);
	}
	assert_zeroed(URIs, count);
	FREE(&URIs);
	FREE(&hasher->internalHash);
	return NULL;
}
strarg_t SLNHasherGetInternalHash(SLNHasherRef const hasher) {
	if(!hasher) return NULL;
	return hasher->internalHash;
}


static int sha1init(char const *const type, void **const algo) {
	assert(algo);
	*algo = calloc(1, sizeof(SHA_CTX));
	if(!*algo) return -1;
	int rc = SHA_Init(*algo);
	if(rc < 0) return rc;
	return 0;
}
static int sha1update(void *const ctx, byte_t const *const buf, size_t const len) {
	if(!ctx) return 0;
	return SHA1_Update(ctx, buf, len);
}
static ssize_t sha1final(void *const ctx, byte_t *const out, size_t const max) {
	if(!ctx) return 0;
	if(max < SHA_DIGEST_LENGTH) {
		free(ctx);
		return -1;
	}
	int rc = SHA1_Final(out, ctx);
	free(ctx);
	if(rc < 0) return -1;
	return SHA_DIGEST_LENGTH;
}
static SLNAlgo const sha1 = {
	.name = "sha1",
	.init = sha1init,
	.update = sha1update,
	.final = sha1final,
};

static int sha256init(char const *const type, void **const algo) {
	assert(algo);
	*algo = calloc(1, sizeof(SHA256_CTX));
	if(!*algo) return -1;
	int rc = SHA256_Init(*algo);
	if(rc < 0) return rc;
	return 0;
}
static int sha256update(void *const ctx, byte_t const *const buf, size_t const len) {
	if(!ctx) return 0;
	return SHA256_Update(ctx, buf, len);
}
static ssize_t sha256final(void *const ctx, byte_t *const out, size_t const max) {
	if(!ctx) return 0;
	if(max < SHA256_DIGEST_LENGTH) {
		free(ctx);
		return -1;
	}
	int rc = SHA256_Final(out, ctx);
	free(ctx);
	if(rc < 0) return -1;
	return SHA256_DIGEST_LENGTH;
}
static SLNAlgo const sha256 = {
	.name = "sha256",
	.init = sha256init,
	.update = sha256update,
	.final = sha256final,
};

static int sha512init(char const *const type, void **const algo) {
	assert(algo);
	*algo = calloc(1, sizeof(SHA512_CTX));
	if(!*algo) return -1;
	int rc = SHA512_Init(*algo);
	if(rc < 0) return rc;
	return 0;
}
static int sha512update(void *const ctx, byte_t const *const buf, size_t const len) {
	if(!ctx) return 0;
	return SHA512_Update(ctx, buf, len);
}
static ssize_t sha512final(void *const ctx, byte_t *const out, size_t const max) {
	if(!ctx) return 0;
	if(max < SHA512_DIGEST_LENGTH) {
		free(ctx);
		return -1;
	}
	int rc = SHA512_Final(out, ctx);
	free(ctx);
	if(rc < 0) return -1;
	return SHA512_DIGEST_LENGTH;
}
static SLNAlgo const sha512 = {
	.name = "sha512",
	.init = sha512init,
	.update = sha512update,
	.final = sha512final,
};

static SLNAlgo const *const algos[] = {
	&sha1,
	&sha256,
	&sha512,
};
static size_t const algocount = numberof(algos);

