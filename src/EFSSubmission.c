#define _GNU_SOURCE
#include <fcntl.h>
#include "async.h"
#include "EarthFS.h"
#include "HTTPServer.h"

// TODO: Find a home for these.
static err_t mkdirp(str_t *const path, ssize_t len, int const mode);

struct EFSSubmission {
	EFSRepoRef repo;
	str_t *path;
	str_t *type;
	ssize_t size; // TODO: Appropriate type? 64-bit unsigned.
	URIListRef URIs;
	str_t *internalHash;
};

EFSSubmissionRef EFSRepoCreateSubmission(EFSRepoRef const repo, HTTPConnectionRef const conn) {
	if(!repo) return NULL;
	BTAssert(conn, "EFSSubmission connection required");

	EFSSubmissionRef const sub = calloc(1, sizeof(struct EFSSubmission));
	sub->repo = repo;

	str_t const x[] = "efs-tmp"; // TODO: Generate random filename.
	(void)BTErrno(asprintf(&sub->path, "/tmp/%s", x)); // TODO: Use temp dir from repo.
	fd_t const tmp = BTErrno(creat(sub->path, 0400));
	if(tmp < 0) {
		EFSSubmissionFree(sub);
		return NULL;
	}

/*	HTTPHeaderList const *const headers = HTTPConnectionGetHeaders(conn);
	for(index_t i = 0; i < headers->count; ++i) {
		if(0 == strcasecmp("content-type", headers->items[i].field)) {
			sub->type = strdup(headers->items[i].value);
		}
	}*/

	// TODO: Reading the payload could never be this easy, we need to parse the multipart encoding.
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
	sub->URIs = EFSHasherEnd(hasher);
	sub->internalHash = strdup(EFSHasherGetInternalHash(hasher));
	EFSHasherFree(hasher);

	return sub;
}
void EFSSubmissionFree(EFSSubmissionRef const sub) {
	if(!sub) return;
	if(sub->path) {
		uv_fs_t req = { .data = co_active() };
		uv_fs_unlink(loop, &req, sub->path, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);
	}
	FREE(&sub->path);
	FREE(&sub->type);
	URIListFree(sub->URIs); sub->URIs = NULL;
	FREE(&sub->internalHash);
	free(sub);
}

err_t EFSSessionAddSubmission(EFSSessionRef const session, EFSSubmissionRef const submission) {
	if(!session) return 0;
	if(!submission) return -1;

	// TODO: Check session mode
	// TODO: Make sure session repo and submission repo match
	EFSRepoRef const repo = submission->repo;

	str_t *dir = NULL;
	(void)BTErrno(asprintf(&dir, "%s/%.2s", EFSRepoGetDataPath(repo), submission->internalHash));
	if(mkdirp(dir, -1, 0700) < 0) {
		fprintf(stderr, "Couldn't mkdir -p %s\n", dir);
		FREE(&dir);
		return -1;
	}

	str_t *internalPath = NULL;
	(void)BTErrno(asprintf(&internalPath, "%s/%s", dir, submission->internalHash));
	uv_fs_t req = { .data = co_active() };
	uv_fs_link(loop, &req, submission->path, internalPath, async_fs_cb);
	co_switch(yield);
	if(req.result < 0) {
		fprintf(stderr, "Couldn't move %s to %s\n", submission->path, internalPath);
		uv_fs_req_cleanup(&req);
		FREE(&internalPath);
		FREE(&dir);
		return -1;
	}
	uv_fs_req_cleanup(&req);
	FREE(&internalPath);
	FREE(&dir);

	uv_fs_unlink(loop, &req, submission->path, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	FREE(&submission->path);

	uint64_t const userID = EFSSessionGetUserID(session);
	sqlite3 *const db = EFSRepoDBConnect(repo);

	EXEC(QUERY(db, "BEGIN TRANSACTION"));

	sqlite3_stmt *const insertFile = QUERY(db,
		"INSERT OR IGNORE INTO \"files\" (\"internalHash\", \"type\", \"size\")\n"
		" VALUES (?, ?, ?)");
	sqlite3_bind_text(insertFile, 1, submission->internalHash, -1, SQLITE_STATIC);
	sqlite3_bind_text(insertFile, 2, submission->type, -1, SQLITE_STATIC);
	sqlite3_bind_int64(insertFile, 3, submission->size);
	EXEC(insertFile);
	int64_t const fileID = sqlite3_last_insert_rowid(db);


	sqlite3_stmt *const insertURI = QUERY(db,
		"INSERT OR IGNORE INTO \"URIs\" (\"URI\") VALUES (?)");
	sqlite3_stmt *const insertFileURI = QUERY(db,
		"INSERT OR IGNORE INTO \"fileURIs\" (\"fileID\", \"URIID\")\n"
		" SELECT ?, \"URIID\" FROM \"URIs\" WHERE \"URI\" = ? LIMIT 1");
	for(index_t i = 0; i < URIListGetCount(submission->URIs); ++i) {
		strarg_t const URI = URIListGetURI(submission->URIs, i);
		sqlite3_bind_text(insertURI, 1, URI, -1, SQLITE_STATIC);
		sqlite3_step(insertURI);
		sqlite3_reset(insertURI);

		sqlite3_bind_int64(insertFileURI, 1, fileID);
		sqlite3_bind_text(insertFileURI, 2, URI, -1, SQLITE_STATIC);
		sqlite3_step(insertFileURI);
		sqlite3_reset(insertFileURI);
	}
	sqlite3_finalize(insertURI);
	sqlite3_finalize(insertFileURI);


	// TODO: Add permissions for other specified users too.
	sqlite3_stmt *const insertFilePermissions = QUERY(db,
		"INSERT OR IGNORE INTO \"filePermissions\"\n"
		"\t" " (\"fileID\", \"userID\", \"grantorID\")\n"
		" VALUES (?, ?, ?)");
	sqlite3_bind_int64(insertFilePermissions, 1, fileID);
	sqlite3_bind_int64(insertFilePermissions, 2, userID);
	sqlite3_bind_int64(insertFilePermissions, 3, userID);
	EXEC(insertFilePermissions);


// TODO: indexing...
//"INSERT OR IGNORE INTO \"links\" (\"sourceURIID\", \"targetURIID\", \"fileID\") VALUES (?, ?, ?)"
//"INSERT INTO \"fulltext\" (\"text\") VALUES (?)"
//"INSERT INTO \"fileContent\" (\"ftID\", \"fileID\") VALUES (?, ?)"


	EXEC(QUERY(db, "COMMIT"));
	EFSRepoDBClose(repo, db);

	return 0;
}


static err_t mkdirp(str_t *const path, ssize_t len, int const mode) {
	if(len < 0) len = strlen(path);
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
	if(-ENOENT != req.result) return -1;
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

