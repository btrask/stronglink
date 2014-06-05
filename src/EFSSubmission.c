#define _GNU_SOURCE
#include <fcntl.h>
#include "EarthFS.h"
#include "HTTPServer.h"

struct EFSSubmission {
	EFSRepoRef repo;
	str_t *path;
	str_t *type;
	ssize_t size; // TODO: Appropriate type? 64-bit unsigned.
	EFSURIListRef URIs;
};

EFSSubmissionRef EFSRepoCreateSubmission(EFSRepoRef const repo, HTTPConnectionRef const conn) {
	if(!repo) return NULL;
	BTAssert(conn, "EFSSubmission connection required");
	EFSSubmissionRef const sub = calloc(1, sizeof(struct EFSSubmission));
	str_t const x[] = "efs-tmp"; // TODO: Generate random filename.
	(void)BTErrno(asprintf(&sub->path, "/tmp/%s", x)); // TODO: Use temp dir from repo.
	fd_t const tmp = BTErrno(creat(sub->path, 0400));

	HTTPHeaderList const *const headers = HTTPConnectionGetHeaders(conn);
	for(index_t i = 0; i < headers->count; ++i) {
		if(0 == strcasecmp("content-type", headers->items[i].field)) {
			sub->type = strdup(headers->items[i].value);
		}
	}

	EFSHasherRef const hasher = EFSHasherCreate(sub->type);
	for(;;) {
		byte_t const *buf = NULL;
		ssize_t const rlen = HTTPConnectionGetBuffer(conn, &buf);
		if(rlen < 0) {
			fprintf(stderr, "EFSSubmission connection read error");
			break;
		}
		if(!rlen) break;

		sub->size += rlen;
		EFSHasherWrite(hasher, buf, rlen);
		(void)BTErrno(write(tmp, buf, rlen));
		// TODO: Indexing.
	}
	(void)BTErrno(close(tmp));
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

err_t EFSSessionAddSubmission(EFSSessionRef const session, EFSSubmissionRef const submission) {
	if(!session) return 0;
	if(!submission) return -1;




	return 0;
}

