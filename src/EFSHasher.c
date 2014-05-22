#define _GNU_SOURCE // For asprintf().
#include <stdio.h>
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
};
struct EFSURIList {
	EFSCount count;
	str_t *items[0];
};

EFSHasherRef EFSHasherCreate(str_t const *const type) {
	BTAssert(type, "EFSHasher type required");
	EFSHasherRef const hasher = calloc(1, sizeof(struct EFSHasher));
	hasher->type = strdup(type);
	(void)BTErrno(SHA1_Init(&hasher->sha1));
	(void)BTErrno(SHA256_Init(&hasher->sha256));
	return hasher;
}
void EFSHasherFree(EFSHasherRef const hasher) {
	if(!hasher) return;
	free(hasher->type); hasher->type = NULL;
	free(hasher);
}
void EFSHasherWrite(EFSHasherRef const hasher, byte_t const *const buf, ssize_t const len) {
	if(!hasher) return;
	if(!len) return;
	BTAssert(buf, "Buffer required");
	(void)BTErrno(SHA1_Update(&hasher->sha1, buf, len));
	(void)BTErrno(SHA256_Update(&hasher->sha256, buf, len));
}

EFSURIListRef EFSHasherCreateURIList(EFSHasherRef const hasher) {
	if(!hasher) return NULL;

	byte_t sha1[SHA_DIGEST_LENGTH] = {};
	byte_t sha256[SHA256_DIGEST_LENGTH] = {};
	(void)BTErrno(SHA1_Final(sha1, &hasher->sha1));
	(void)BTErrno(SHA256_Final(sha256, &hasher->sha256));
	str_t *const sha1h = tohex(sha1, SHA_DIGEST_LENGTH);
	str_t *const sha256h = tohex(sha256, SHA256_DIGEST_LENGTH);
	// TODO: Base64.

	EFSURIListRef const URIs = calloc(1, sizeof(struct EFSURIList) + sizeof(str_t *) * 4);
	URIs->count = 4;
	EFSIndex i = 0;
	(void)BTErrno(asprintf(&URIs->items[i++], "hash://sha1/%s", sha1h));
	(void)BTErrno(asprintf(&URIs->items[i++], "hash://sha1/%.16s", sha1h));
	(void)BTErrno(asprintf(&URIs->items[i++], "hash://sha256/%s", sha256h));
	(void)BTErrno(asprintf(&URIs->items[i++], "hash://sha256/%.24s", sha256h));

	free(sha1h);
	free(sha256h);

	return URIs;
}
void EFSURIListFree(EFSURIListRef const list) {
	if(!list) return;
	for(EFSIndex i = 0; i < list->count; ++i) {
		free(list->items[i]); list->items[i] = NULL;
	}
	free(list);
}
EFSCount EFSURIListGetCount(EFSURIListRef const list) {
	if(!list) return 0;
	return list->count;
}
str_t const *EFSURIListGetURI(EFSURIListRef const list, EFSIndex const x) {
	if(!list) return NULL;
	BTAssert(x < list->count, "Invalid index");
	return list->items[x];
}

