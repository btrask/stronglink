#define _GNU_SOURCE
#include <fcntl.h>
#include "async.h"
#include "EarthFS.h"
#include "HTTPServer.h"

// TODO: Find a home for these.
static err_t mkdirp(str_t *const path, size_t const len, int const mode);
#define QUERY(db, str) ({ \
	sqlite3_stmt *__stmt = NULL; \
	str_t const __str[] = (str);\
	(void)BTSQLiteErr(sqlite3_prepare_v2((db), __str, sizeof(__str), &__stmt, NULL)); \
	__stmt; \
})
#define EXEC(stmt) ({ \
	sqlite3_stmt *const __stmt = (stmt); \
	(void)BTSQLiteErr(sqlite3_step(__stmt)); \
	__stmt; \
})

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
		(void)BTErrno(write(tmp, buf, rlen)); // TODO: libuv
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

	// TODO: Check session mode
	// TODO: Make sure session repo and submission repo match
	EFSRepoRef const repo = submission->repo;

	str_t *internalPath = NULL;
	strarg_t const internalHash = EFSURIListGetInternalHash(submission->URIs);
	(void)BTErrno(asprintf(&internalPath, "%s/%2s/%s", EFSRepoGetDataPath(repo), internalHash, internalHash));
	uv_fs_t req = { .data = co_active() };
	uv_fs_link(loop, &req, submission->path, internalPath, async_fs_cb);
	co_switch(yield);
	if(req.result < 0) {
		fprintf(stderr, "Couldn't move %s to %s\n", submission->path, internalPath);
		uv_fs_req_cleanup(&req);
		FREE(&internalPath);
		return -1;
	}
	uv_fs_req_cleanup(&req);
	FREE(&internalPath);

	uint64_t const userID = EFSSessionGetUserID(session);
	sqlite3 *const db = EFSRepoDBConnect(repo);

	sqlite3_finalize(EXEC(QUERY(db, "BEGIN TRANSACTION")));

	sqlite3_stmt *const insertFile = QUERY(db,
		"INSERT INTO \"files\" (\"internalHash\", \"type\", \"size\")\n"
		" VALUES (?, ?, ?)");
	sqlite3_bind_text(insertFile, 1, internalHash, -1, SQLITE_STATIC);
	sqlite3_bind_text(insertFile, 2, submission->type, -1, SQLITE_STATIC);
	sqlite3_bind_int64(insertFile, 3, submission->size);
	(void)EXEC(insertFile);
	int64_t const fileID = sqlite3_last_insert_rowid(db);
	sqlite3_finalize(insertFile);


	sqlite3_stmt *const insertURI = QUERY(db,
		"INSERT INTO \"URIs\" (\"URI\") VALUES (?)");
	sqlite3_stmt *const insertFileURI = QUERY(db,
		"INSERT INTO \"fileURIs\" (\"fileID\", \"URIID\") VALUES (?, ?)");
	for(index_t i = 0; i < EFSURIListGetCount(submission->URIs); ++i) {
		sqlite3_bind_text(insertURI, 1, EFSURIListGetURI(submission->URIs, i), -1, SQLITE_STATIC);
		(void)EXEC(insertURI);
		int64_t const URIID = sqlite3_last_insert_rowid(db);
		sqlite3_bind_int64(insertFileURI, 1, fileID);
		sqlite3_bind_int64(insertFileURI, 2, URIID);
		(void)EXEC(insertFileURI);
	}
	sqlite3_finalize(insertURI);
	sqlite3_finalize(insertFileURI);


	// TODO: Add permissions for other specified users too.
	sqlite3_stmt *const insertFilePermissions = QUERY(db,
		"INSERT INTO \"filePermissions\"\n"
		"\t" " (\"fileID\", \"userID\", \"grantorID\")\n"
		" VALUES (?, ?, ?)");
	sqlite3_bind_int64(insertFilePermissions, 1, fileID);
	sqlite3_bind_int64(insertFilePermissions, 2, userID);
	sqlite3_bind_int64(insertFilePermissions, 3, userID);
	sqlite3_finalize(EXEC(insertFilePermissions));


// TODO: indexing...
//"INSERT INTO \"links\" (\"sourceURIID\", \"targetURIID\", \"fileID\") VALUES (?, ?, ?)"
//"INSERT INTO \"fulltext\" (\"text\") VALUES (?)"
//"INSERT INTO \"fileContent\" (\"ftID\", \"fileID\") VALUES (?, ?)"


	sqlite3_finalize(EXEC(QUERY(db, "COMMIT")));
	EFSRepoDBClose(repo, db);

	return 0;
}


static err_t mkdirp(str_t *const path, size_t const len, int const mode) {
	if(0 == len) return 0;
	if(1 == len) {
		if('/' == path[0]) return 0;
		if('.' == path[0]) return 0;
	}
	uv_fs_t req = { .data = co_active() };
	uv_fs_mkdir(loop, &req, path, mode, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	if(req.result >= 0) return 0;
	if(ENOENT != req.result) return -1;
	index_t i = len;
	for(; i > 0 && '/' != path[i]; --i);
	if(0 == len || len == i) return -1;
	path[i] = '\0';
	if(mkdirp(path, i, mode) < 0) return -1;
	path[i] = '/';
	uv_fs_mkdir(loop, &req, path, mode, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	if(req.result < 0) return -1;
	return 0;
}

