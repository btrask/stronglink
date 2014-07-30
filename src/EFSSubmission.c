#define _GNU_SOURCE
#include <fcntl.h>
#include <yajl/yajl_gen.h>
#include "async.h"
#include "EarthFS.h"

struct EFSSubmission {
	EFSSessionRef session;
	str_t *type;

	str_t *tmppath;
	uv_file tmpfile;
	int64_t size;
	EFSHasherRef hasher;
	EFSMetaFileRef meta;

	URIListRef URIs;
	str_t *internalHash;
};

EFSSubmissionRef EFSSubmissionCreate(EFSSessionRef const session, strarg_t const type) {
	if(!session) return NULL;
	assertf(type, "Submission requires type");
	// TODO: Check session permissions?

	EFSSubmissionRef sub = calloc(1, sizeof(struct EFSSubmission));
	if(!sub) return NULL;
	sub->session = session;
	sub->type = strdup(type);

	sub->tmppath = EFSRepoCopyTempPath(EFSSessionGetRepo(session));
	sub->tmpfile = async_fs_open(sub->tmppath, O_CREAT | O_EXCL | O_TRUNC | O_WRONLY, 0400);
	if(sub->tmpfile < 0) {
		if(-UV_ENOENT == sub->tmpfile) {
			async_fs_mkdirp_dirname(sub->tmppath, 0700);
			sub->tmpfile = async_fs_open(sub->tmppath, O_CREAT | O_EXCL | O_TRUNC | O_WRONLY, 0400);
		}
		if(sub->tmpfile < 0) {
			fprintf(stderr, "Error: couldn't create temp file %s\n", sub->tmppath);
			EFSSubmissionFree(&sub);
			return NULL;
		}
	}

	sub->hasher = EFSHasherCreate(sub->type);
	sub->meta = EFSMetaFileCreate(sub->type);

	return sub;
}
void EFSSubmissionFree(EFSSubmissionRef *const subptr) {
	EFSSubmissionRef sub = *subptr;
	if(!sub) return;
	sub->session = NULL;
	FREE(&sub->type);
	if(sub->tmppath) async_fs_unlink(sub->tmppath);
	FREE(&sub->tmppath);
	EFSHasherFree(&sub->hasher);
	EFSMetaFileFree(&sub->meta);
	URIListFree(&sub->URIs);
	FREE(&sub->internalHash);
	FREE(subptr); sub = NULL;
}

EFSRepoRef EFSSubmissionGetRepo(EFSSubmissionRef const sub) {
	if(!sub) return NULL;
	return EFSSessionGetRepo(sub->session);
}

err_t EFSSubmissionWrite(EFSSubmissionRef const sub, byte_t const *const buf, size_t const len) {
	if(!sub) return 0;
	if(sub->tmpfile < 0) return -1;

	uv_buf_t info = uv_buf_init((char *)buf, len);
	ssize_t const result = async_fs_write(sub->tmpfile, &info, 1, sub->size);
	if(result < 0) {
		fprintf(stderr, "EFSSubmission write error %ld\n", (long)result);
		return -1;
	}

	sub->size += len;
	EFSHasherWrite(sub->hasher, buf, len);
	EFSMetaFileWrite(sub->meta, buf, len);
	return 0;
}
err_t EFSSubmissionEnd(EFSSubmissionRef const sub) {
	if(!sub) return 0;
	if(sub->tmpfile < 0) return -1;
	sub->URIs = EFSHasherEnd(sub->hasher);
	sub->internalHash = strdup(EFSHasherGetInternalHash(sub->hasher));
	EFSHasherFree(&sub->hasher);

	EFSMetaFileEnd(sub->meta);

	async_fs_close(sub->tmpfile);
	sub->tmpfile = -1;
	return 0;
}
err_t EFSSubmissionWriteFrom(EFSSubmissionRef const sub, ssize_t (*read)(void *, byte_t const **), void *const context) {
	if(!sub) return 0;
	assertf(read, "Read function required");
	for(;;) {
		byte_t const *buf = NULL;
		ssize_t const len = read(context, &buf);
		if(0 == len) break;
		if(len < 0) return -1;
		if(EFSSubmissionWrite(sub, buf, len) < 0) return -1;
	}
	if(EFSSubmissionEnd(sub) < 0) return -1;
	return 0;
}

