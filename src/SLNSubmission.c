// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include "StrongLink.h"
#include "SLNDB.h"

struct SLNSubmission {
	SLNSessionRef session;
	str_t *knownURI;
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

int SLNSubmissionCreate(SLNSessionRef const session, strarg_t const knownURI, strarg_t const type, SLNSubmissionRef *const out) {
	assert(out);
	if(!SLNSessionHasPermission(session, SLN_WRONLY)) return UV_EACCES;
	if(!type) return UV_EINVAL;

	SLNSubmissionRef sub = calloc(1, sizeof(struct SLNSubmission));
	if(!sub) return UV_ENOMEM;
	int rc = 0;

	sub->session = session;
	if(knownURI) {
		sub->knownURI = strdup(knownURI);
		if(!sub->knownURI) rc = UV_ENOMEM;
		if(rc < 0) goto cleanup;
	}
	sub->type = strdup(type);
	if(!sub->type) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	sub->tmppath = SLNRepoCopyTempPath(SLNSessionGetRepo(session));
	if(!sub->tmppath) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;
	rc = async_fs_open_mkdirp(sub->tmppath, O_CREAT | O_EXCL | O_RDWR, 0400);
	if(rc < 0) goto cleanup;
	sub->tmpfile = rc;

	sub->hasher = SLNHasherCreate(sub->type);
	if(!sub->hasher) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	sub->metaFileID = 0;

	*out = sub; sub = NULL;

cleanup:
	SLNSubmissionFree(&sub);
	return rc;
}
int SLNSubmissionCreateQuick(SLNSessionRef const session, strarg_t const knownURI, strarg_t const type, ssize_t (*read)(void *, byte_t const **), void *const context, SLNSubmissionRef *const out) {
	int rc = SLNSubmissionCreate(session, knownURI, type, out);
	if(rc < 0) return rc;
	rc = SLNSubmissionWriteFrom(*out, read, context);
	if(rc < 0) SLNSubmissionFree(out);
	return rc;
}
void SLNSubmissionFree(SLNSubmissionRef *const subptr) {
	SLNSubmissionRef sub = *subptr;
	if(!sub) return;

	sub->session = NULL;
	FREE(&sub->knownURI);
	FREE(&sub->type);

	if(sub->tmppath) async_fs_unlink(sub->tmppath);
	FREE(&sub->tmppath);
	if(sub->tmpfile >= 0) async_fs_close(sub->tmpfile);
	sub->tmpfile = 0;
	sub->size = 0;

	SLNHasherFree(&sub->hasher);
	sub->metaFileID = 0;

	if(sub->URIs) for(size_t i = 0; sub->URIs[i]; ++i) FREE(&sub->URIs[i]);
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
	if(!sub) return UV_EINVAL;
	return sub->tmpfile;
}

int SLNSubmissionWrite(SLNSubmissionRef const sub, byte_t const *const buf, size_t const len) {
	if(!sub) return 0;
	assert(sub->tmpfile >= 0);

	uv_buf_t parts[] = { uv_buf_init((char *)buf, len) };
	int rc = async_fs_writeall(sub->tmpfile, parts, numberof(parts), -1);
	if(rc < 0) {
		fprintf(stderr, "SLNSubmission write error %s\n", sln_strerror(rc));
		return rc;
	}

	sub->size += len;
	SLNHasherWrite(sub->hasher, buf, len);
	return 0;
}
static int verify(SLNSubmissionRef const sub) {
	assert(sub->URIs);
	if(!sub->knownURI) return 0;
	for(size_t i = 0; sub->URIs[i]; i++) {
		// Note: this comparison assumes knownURI is normalized.
		if(0 == strcmp(sub->knownURI, sub->URIs[i])) return 0;
	}
	// TODO: In the event where we do not have the hash algorithm
	// available, we should probably pass verification, although
	// it's a little questionable.
	return UV_EIO; // TODO: EFAULT? Something else?
}
int SLNSubmissionEnd(SLNSubmissionRef const sub) {
	if(!sub) return 0;
	if(sub->size <= 0) return UV_EINVAL;
	assert(sub->tmppath);
	assert(sub->tmpfile >= 0);

	sub->URIs = SLNHasherEnd(sub->hasher);
	sub->internalHash = strdup(SLNHasherGetInternalHash(sub->hasher));
	SLNHasherFree(&sub->hasher);
	if(!sub->URIs || !sub->internalHash) return UV_ENOMEM;

	SLNRepoRef const repo = SLNSubmissionGetRepo(sub);
	str_t *internalPath = NULL;
	bool worker = false;
	int rc = 0;

	rc = verify(sub);
	if(rc < 0) goto cleanup;

	internalPath = SLNRepoCopyInternalPath(repo, sub->internalHash);
	if(!internalPath) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	async_pool_enter(NULL); worker = true;

	rc = async_fs_fdatasync(sub->tmpfile);
	if(rc < 0) goto cleanup;

	rc = async_fs_link_mkdirp(sub->tmppath, internalPath);
	if(UV_EEXIST == rc) {
		rc = 0;
		goto cleanup;
	}
	if(rc < 0) {
		fprintf(stderr, "SLNSubmission couldn't move '%s' to '%s' (%s)\n", sub->tmppath, internalPath, sln_strerror(rc));
		goto cleanup;
	}

	rc = async_fs_sync_dirname(internalPath);

cleanup:
	if(worker) { async_pool_leave(NULL); worker = false; }
	FREE(&internalPath);

	async_fs_unlink(sub->tmppath);
	FREE(&sub->tmppath);
	return rc;
}
int SLNSubmissionWriteFrom(SLNSubmissionRef const sub, ssize_t (*read)(void *, byte_t const **), void *const context) {
	if(!sub) return 0;
	assert(read);
	for(;;) {
		byte_t const *buf = NULL;
		ssize_t const len = read(context, &buf);
		if(0 == len) break;
		if(len < 0) return (int)len;
		int rc = SLNSubmissionWrite(sub, buf, len);
		if(rc < 0) return rc;
	}
	return SLNSubmissionEnd(sub);
}

strarg_t SLNSubmissionGetPrimaryURI(SLNSubmissionRef const sub) {
	if(!sub) return NULL;
	if(!sub->URIs) return NULL;
	return sub->URIs[0];
}
int SLNSubmissionGetFileInfo(SLNSubmissionRef const sub, SLNFileInfo *const info) {
	if(!sub) return UV_EINVAL;
	if(!sub->internalHash) return UV_EINVAL;
	SLNRepoRef const repo = SLNSessionGetRepo(sub->session);
	info->hash = strdup(sub->internalHash);
	info->path = SLNRepoCopyInternalPath(repo, sub->internalHash);
	info->type = strdup(sub->type);
	info->size = sub->size;
	if(!info->hash || !info->path || !info->type) {
		SLNFileInfoCleanup(info);
		return UV_ENOMEM;
	}
	return 0;
}

int SLNSubmissionStore(SLNSubmissionRef const sub, DB_txn *const txn) {
	assert(sub);
	assert(txn);
	assert(!sub->tmppath);
	// Session permissions were already checked when the sub was created.

	int64_t fileID = db_next_id(SLNFileByID, txn);
	int rc;

	DB_val dupFileID_val[1];
	SLNFileIDByInfoValPack(dupFileID_val, txn, fileID);

	DB_val fileInfo_key[1];
	SLNFileIDByInfoKeyPack(fileInfo_key, txn, sub->internalHash, sub->type);
	rc = db_put(txn, fileInfo_key, dupFileID_val, DB_NOOVERWRITE);
	if(rc >= 0) {
		DB_val fileID_key[1];
		SLNFileByIDKeyPack(fileID_key, txn, fileID);
		DB_val file_val[1];
		SLNFileByIDValPack(file_val, txn, sub->internalHash, sub->type, sub->size);
		rc = db_put(txn, fileID_key, file_val, DB_NOOVERWRITE_FAST);
		if(rc < 0) return rc;
	} else if(DB_KEYEXIST == rc) {
		fileID = db_read_uint64(dupFileID_val);
	} else return rc;

	for(size_t i = 0; sub->URIs[i]; ++i) {
		strarg_t const URI = sub->URIs[i];
		DB_val null = { 0, NULL };

		DB_val fwd[1];
		SLNFileIDAndURIKeyPack(fwd, txn, fileID, URI);
		rc = db_put(txn, fwd, &null, DB_NOOVERWRITE_FAST);
		if(rc < 0 && DB_KEYEXIST != rc) return rc;

		DB_val rev[1];
		SLNURIAndFileIDKeyPack(rev, txn, URI, fileID);
		rc = db_put(txn, rev, &null, DB_NOOVERWRITE_FAST);
		if(rc < 0 && DB_KEYEXIST != rc) return rc;
	}

	rc = SLNSubmissionParseMetaFile(sub, fileID, txn, &sub->metaFileID);
	if(rc < 0) {
		fprintf(stderr, "Submission meta-file error %s\n", sln_strerror(rc));
		return rc;
	}

	return 0;
}
int SLNSubmissionStoreBatch(SLNSubmissionRef const *const list, size_t const count) {
	if(!count) return 0;
	// Session permissions were already checked when the sub was created.

	SLNRepoRef const repo = SLNSessionGetRepo(list[0]->session);
	DB_env *db = NULL;
	SLNRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
	if(rc < 0) {
		SLNRepoDBClose(repo, &db);
		return rc;
	}
	uint64_t sortID = 0;
	rc = DB_NOTFOUND;
	for(size_t i = 0; i < count; i++) {
		if(!list[i]) continue;
		assert(repo == SLNSessionGetRepo(list[i]->session));
		rc = SLNSubmissionStore(list[i], txn);
		if(rc < 0) break;
		uint64_t const metaFileID = list[i]->metaFileID;
		if(metaFileID > sortID) sortID = metaFileID;
	}
	if(rc >= 0) {
		rc = db_txn_commit(txn); txn = NULL;
	} else {
		db_txn_abort(txn); txn = NULL;
	}
	SLNRepoDBClose(repo, &db);
	if(rc >= 0) SLNRepoSubmissionEmit(repo, sortID);
	return rc;
}

