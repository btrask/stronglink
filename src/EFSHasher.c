#define _GNU_SOURCE // For asprintf().
#include <openssl/sha.h> // TODO: Switch to LibreSSL.
#include "EarthFS.h"

static str_t *tohex(byte_t const *const buf, ssize_t const len) {
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
	EFSHasherRef const hasher = calloc(1, sizeof(struct EFSHasher));
	hasher->type = strdup(type);
	if(
		SHA1_Init(&hasher->sha1) < 0 ||
		SHA256_Init(&hasher->sha256) < 0
	) {
		EFSHasherFree(hasher);
		return NULL;
	};
	return hasher;
}
void EFSHasherFree(EFSHasherRef const hasher) {
	if(!hasher) return;
	FREE(&hasher->type);
	FREE(&hasher->internalHash);
	free(hasher);
}
err_t EFSHasherWrite(EFSHasherRef const hasher, byte_t const *const buf, size_t const len) {
	if(!hasher) return 0;
	if(!len) return 0;
	assertf(buf, "Buffer required");
	if(SHA1_Update(&hasher->sha1, buf, len) < 0) return -1;
	if(SHA256_Update(&hasher->sha256, buf, len) < 0) return -1;
	return 0;
}

URIListRef EFSHasherEnd(EFSHasherRef const hasher) {
	if(!hasher) return NULL;

	byte_t sha1[SHA_DIGEST_LENGTH] = {};
	byte_t sha256[SHA256_DIGEST_LENGTH] = {};
	if(SHA1_Final(sha1, &hasher->sha1) < 0) return NULL;
	if(SHA256_Final(sha256, &hasher->sha256) < 0) return NULL;
	str_t *sha1h = tohex(sha1, SHA_DIGEST_LENGTH);
	str_t *sha256h = tohex(sha256, SHA256_DIGEST_LENGTH);
	if(!sha1h || !sha256h) {
		FREE(&sha1h);
		FREE(&sha256h);
		return NULL;
	}
	// TODO: Base64.

	hasher->internalHash = strdup(sha256h);

	URIListRef const URIs = URIListCreate();
	str_t *URI;
	size_t len;

	// Make sure the preferred URI (e.g. the one used for internalHash) is first.
	// TODO: Get rid of asprintf()
	len = asprintf(&URI, "hash://sha256/%s", sha256h);
	if(len > 0) URIListAddURI(URIs, URI, len);
	if(len >= 0) FREE(&URI);
	len = asprintf(&URI, "hash://sha256/%.24s", sha256h);
	if(len > 0) URIListAddURI(URIs, URI, len);
	if(len >= 0) FREE(&URI);
	len = asprintf(&URI, "hash://sha1/%s", sha1h);
	if(len > 0) URIListAddURI(URIs, URI, len);
	if(len >= 0) FREE(&URI);
	len = asprintf(&URI, "hash://sha1/%.16s", sha1h);
	if(len > 0) URIListAddURI(URIs, URI, len);
	if(len >= 0) FREE(&URI);

	FREE(&sha1h);
	FREE(&sha256h);

	return URIs;
}
strarg_t EFSHasherGetInternalHash(EFSHasherRef const hasher) {
	if(!hasher) return NULL;
	return hasher->internalHash;
}