strarg_t EFSSubmissionGetPrimaryURI(EFSSubmissionRef const sub) {
	if(!sub) return NULL;
	return URIListGetURI(sub->URIs, 0);
}

err_t EFSSubmissionAddFile(EFSSubmissionRef const sub) {
	if(!sub) return -1;
	if(!sub->tmppath) return -1;
	if(!sub->size) return -1;
	EFSRepoRef const repo = EFSSubmissionGetRepo(sub);
	str_t *internalPath = EFSRepoCopyInternalPath(repo, sub->internalHash);
	err_t result = 0;
	result = async_fs_link(sub->tmppath, internalPath);
	if(result < 0 && -UV_EEXIST != result) {
		if(-UV_ENOENT == result) {
			async_fs_mkdirp_dirname(internalPath, 0700);
			result = async_fs_link(sub->tmppath, internalPath);
		}
		if(result < 0 && -UV_EEXIST != result) {
			fprintf(stderr, "Couldn't move %s to %s\n", sub->tmppath, internalPath);
			FREE(&internalPath);
			return -1;
		}
	}
	FREE(&internalPath);
	async_fs_unlink(sub->tmppath);
	FREE(&sub->tmppath);
	return 0;
}
err_t EFSSubmissionStore(EFSSubmissionRef const sub, sqlite3f *const db) {
	if(!sub) return -1;
	assertf(db, "EFSSubmissionStore DB required");
	if(sub->tmppath) return -1;
	EFSSessionRef const session = sub->session;
	EFSRepoRef const repo = EFSSubmissionGetRepo(sub);
	uint64_t const userID = EFSSessionGetUserID(session);

	EXEC(QUERY(db, "SAVEPOINT submission"));

	sqlite3_stmt *insertFile = QUERY(db,
		"INSERT OR IGNORE INTO files (internal_hash, file_type, file_size)\n"
		"VALUES (?, ?, ?)");
	sqlite3_bind_text(insertFile, 1, sub->internalHash, -1, SQLITE_STATIC);
	sqlite3_bind_text(insertFile, 2, sub->type, -1, SQLITE_STATIC);
	sqlite3_bind_int64(insertFile, 3, sub->size);
	EXEC(insertFile); insertFile = NULL;

	// We can't use last_insert_rowid() if the file already existed.
	sqlite3_stmt *selectFile = QUERY(db,
		"SELECT file_id FROM files\n"
		"WHERE internal_hash = ? AND file_type = ?");
	sqlite3_bind_text(selectFile, 1, sub->internalHash, -1, SQLITE_STATIC);
	sqlite3_bind_text(selectFile, 2, sub->type, -1, SQLITE_STATIC);
	STEP(selectFile);
	int64_t const fileID = sqlite3_column_int64(selectFile, 0);
	sqlite3f_finalize(selectFile); selectFile = NULL;

	sqlite3_stmt *insertFileURI = QUERY(db,
		"INSERT OR IGNORE INTO file_uris (file_id, uri)\n"
		"VALUES (?, ?)");
	for(index_t i = 0; i < URIListGetCount(sub->URIs); ++i) {
		strarg_t const URI = URIListGetURI(sub->URIs, i);
		sqlite3_bind_int64(insertFileURI, 1, fileID);
		sqlite3_bind_text(insertFileURI, 2, URI, -1, SQLITE_STATIC);
		STEP(insertFileURI); sqlite3_reset(insertFileURI);
	}
	sqlite3f_finalize(insertFileURI); insertFileURI = NULL;


	// TODO: Add permissions for other specified users too.
/*	sqlite3_stmt *insertFilePermission = QUERY(db,
		"INSERT OR IGNORE INTO file_permissions\n"
		"	(file_id, user_id, meta_file_id)\n"
		"VALUES (?, ?, ?)");
	sqlite3_bind_int64(insertFilePermission, 1, fileID);
	sqlite3_bind_int64(insertFilePermission, 2, userID);
	sqlite3_bind_int64(insertFilePermission, 3, fileID);
	EXEC(insertFilePermission); insertFilePermission = NULL;*/


	strarg_t const primaryURI = EFSSubmissionGetPrimaryURI(sub);
	if(EFSMetaFileStore(sub->meta, fileID, primaryURI, db) < 0) {
		fprintf(stderr, "EFSMetaFileStore error\n");
		EXEC(QUERY(db, "ROLLBACK TO submission"));
		EXEC(QUERY(db, "RELEASE submission"));
		return -1;
	}

	EXEC(QUERY(db, "RELEASE submission"));

	return 0;
}

