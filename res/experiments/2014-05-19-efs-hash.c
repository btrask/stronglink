
typedef int fd_t;
typedef unsigned char byte_t;
typedef char const str_t;

typedef struct EFSRepo* EFSRepoRef;
typedef struct EFSSubmission* EFSSubmissionRef;
typedef struct EFSHasher* EFSHasherRef;
typedef struct EFSURIList* EFSURIListRef;

struct EFSRepo {
	str_t *path;
	str_t *configPath;
	str_t *dataPath;
	str_t *cachePath;

	// TODO: sqlite3 permissions object? not an actual DB connection.
}

EFSRepoRef EFSRepoCreate(str_t *const path) {
	EFSRepo *const repo = calloc(1, sizeof(struct EFSRepo));
	asprintf(&repo->path, "%s", path);
	asprintf(&repo->configPath, "%s/config.json", path); // TODO: JSON?
	
	return repo;
}
void EFSRepoFree(void) {
	free(repo->path); repo->path = NULL;
	free(repo);
}

struct EFSSession {
	EFSRepoRef repo;
	str_t *user;
	str_t *pass;
	str_t *cookie;
	// sqlite3 database obj
	int userID; // TODO: Use sqlite3-appropriate type.
}

struct EFSSubmission {
	EFSRepoRef repo;
	char *path;
	EFSFileTypeRef type;
	ssize_t size; // TODO: Appropriate type? 64-bit unsigned.
	EFSURIListRef URIs;
}

void EFSRepoCreateSession(EFSRepoRef const repo, str_t *const user, str_t *const pass, str_t *const cookie) {
	EFSSessionRef const session = calloc(1, sizeof(struct EFSSession));
	session->repo = repo;
	asprintf(&session->user, "%s", user);
	asprintf(&session->pass, "%s", pass);
	asprintf(&session->cookie, "%s", cookie);
	// TODO: Connect to sqlite database.
	return session;
}

#include <unistd.h>

EFSSubmissionRef EFSRepoCreateSubmission(EFSRepoRef const repo, str_t *const path, EFSFileTypeRef type, fd_t const stream) {
	EFSSubmissionRef const sub = calloc(1, sizeof(struct EFSSubmission));
	fd_t tmp = -1;
	if(path) {
		asprintf(&sub->path, "%s", path);
	} else {
		asprintf(&sub->path, "/tmp/%s", random); // TODO: Generate random string, use repo to get temp dir.
		tmp = creat(sub->path, 0400);
	}
	asprintf(&sub->type, "%s", type);

	byte_t *const buf = calloc(1024 * 512, 1);
	EFSHasherRef const hasher = EFSHasherCreate(type);
	for(;;) {
		ssize_t const length = read(fd, buf, bufferLength);
		if(-1 == length && EBADF == errno) break; // Closed by client?
		BTErrno(length);
		if(length <= 0) break;

		sub->size += length;
		EFSHasherWrite(hasher, buf, length);
		if(-1 != tmp) write(tmp, buf, length);
		// TODO: Indexing.
	}
	free(buf);
	close(tmp); tmp = -1;
	sub->URIs = EFSHasherCreateURIList(hasher);
	EFSHasherFree(hasher);

	return sub;
}
void EFSSessionAddSubmission(EFSSessionRef const session, EFSSubmissionRef const submission) {
	
}
void EFSSessionFree(EFSSessionRef const session) {
	session->repo = NULL;
	free(session->user); session->user = NULL;
	free(session->pass); session->pass = NULL;
	free(session->cookie); session->cookie = NULL;
	// TODO: Close database connection.
	free(session);
}

#include <openssl/sha.h> // TODO: Switch to libressl

typedef char* EFSFileTypeRef;

struct EFSHasher {
	EFSFileTypeRef type;
	SHA_CTX sha1;
	SHA256_CTX sha256;
}

typedef unsigned int EFSIndex;
typedef unsigned int EFSCount; // TODO: Appropriate types?

typedef char* EFSURIRef;

struct EFSURIList {
	EFSCount len;
	EFSURIRef items[0];
}


EFSHasherRef EFSHasherCreate(EFSFileTypeRef const type) {
	EFSHasherRef const hasher = calloc(1, sizeof(struct EFSHasher));
	asprintf(&hasher->type, "%s", type);
	SHA_Init(hasher->sha1);
	SHA256_Init(hasher->sha256);
	return hasher;
}
void EFSHasherFree(EFSHasherRef const hasher) {
	free(hasher->type); hasher->type = NULL;
	free(hasher);
}
void EFSHasherWrite(EFSHasherRef const hasher, byte_t const *const buf, ssize_t const len) {
	SHA_Update(hasher->sha1, buf, len);
	SHA256_Update(hasher->sha256, buf, len);
}

EFSURIListRef EFSHasherCreateURIList(EFSHasherRef const hasher) {
	byte_t sha1[SHA_DIGEST_LENGTH] = {};
	byte_t sha256[SHA256_DIGEST_LENGTH] = {};
	SHA_Final(sha1, hasher->sha1);
	SHA256_Final(sha256, hasher->sha256); // TODO: Check return values.

	str_t test[] = "asdf"; // TODO: Convert hashes to hex and base64, truncate.

	EFSURIListRef const URIs = calloc(1, sizeof(struct EFSURIList) + sizeof(EFSURIRef) * 2);
	URIs->len = 2;
	EFSIndex i = 0;
	asprintf(&URIs->items[i++], "hash://sha1/%s", test);
//	asprintf(&URIs->items[i++], "hash://sha1/%s", test);
	asprintf(&URIs->items[i++], "hash://sha256/%s", test);
//	asprintf(&URIs->items[i++], "hash://sha256/%s", test);
	return URIs;
}
void EFSURIListFree(EFSURIListRef const list) {
	for(EFSIndex i = 0; i < list->len; ++i) {
		free(list->items[i]); list->items[i] = NULL;
	}
	free(list);
}


