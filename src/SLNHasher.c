#include <openssl/sha.h>
#include "StrongLink.h"

struct SLNHasher {
	str_t *type;
	SHA_CTX sha1;
	SHA256_CTX sha256;
	str_t *internalHash;
};

SLNHasherRef SLNHasherCreate(strarg_t const type) {
	assertf(type, "SLNHasher type required");
	SLNHasherRef hasher = calloc(1, sizeof(struct SLNHasher));
	if(!hasher) return NULL;
	hasher->type = strdup(type);

	// Note: Support for old/weak algorithms is important for old files
	// that have links using those algorithms. The algorithm we use
	// internally is currently SHA-256.
	// TODO: Plugin support.
	int rc = 0;
	rc = rc < 0 ? rc : SHA256_Init(&hasher->sha256);
	rc = rc < 0 ? rc : SHA1_Init(&hasher->sha1);
	if(rc < 0) {
		SLNHasherFree(&hasher);
		return NULL;
	};

	return hasher;
}
void SLNHasherFree(SLNHasherRef *const hasherptr) {
	SLNHasherRef hasher = *hasherptr;
	if(!hasher) return;
	FREE(&hasher->type);
	memset(&hasher->sha1, 0, sizeof(hasher->sha1));
	memset(&hasher->sha256, 0, sizeof(hasher->sha256));
	FREE(&hasher->internalHash);
	assert_zeroed(hasher, 1);
	FREE(hasherptr); hasher = NULL;
}
int SLNHasherWrite(SLNHasherRef const hasher, byte_t const *const buf, size_t const len) {
	if(!hasher) return 0;
	if(!len) return 0;
	assertf(buf, "Buffer required");
	if(SHA1_Update(&hasher->sha1, buf, len) < 0) return -1;
	if(SHA256_Update(&hasher->sha256, buf, len) < 0) return -1;
	return 0;
}

str_t **SLNHasherEnd(SLNHasherRef const hasher) {
	if(!hasher) return NULL;

	byte_t sha1[SHA_DIGEST_LENGTH];
	byte_t sha256[SHA256_DIGEST_LENGTH];
	if(SHA1_Final(sha1, &hasher->sha1) < 0) return NULL;
	if(SHA256_Final(sha256, &hasher->sha256) < 0) return NULL;
	str_t *sha1hex = tohexstr(sha1, SHA_DIGEST_LENGTH);
	str_t *sha256hex = tohexstr(sha256, SHA256_DIGEST_LENGTH);
	if(!sha1hex || !sha256hex) {
		FREE(&sha1hex);
		FREE(&sha256hex);
		return NULL;
	}
	// TODO: Base64.

	hasher->internalHash = strdup(sha256hex);

	// TODO: Plugins, dynamic size.
	str_t **const URIs = malloc(sizeof(str_t *) * (4+1));
	if(!URIs) {
		FREE(&sha1hex);
		FREE(&sha256hex);
		return NULL;
	}

	// Make sure the preferred URI (e.g. the one used for internalHash) is first.
	URIs[0] = SLNFormatURI("sha256", sha256hex);
	sha256hex[24] = '\0';
	URIs[1] = SLNFormatURI("sha256", sha256hex);

	URIs[2] = SLNFormatURI("sha1", sha1hex);
	sha1hex[24] = '\0';
	URIs[3] = SLNFormatURI("sha1", sha1hex);

	URIs[4] = NULL;

	FREE(&sha1hex);
	FREE(&sha256hex);

	return URIs;
}
strarg_t SLNHasherGetInternalHash(SLNHasherRef const hasher) {
	if(!hasher) return NULL;
	return hasher->internalHash;
}