EFSSubmissionRef EFSSubmissionCreateAndAdd(EFSSessionRef const session, strarg_t const type, ssize_t (*read)(void *, byte_t const **), void *const context) {
	EFSSubmissionRef sub = EFSSubmissionCreate(session, type);
	if(!sub) return NULL;
	err_t err = 0;
	err = err < 0 ? err : EFSSubmissionWriteFrom(sub, read, context);
	err = err < 0 ? err : EFSSubmissionAddFile(sub);
	if(err >= 0) {
		EFSRepoRef const repo = EFSSubmissionGetRepo(sub);
		sqlite3f *db = EFSRepoDBConnect(repo);
		err = err < 0 ? err : EFSSubmissionStore(sub, db);
		EFSRepoDBClose(repo, &db);
	}
	if(err < 0) EFSSubmissionFree(&sub);
	return sub;
}
err_t EFSSubmissionCreatePair(EFSSessionRef const session, strarg_t const type, ssize_t (*read)(void *, byte_t const **), void *const context, strarg_t const title, EFSSubmissionRef *const outSub, EFSSubmissionRef *const outMeta) {
	EFSSubmissionRef sub = EFSSubmissionCreate(session, type);
	EFSSubmissionRef meta = EFSSubmissionCreate(session, "text/efs-meta+json; charset=utf-8");
	if(
		!sub || !meta ||
		EFSSubmissionWriteFrom(sub, read, context) < 0 ||
		EFSSubmissionAddFile(sub) < 0
	) {
		EFSSubmissionFree(&sub);
		EFSSubmissionFree(&meta);
		return -1;
	}

	yajl_gen const json = yajl_gen_alloc(NULL);
	yajl_gen_config(json, yajl_gen_print_callback, (void (*)())EFSSubmissionWrite, meta);
	yajl_gen_config(json, yajl_gen_beautify, (int)true);

	yajl_gen_map_open(json);

	strarg_t const metaURI = EFSSubmissionGetPrimaryURI(sub);
	yajl_gen_string(json, (byte_t const *)"metaURI", strlen("metaURI"));
	yajl_gen_string(json, (byte_t const *)metaURI, strlen(metaURI));

	if(title) {
		yajl_gen_string(json, (byte_t const *)"title", strlen("title"));
//		yajl_gen_array_open(json);
		yajl_gen_string(json, (byte_t const *)title, strlen(title));
//		yajl_gen_array_close(json);
	}
	// TODO: We should also try to extract a title from the content. Write them together in an array. All of our fields should support multiple values.

	yajl_gen_string(json, (byte_t const *)"link", strlen("link"));
	yajl_gen_array_open(json);
	yajl_gen_string(json, (byte_t const *)"efs://user", strlen("efs://user"));
	yajl_gen_array_close(json);
	// TODO: Full text indexing, determine links, etc.

	yajl_gen_map_close(json);

	if(
		EFSSubmissionEnd(meta) < 0 ||
		EFSSubmissionAddFile(meta) < 0
	) {
		EFSSubmissionFree(&sub);
		EFSSubmissionFree(&meta);
		return -1;
	}

	*outSub = sub;
	*outMeta = meta;
	return 0;
}

