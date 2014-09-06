#include <openssl/sha.h> // TODO: Switch to LibreSSL.
#include "EarthFS.h"

static str_t *tohex(byte_t const *const buf, size_t const len) {
	str_t const map[] = "0123456789abcdef";
	str_t *const hex = calloc(len*2+1, 1);
	for(off_t i = 0; i < len; ++i) {
		hex[i*2+0] = map[0xf & (buf[i] >> 4)];
		hex[i*2+1] = map[0xf & (buf[i] >> 0)];
	}
	return hex;
}

struct EFSHasher {
	str_t *type;
	SHA_CTX sha1;
	SHA256_CTX sha256;
	str_t *internalHash;
};

EFSHasherRef EFSHasherCreate(strarg_t const type) {
	assertf(type, "EFSHasher type required");
	EFSHasherRef hasher = calloc(1, sizeof(struct EFSHasher));
	if(!hasher) return NULL;
	hasher->type = strdup(type);
	if(
		SHA1_Init(&hasher->sha1) < 0 ||
		SHA256_Init(&hasher->sha256) < 0
	) {
		EFSHasherFree(&hasher);
		return NULL;
	};
	return hasher;
}
void EFSHasherFree(EFSHasherRef *const hasherptr) {
	EFSHasherRef hasher = *hasherptr;
	if(!hasher) return;
	FREE(&hasher->type);
	FREE(&hasher->internalHash);
	FREE(hasherptr); hasher = NULL;
}
err_t EFSHasherWrite(EFSHasherRef const hasher, byte_t const *const buf, size_t const len) {
	if(!hasher) return 0;
	if(!len) return 0;
	assertf(buf, "Buffer required");
	if(SHA1_Update(&hasher->sha1, buf, len) < 0) return -1;
	if(SHA256_Update(&hasher->sha256, buf, len) < 0) return -1;
	return 0;
}

str_t **EFSHasherEnd(EFSHasherRef const hasher) {
	if(!hasher) return NULL;

	byte_t sha1[SHA_DIGEST_LENGTH];
	byte_t sha256[SHA256_DIGEST_LENGTH];
	if(SHA1_Final(sha1, &hasher->sha1) < 0) return NULL;
	if(SHA256_Final(sha256, &hasher->sha256) < 0) return NULL;
	str_t *sha1hex = tohex(sha1, SHA_DIGEST_LENGTH);
	str_t *sha256hex = tohex(sha256, SHA256_DIGEST_LENGTH);
	if(!sha1hex || !sha256hex) {
		FREE(&sha1hex);
		FREE(&sha256hex);
		return NULL;
	}
	// TODO: Base64.

	hasher->internalHash = strdup(sha256hex);

	// TODO: Plugins, dynamic size.
	str_t **const URIs = malloc(sizeof(str_t *) * (4+1));
	if(!URIs) return NULL;

	// Make sure the preferred URI (e.g. the one used for internalHash) is first.
	URIs[0] = EFSFormatURI("sha256", sha256hex);
	sha256hex[24] = '\0';
	URIs[1] = EFSFormatURI("sha256", sha256hex);

	URIs[2] = EFSFormatURI("sha1", sha1hex);
	sha1hex[24] = '\0';
	URIs[3] = EFSFormatURI("sha1", sha1hex);

	URIs[4] = NULL;

	FREE(&sha1hex);
	FREE(&sha256hex);

	return URIs;
}
strarg_t EFSHasherGetInternalHash(EFSHasherRef const hasher) {
	if(!hasher) return NULL;
	return hasher->internalHash;
}

