#define _GNU_SOURCE
#include <fcntl.h>
#include "async.h"
#include "fs.h"
#include "EarthFS.h"

#define INDEX_MAX (1024 * 100)

struct EFSSubmission {
	EFSRepoRef repo;
	str_t *path;
	str_t *type;
	int64_t size;
	URIListRef URIs;
	str_t *internalHash;
	URIListRef metaURIs; // 0 is source, rest are targets.
};

EFSSubmissionRef EFSRepoCreateSubmission(EFSRepoRef const repo, strarg_t const type, ssize_t (*read)(void *, byte_t const **), void *const context) {
	if(!repo) return NULL;

	EFSSubmissionRef sub = calloc(1, sizeof(struct EFSSubmission));
	sub->repo = repo;
	sub->type = strdup(type);

	sub->path = EFSRepoCopyTempPath(repo);
	ssize_t const dirlen = dirname(sub->path, strlen(sub->path));
	if(dirlen < 0) {
		EFSSubmissionFree(sub);
		return NULL;
	}
	if(mkdirp(sub->path, dirlen, 0700) < 0) {
		fprintf(stderr, "Error: couldn't create temp dir %s\n", sub->path);
		EFSSubmissionFree(sub);
		return NULL;
	}

	uv_fs_t req = { .data = co_active() };
	uv_fs_open(loop, &req, sub->path, O_CREAT | O_EXCL | O_TRUNC | O_WRONLY, 0400, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	if(req.result < 0) {
		fprintf(stderr, "Error: couldn't create temp file %s\n", sub->path);
		EFSSubmissionFree(sub);
		return NULL;
	}
	uv_file const tmp = req.result;

	EFSHasherRef const hasher = EFSHasherCreate(sub->type);
	URIListParserRef const metaURIsParser = URIListParserCreate(sub->type);

	for(;;) {
		byte_t const *buf = NULL;
		ssize_t const rlen = read(context, &buf);
		if(0 == rlen) break;
		if(rlen < 0) {
			fprintf(stderr, "EFSSubmission read error %d\n", rlen);
			EFSSubmissionFree(sub); sub = NULL;
			goto bail;
		}

		uv_buf_t info = uv_buf_init((char *)buf, rlen);
		uv_fs_write(loop, &req, tmp, &info, 1, sub->size, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);
		if(req.result < 0) {
			fprintf(stderr, "EFSSubmission write error %d\n", req.result);
			EFSSubmissionFree(sub); sub = NULL;
			goto bail;
		}

		size_t const indexable = MIN(rlen, SUB_ZERO(INDEX_MAX, sub->size));
		EFSHasherWrite(hasher, buf, rlen);
		URIListParserWrite(metaURIsParser, buf, indexable);
		// TODO: Full-text indexing.

		sub->size += rlen;
	}

	if(!sub->size) {
		EFSSubmissionFree(sub); sub = NULL;
		goto bail;
	}

	sub->URIs = EFSHasherEnd(hasher);
	sub->internalHash = strdup(EFSHasherGetInternalHash(hasher));
	sub->metaURIs = URIListParserEnd(metaURIsParser, sub->size > INDEX_MAX);

bail:
	EFSHasherFree(hasher);
	URIListParserFree(metaURIsParser);

	uv_fs_close(loop, &req, tmp, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);

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
strarg_t EFSSubmissionGetPrimaryURI(EFSSubmissionRef const sub) {
	if(!sub) return NULL;
	return URIListGetURI(sub->URIs, 0);
}

err_t EFSSessionAddSubmission(EFSSessionRef const session, EFSSubmissionRef const sub) {
	if(!session) return 0;
	if(!sub) return -1;

	// TODO: Check session mode
	// TODO: Make sure session repo and submission repo match
	EFSRepoRef const repo = sub->repo;

	str_t *internalPath = EFSRepoCopyInternalPath(repo, sub->internalHash);
	ssize_t const dlen = dirname(internalPath, strlen(internalPath));
	internalPath[dlen] = '\0';
	if(mkdirp(internalPath, dlen, 0700) < 0) {
		fprintf(stderr, "Couldn't mkdir -p %s\n", internalPath);
		FREE(&internalPath);
		return -1;
	}
	internalPath[dlen] = '/';

	uv_fs_t req = { .data = co_active() };
	uv_fs_link(loop, &req, sub->path, internalPath, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	if(req.result < 0 && -EEXIST != req.result) {
		fprintf(stderr, "Couldn't move %s to %s\n", sub->path, internalPath);
		FREE(&internalPath);
		return -1;
	}
	FREE(&internalPath);

	uv_fs_unlink(loop, &req, sub->path, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	FREE(&sub->path);

	uint64_t const userID = EFSSessionGetUserID(session);
	sqlite3 *const db = EFSRepoDBConnect(repo);

	EXEC(QUERY(db, "BEGIN TRANSACTION"));

	sqlite3_stmt *const insertFile = QUERY(db,
		"INSERT OR IGNORE INTO \"files\" (\"internalHash\", \"type\", \"size\")\n"
		" VALUES (?, ?, ?)");
	sqlite3_bind_text(insertFile, 1, sub->internalHash, -1, SQLITE_STATIC);
	sqlite3_bind_text(insertFile, 2, sub->type, -1, SQLITE_STATIC);
	sqlite3_bind_int64(insertFile, 3, sub->size);
	EXEC(insertFile);
	int64_t const fileID = sqlite3_last_insert_rowid(db);


	sqlite3_stmt *const insertURI = QUERY(db,
		"INSERT OR IGNORE INTO \"URIs\" (\"URI\") VALUES (?)");
	sqlite3_stmt *const insertFileURI = QUERY(db,
		"INSERT OR IGNORE INTO \"fileURIs\" (\"fileID\", \"URIID\")\n"
		" SELECT ?, \"URIID\" FROM \"URIs\" WHERE \"URI\" = ? LIMIT 1");
	for(index_t i = 0; i < URIListGetCount(sub->URIs); ++i) {
		strarg_t const URI = URIListGetURI(sub->URIs, i);
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
	sqlite3_stmt *const insertFilePermission = QUERY(db,
		"INSERT OR IGNORE INTO \"filePermissions\"\n"
		"\t" " (\"fileID\", \"userID\", \"grantorID\")\n"
		" VALUES (?, ?, ?)");
	sqlite3_bind_int64(insertFilePermission, 1, fileID);
	sqlite3_bind_int64(insertFilePermission, 2, userID);
	sqlite3_bind_int64(insertFilePermission, 3, userID);
	EXEC(insertFilePermission);


	// TODO: Handle `text/uri-list` versus `text/efs-meta+uri-list`.
	// A plain URI list is like a directory, it isn't meta at all.
	// TODO: Normalize URIs before adding (at least lowercase scheme and authority).
	count_t const metaURICount = URIListGetCount(sub->metaURIs);
	sqlite3_stmt *const insertLink = QUERY(db,
		"INSERT OR IGNORE INTO \"links\"\n"
		"\t" "(\"sourceURIID\", \"targetURIID\", \"metaFileID\")\n"
		"SELECT s.\"URIID\", t.\"URIID\", ?\n"
		"FROM \"URIs\" AS s\n"
		"INNER JOIN \"URIs\" AS t\n"
		"WHERE s.\"URI\" = ? AND t.\"URI\" = ?");
	sqlite3_bind_int64(insertLink, 1, fileID);
	if(metaURICount >= 1) {
		sqlite3_bind_text(insertLink, 2, URIListGetURI(sub->URIs, 0), -1, SQLITE_STATIC);
		sqlite3_bind_text(insertLink, 3, URIListGetURI(sub->metaURIs, 0), -1, SQLITE_STATIC);
		sqlite3_step(insertLink);
		sqlite3_reset(insertLink);
	}
	if(metaURICount >= 2) {
		sqlite3_bind_text(insertLink, 2, URIListGetURI(sub->metaURIs, 0), -1, SQLITE_STATIC);
		for(index_t i = 1; i < metaURICount; ++i) {
			sqlite3_bind_text(insertLink, 3, URIListGetURI(sub->metaURIs, i), -1, SQLITE_STATIC);
			sqlite3_step(insertLink);
			sqlite3_reset(insertLink);
		}
	}
	sqlite3_finalize(insertLink);


// TODO: Full-text indexing...
// Type: `text/efs-meta+plain` ?
//
//"INSERT INTO \"fulltext\" (\"text\") VALUES (?)"
//"INSERT INTO \"fileContent\" (\"ftID\", \"fileID\") VALUES (?, ?)"


	EXEC(QUERY(db, "COMMIT"));
	EFSRepoDBClose(repo, db);

	return 0;
}

