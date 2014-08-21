#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <yajl/yajl_gen.h>
#include "async.h"
#include "EarthFS.h"

#define FTS_MAX (1024 * 50)

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
		if(UV_ENOENT == sub->tmpfile) {
			async_fs_mkdirp_dirname(sub->tmppath, 0700);
			sub->tmpfile = async_fs_open(sub->tmppath, O_CREAT | O_EXCL | O_TRUNC | O_WRONLY, 0400);
		}
		if(sub->tmpfile < 0) {
			fprintf(stderr, "Error: couldn't create temp file %s (%s)\n", sub->tmppath, uv_err_name(sub->tmpfile));
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
	if(result < 0 && UV_EEXIST != result) {
		if(UV_ENOENT == result) {
			async_fs_mkdirp_dirname(internalPath, 0700);
			result = async_fs_link(sub->tmppath, internalPath);
		}
		if(result < 0 && UV_EEXIST != result) {
			fprintf(stderr, "Couldn't move %s to %s (%s)\n", sub->tmppath, internalPath, uv_err_name(result));
			FREE(&internalPath);
			return -1;
		}
	}
	FREE(&internalPath);
	async_fs_unlink(sub->tmppath);
	FREE(&sub->tmppath);
	return 0;
}
err_t EFSSubmissionStore(EFSSubmissionRef const sub, EFSConnection const *const conn, MDB_txn *const txn) {
	if(!sub) return -1;
	assertf(conn && txn, "EFSSubmissionStore DB connection required");
	if(sub->tmppath) return -1;
	EFSSessionRef const session = sub->session;
	EFSRepoRef const repo = EFSSubmissionGetRepo(sub);
	int64_t const userID = EFSSessionGetUserID(session);

	int64_t fileID = db_last_id(txn, conn->fileByID)+1;
	int rc;

	DB_VAL(dupFileID_val, 1);
	db_bind(dupFileID_val, 0, fileID);

	uint64_t const internalHash_id = db_string_id(txn, conn->schema, sub->internalHash);
	uint64_t const type_id = db_string_id(txn, conn->schema, sub->type);
	DB_VAL(fileInfo_val, 2);
	db_bind(fileInfo_val, 0, internalHash_id);
	db_bind(fileInfo_val, 1, type_id);
	rc = mdb_put(txn, conn->fileIDByInfo, fileInfo_val, dupFileID_val, MDB_NOOVERWRITE);
	if(MDB_KEYEXIST == rc) fileID = db_column(dupFileID_val, 0);
	else if(MDB_SUCCESS != rc) return -1;

	DB_VAL(fileID_val, 1);
	db_bind(fileID_val, 0, fileID);

	DB_VAL(file_val, 3);
	db_bind(file_val, 0, internalHash_id);
	db_bind(file_val, 1, type_id);
	db_bind(file_val, 2, sub->size);
	rc = mdb_put(txn, conn->fileByID, fileID_val, file_val, MDB_NOOVERWRITE);
	if(MDB_SUCCESS != rc && MDB_KEYEXIST != rc) return -1;

	for(index_t i = 0; i < URIListGetCount(sub->URIs); ++i) {
		strarg_t const URI = URIListGetURI(sub->URIs, i);
		uint64_t const URI_id = db_string_id(txn, conn->schema, URI);
		DB_VAL(URI_val, 1);
		db_bind(URI_val, 0, URI_id);

		rc = mdb_put(txn, conn->URIByFileID, fileID_val, URI_val, MDB_NODUPDATA);
		assert(MDB_SUCCESS == rc || MDB_KEYEXIST == rc);

		rc = mdb_put(txn, conn->fileIDByURI, URI_val, fileID_val, MDB_NODUPDATA);
		assert(MDB_SUCCESS == rc || MDB_KEYEXIST == rc);
	}

	// TODO: Store fileIDByType


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
	if(EFSMetaFileStore(sub->meta, fileID, primaryURI, conn, txn) < 0) {
		fprintf(stderr, "EFSMetaFileStore error\n");
		return -1;
	}

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
		EFSConnection const *conn = EFSRepoDBOpen(repo);
		int rc;
		MDB_txn *txn = NULL;
		rc = mdb_txn_begin(conn->env, NULL, MDB_RDWR, &txn);
		if(MDB_SUCCESS != rc) err = -1;
		err = err < 0 ? err : EFSSubmissionStore(sub, conn, txn);
		if(err < 0) {
			mdb_txn_abort(txn); txn = NULL;
		} else {
			rc = mdb_txn_commit(txn); txn = NULL;
		}
		EFSRepoDBClose(repo, &conn);
	}
	if(err < 0) EFSSubmissionFree(&sub);
	return sub;
}
err_t EFSSubmissionCreatePair(EFSSessionRef const session, strarg_t const type, ssize_t (*read)(void *, byte_t const **), void *const context, strarg_t const title, EFSSubmissionRef *const outSub, EFSSubmissionRef *const outMeta) {
	EFSSubmissionRef sub = EFSSubmissionCreate(session, type);
	EFSSubmissionRef meta = EFSSubmissionCreate(session, "text/efs-meta+json; charset=utf-8");
	if(!sub || !meta) {
		EFSSubmissionFree(&sub);
		EFSSubmissionFree(&meta);
		return -1;
	}


	str_t *fulltext = NULL;
	size_t fulltextlen = 0;
	if(
		0 == strcasecmp(type, "text/markdown; charset=utf-8") ||
		0 == strcasecmp(type, "text/plain; charset=utf-8")
	) {
		fulltext = malloc(FTS_MAX + 1);
		// TODO
	}
	for(;;) {
		byte_t const *buf = NULL;
		ssize_t const len = read(context, &buf);
		if(0 == len) break;
		if(len < 0 || EFSSubmissionWrite(sub, buf, len) < 0) {
			FREE(&fulltext);
			EFSSubmissionFree(&sub);
			EFSSubmissionFree(&meta);
			return -1;
		}
		if(fulltext) {
			size_t const use = MIN(FTS_MAX-fulltextlen, len);
			memcpy(fulltext+fulltextlen, buf, use);
			fulltextlen += use;
		}
	}
	if(fulltext) {
		fulltext[fulltextlen] = '\0';
	}


	if(EFSSubmissionEnd(sub) < 0 || EFSSubmissionAddFile(sub) < 0) {
		FREE(&fulltext);
		EFSSubmissionFree(&sub);
		EFSSubmissionFree(&meta);
		return -1;
	}

	strarg_t const targetURI = EFSSubmissionGetPrimaryURI(sub);
	EFSSubmissionWrite(meta, (byte_t const *)targetURI, strlen(targetURI));
	EFSSubmissionWrite(meta, (byte_t const *)"\r\n\r\n", 4);

	yajl_gen json = yajl_gen_alloc(NULL);
	yajl_gen_config(json, yajl_gen_print_callback, (void (*)())EFSSubmissionWrite, meta);
	yajl_gen_config(json, yajl_gen_beautify, (int)true);

	yajl_gen_map_open(json);

	if(title) {
		yajl_gen_string(json, (byte_t const *)"title", strlen("title"));
		yajl_gen_array_open(json);
		yajl_gen_string(json, (byte_t const *)title, strlen(title));
		if(fulltextlen) {
			// TODO: Try to determine title from content
		}
		yajl_gen_array_close(json);
	}

	if(fulltextlen) {
		yajl_gen_string(json, (byte_t const *)"fulltext", strlen("fulltext"));
		yajl_gen_string(json, (byte_t const *)fulltext, fulltextlen);
	}

	yajl_gen_string(json, (byte_t const *)"link", strlen("link"));
	yajl_gen_array_open(json);
	yajl_gen_string(json, (byte_t const *)"efs://user", strlen("efs://user"));
	yajl_gen_array_close(json);
	// TODO: Parse fulltext for links

	yajl_gen_map_close(json);
	yajl_gen_free(json); json = NULL;
	FREE(&fulltext);

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
err_t EFSSubmissionBatchStore(EFSSubmissionRef const *const list, count_t const count) {
	if(!count) return 0;
	EFSRepoRef const repo = EFSSessionGetRepo(list[0]->session);
	EFSConnection const *conn = EFSRepoDBOpen(repo);
	int rc;
	MDB_txn *txn = NULL;
	rc = mdb_txn_begin(conn->env, NULL, MDB_RDWR, &txn);
	if(MDB_SUCCESS != rc) {
		EFSRepoDBClose(repo, &conn);
		return -1;
	}
	err_t err = 0;
	for(index_t i = 0; i < count; ++i) {
		assert(list[i]);
		assert(repo == EFSSessionGetRepo(list[i]->session));
		err = EFSSubmissionStore(list[i], conn, txn);
		if(err < 0) break;
	}
	if(err < 0) {
		mdb_txn_abort(txn); txn = NULL;
	} else {
		rc = mdb_txn_commit(txn); txn = NULL;
		if(MDB_SUCCESS != rc) err = -1;
	}
	EFSRepoDBClose(repo, &conn);
	return err;
}

