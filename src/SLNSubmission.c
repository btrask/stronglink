#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include "StrongLink.h"
#include "SLNDB.h"

struct SLNSubmission {
	SLNSessionRef session;
	str_t *type;

	str_t *tmppath;
	uv_file tmpfile;
	uint64_t size;

	SLNHasherRef hasher;
	uint64_t metaFileID;

	str_t **URIs;
	str_t *internalHash;
};

int SLNSubmissionParseMetaFile(SLNSubmissionRef const sub, uint64_t const fileID, DB_txn *const txn, uint64_t *const out);

SLNSubmissionRef SLNSubmissionCreate(SLNSessionRef const session, strarg_t const type) {
	if(!session) return NULL;
	assert(type);

	if(!SLNSessionHasPermission(session, SLN_WRONLY)) return NULL;

	SLNSubmissionRef sub = calloc(1, sizeof(struct SLNSubmission));
	if(!sub) return NULL;
	sub->session = session;
	sub->type = strdup(type);

	sub->tmppath = SLNRepoCopyTempPath(SLNSessionGetRepo(session));
	sub->tmpfile = async_fs_open(sub->tmppath, O_CREAT | O_EXCL | O_TRUNC | O_RDWR, 0400);
	if(sub->tmpfile < 0) {
		if(UV_ENOENT == sub->tmpfile) {
			async_fs_mkdirp_dirname(sub->tmppath, 0700);
			sub->tmpfile = async_fs_open(sub->tmppath, O_CREAT | O_EXCL | O_TRUNC | O_WRONLY, 0400);
		}
		if(sub->tmpfile < 0) {
			fprintf(stderr, "Error: couldn't create temp file %s (%s)\n", sub->tmppath, uv_err_name(sub->tmpfile));
			SLNSubmissionFree(&sub);
			return NULL;
		}
	}

	sub->hasher = SLNHasherCreate(sub->type);
	sub->metaFileID = 0;

	return sub;
}
void SLNSubmissionFree(SLNSubmissionRef *const subptr) {
	SLNSubmissionRef sub = *subptr;
	if(!sub) return;

	sub->session = NULL;
	FREE(&sub->type);

	if(sub->tmppath) async_fs_unlink(sub->tmppath);
	FREE(&sub->tmppath);
	if(sub->tmpfile >= 0) async_fs_close(sub->tmpfile);
	sub->tmpfile = 0;
	sub->size = 0;

	SLNHasherFree(&sub->hasher);
	sub->metaFileID = 0;

	if(sub->URIs) for(index_t i = 0; sub->URIs[i]; ++i) FREE(&sub->URIs[i]);
	FREE(&sub->URIs);
	FREE(&sub->internalHash);

	assert_zeroed(sub, 1);
	FREE(subptr); sub = NULL;
}

SLNRepoRef SLNSubmissionGetRepo(SLNSubmissionRef const sub) {
	if(!sub) return NULL;
	return SLNSessionGetRepo(sub->session);
}
strarg_t SLNSubmissionGetType(SLNSubmissionRef const sub) {
	if(!sub) return NULL;
	return sub->type;
}
uv_file SLNSubmissionGetFile(SLNSubmissionRef const sub) {
	if(!sub) return -1;
	return sub->tmpfile;
}

