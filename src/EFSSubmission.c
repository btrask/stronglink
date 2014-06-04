#define _GNU_SOURCE
#include <fcntl.h>
#include "EarthFS.h"

#define BUFFER_SIZE (1024 * 512)

struct EFSSubmission {
	EFSRepoRef repo;
	str_t *path;
	str_t *type;
	ssize_t size; // TODO: Appropriate type? 64-bit unsigned.
	EFSURIListRef URIs;
};

EFSSubmissionRef EFSRepoCreateSubmission(EFSRepoRef const repo, strarg_t const path, strarg_t const type, fd_t const stream) {
	if(!repo) return NULL;
	BTAssert(type, "EFSSubmission type required");
	BTAssert(-1 != stream, "EFSSubmission stream required");
	EFSSubmissionRef const sub = calloc(1, sizeof(struct EFSSubmission));
	fd_t tmp = -1;
	if(path) {
		sub->path = strdup(path);
	} else {
		str_t x[5] = "";
		(void)BTErrno(asprintf(&sub->path, "/tmp/%s", x)); // TODO: Generate random string, use repo to get temp dir.
		tmp = BTErrno(creat(sub->path, 0400));
	}
	sub->type = strdup(type);

	byte_t buf[BUFFER_SIZE] = {};
	EFSHasherRef const hasher = EFSHasherCreate(type);
	for(;;) {
		ssize_t const length = read(stream, buf, BUFFER_SIZE);
		if(-1 == length && EBADF == errno) break; // Closed by client?
		(void)BTErrno(length);
		if(length <= 0) break;

		sub->size += length;
		EFSHasherWrite(hasher, buf, length);
		if(-1 != tmp) write(tmp, buf, length);
		// TODO: Indexing.
	}
	(void)BTErrno(close(tmp)); tmp = -1;
	sub->URIs = EFSHasherCreateURIList(hasher);
	EFSHasherFree(hasher);

	return sub;
}
void EFSSubmissionFree(EFSSubmissionRef const sub) {
	if(!sub) return;
	FREE(&sub->path);
	FREE(&sub->type);
	EFSURIListFree(sub->URIs); sub->URIs = NULL;
	free(sub);
}

void EFSSessionAddSubmission(EFSSessionRef const session, EFSSubmissionRef const submission) {
	if(!session) return;
	if(!submission) return;
	
}