int SLNSubmissionWrite(SLNSubmissionRef const sub, byte_t const *const buf, size_t const len) {
	if(!sub) return 0;
	if(sub->tmpfile < 0) return -1;

	uv_buf_t info = uv_buf_init((char *)buf, len);
	ssize_t const result = async_fs_write(sub->tmpfile, &info, 1, sub->size);
	if(result < 0) {
		fprintf(stderr, "SLNSubmission write error %ld\n", (long)result);
		return -1;
	}

	sub->size += len;
	SLNHasherWrite(sub->hasher, buf, len);
	return 0;
}
static int add(SLNSubmissionRef const sub) {
	if(!sub) return -1;
	if(!sub->tmppath) return -1;
	if(!sub->size) return -1;
	SLNRepoRef const repo = SLNSubmissionGetRepo(sub);
	str_t *internalPath = SLNRepoCopyInternalPath(repo, sub->internalHash);
	int result = 0;
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
int SLNSubmissionEnd(SLNSubmissionRef const sub) {
	if(!sub) return 0;
	if(sub->tmpfile < 0) return -1;
	sub->URIs = SLNHasherEnd(sub->hasher);
	sub->internalHash = strdup(SLNHasherGetInternalHash(sub->hasher));
	SLNHasherFree(&sub->hasher);

	if(async_fs_fsync(sub->tmpfile) < 0) return -1;
	return add(sub);
}
int SLNSubmissionWriteFrom(SLNSubmissionRef const sub, ssize_t (*read)(void *, byte_t const **), void *const context) {
	if(!sub) return 0;
	assertf(read, "Read function required");
	for(;;) {
		byte_t const *buf = NULL;
		ssize_t const len = read(context, &buf);
		if(0 == len) break;
		if(len < 0) return -1;
		if(SLNSubmissionWrite(sub, buf, len) < 0) return -1;
	}
	if(SLNSubmissionEnd(sub) < 0) return -1;
	return 0;
}

strarg_t SLNSubmissionGetPrimaryURI(SLNSubmissionRef const sub) {
	if(!sub) return NULL;
	if(!sub->URIs) return NULL;
	return sub->URIs[0];
}

int SLNSubmissionStore(SLNSubmissionRef const sub, DB_txn *const txn) {
	if(!sub) return -1;
	assert(txn);
	if(sub->tmppath) return -1;
	SLNSessionRef const session = sub->session;
	SLNRepoRef const repo = SLNSubmissionGetRepo(sub);
	int64_t const userID = SLNSessionGetUserID(session);

	int64_t fileID = db_next_id(SLNFileByID, txn);
	int rc;

	DB_val dupFileID_val[1];
	SLNFileIDByInfoValPack(dupFileID_val, txn, fileID);

	DB_val fileInfo_key[1];
	SLNFileIDByInfoKeyPack(fileInfo_key, txn, sub->internalHash, sub->type);
	rc = db_put(txn, fileInfo_key, dupFileID_val, DB_NOOVERWRITE);
	if(DB_SUCCESS == rc) {
		DB_val fileID_key[1];
		SLNFileByIDKeyPack(fileID_key, txn, fileID);
		DB_val file_val[1];
		SLNFileByIDValPack(file_val, txn, sub->internalHash, sub->type, sub->size);
		rc = db_put(txn, fileID_key, file_val, DB_NOOVERWRITE_FAST);
		if(DB_SUCCESS != rc) return -1;
	} else if(DB_KEYEXIST == rc) {
		fileID = db_read_uint64(dupFileID_val);
	} else return -1;

	for(index_t i = 0; sub->URIs[i]; ++i) {
		strarg_t const URI = sub->URIs[i];
		DB_val null = { 0, NULL };

		DB_val fwd[1];
		SLNFileIDAndURIKeyPack(fwd, txn, fileID, URI);
		rc = db_put(txn, fwd, &null, DB_NOOVERWRITE_FAST);
		assert(DB_SUCCESS == rc || DB_KEYEXIST == rc);

		DB_val rev[1];
		SLNURIAndFileIDKeyPack(rev, txn, URI, fileID);
		rc = db_put(txn, rev, &null, DB_NOOVERWRITE_FAST);
		assert(DB_SUCCESS == rc || DB_KEYEXIST == rc);
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


	rc = SLNSubmissionParseMetaFile(sub, fileID, txn, &sub->metaFileID);
	if(rc < 0) {
		fprintf(stderr, "Submission meta-file error %s\n", db_strerror(rc));
		return rc;
	}

	return 0;
}

SLNSubmissionRef SLNSubmissionCreateQuick(SLNSessionRef const session, strarg_t const type, ssize_t (*read)(void *, byte_t const **), void *const context) {
	SLNSubmissionRef sub = SLNSubmissionCreate(session, type);
	if(!sub) return NULL;
	int rc = SLNSubmissionWriteFrom(sub, read, context);
	if(rc < 0) SLNSubmissionFree(&sub);
	return sub;
}
int SLNSubmissionBatchStore(SLNSubmissionRef const *const list, count_t const count) {
	if(!count) return 0;
	SLNRepoRef const repo = SLNSessionGetRepo(list[0]->session);
	DB_env *db = NULL;
	SLNRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
	if(DB_SUCCESS != rc) {
		SLNRepoDBClose(repo, &db);
		return -1;
	}
	int err = 0;
	uint64_t sortID = 0;
	for(index_t i = 0; i < count; ++i) {
		assert(list[i]);
		assert(repo == SLNSessionGetRepo(list[i]->session));
		err = SLNSubmissionStore(list[i], txn);
		if(err < 0) break;
		uint64_t const metaFileID = list[i]->metaFileID;
		if(metaFileID > sortID) sortID = metaFileID;
	}
	if(err < 0) {
		db_txn_abort(txn); txn = NULL;
	} else {
		rc = db_txn_commit(txn); txn = NULL;
		if(DB_SUCCESS != rc) err = -1;
	}
	SLNRepoDBClose(repo, &db);
	if(err >= 0) SLNRepoSubmissionEmit(repo, sortID);
	return err;
}

