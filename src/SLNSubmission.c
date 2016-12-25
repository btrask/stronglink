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
	str_t *knownTarget;
	str_t *type;

	str_t *tmppath;
	uv_file tmpfile;
	uint64_t size;

	SLNHasherRef hasher;
	uint64_t fileID;
	uint64_t metaFileID; // TODO: Don't store both of these...

	str_t **URIs;
	str_t *primaryURI;
	str_t *internalHash;
};

int SLNSubmissionParseMetaFile(SLNSubmissionRef const sub, uint64_t const fileID, KVS_txn *const txn, uint64_t *const out);

int SLNSubmissionCreate(SLNSessionRef const session, strarg_t const knownURI, strarg_t const knownTarget, SLNSubmissionRef *const out) {
	assert(out);
	if(!SLNSessionHasPermission(session, SLN_WRONLY)) return UV_EACCES;

	SLNSubmissionRef sub = calloc(1, sizeof(struct SLNSubmission));
	if(!sub) return UV_ENOMEM;
	int rc = 0;

	sub->session = session;
	if(knownURI) {
		sub->knownURI = strdup(knownURI);
		if(!sub->knownURI) rc = UV_ENOMEM;
		if(rc < 0) goto cleanup;
	}
	if(knownTarget) {
		sub->knownTarget = strdup(knownTarget);
		if(!sub->knownTarget) rc = UV_ENOMEM;
		if(rc < 0) goto cleanup;
	}
	sub->type = NULL;

	sub->tmppath = SLNRepoCopyTempPath(SLNSessionGetRepo(session));
	if(!sub->tmppath) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;
	rc = async_fs_open_mkdirp(sub->tmppath, O_CREAT | O_EXCL | O_RDWR, 0400);
	if(rc < 0) goto cleanup;
	sub->tmpfile = rc;

	sub->metaFileID = 0;

	*out = sub; sub = NULL;

cleanup:
	SLNSubmissionFree(&sub);
	return rc;
}
int SLNSubmissionCreateQuick(SLNSessionRef const session, strarg_t const knownURI, strarg_t const knownTarget, strarg_t const type, ssize_t (*read)(void *, byte_t const **), void *const context, SLNSubmissionRef *const out) {
	assert(out);
	SLNSubmissionRef sub = NULL;
	int rc = SLNSubmissionCreate(session, knownURI, knownTarget, &sub);
	if(rc < 0) return rc;
	rc = SLNSubmissionSetType(sub, type);
	if(rc < 0) goto cleanup;
	rc = SLNSubmissionWriteFrom(sub, read, context);
	if(rc < 0) goto cleanup;
	*out = sub; sub = NULL;
cleanup:
	SLNSubmissionFree(&sub);
	return rc;
}
void SLNSubmissionFree(SLNSubmissionRef *const subptr) {
	SLNSubmissionRef sub = *subptr;
	if(!sub) return;

	sub->session = NULL;
	FREE(&sub->knownURI);
	FREE(&sub->knownTarget);
	FREE(&sub->type);

	if(sub->tmppath) async_fs_unlink(sub->tmppath);
	FREE(&sub->tmppath);
	if(sub->tmpfile >= 0) async_fs_close(sub->tmpfile);
	sub->tmpfile = 0;
	sub->size = 0;

	SLNHasherFree(&sub->hasher);
	sub->fileID = 0;
	sub->metaFileID = 0;

	if(sub->URIs) for(size_t i = 0; sub->URIs[i]; ++i) FREE(&sub->URIs[i]);
	FREE(&sub->URIs);
	FREE(&sub->primaryURI);
	FREE(&sub->internalHash);

	assert_zeroed(sub, 1);
	FREE(subptr); sub = NULL;
}

SLNRepoRef SLNSubmissionGetRepo(SLNSubmissionRef const sub) {
	if(!sub) return NULL;
	return SLNSessionGetRepo(sub->session);
}
strarg_t SLNSubmissionGetKnownURI(SLNSubmissionRef const sub) {
	if(!sub) return NULL;
	return sub->knownURI;
}
strarg_t SLNSubmissionGetKnownTarget(SLNSubmissionRef const sub) {
	if(!sub) return NULL;
	return sub->knownTarget;
}
strarg_t SLNSubmissionGetType(SLNSubmissionRef const sub) {
	if(!sub) return NULL;
	return sub->type;
}
int SLNSubmissionSetType(SLNSubmissionRef const sub, strarg_t const type) {
	if(!sub) return UV_EINVAL;
	if(!type) return UV_EINVAL;
	assert(!sub->hasher);

	FREE(&sub->type);
	sub->type = strdup(type);
	if(!sub->type) return UV_ENOMEM;

	sub->hasher = SLNHasherCreate(sub->type);
	if(!sub->hasher) return UV_ENOMEM;

	return 0;
}
uv_file SLNSubmissionGetFile(SLNSubmissionRef const sub) {
	if(!sub) return UV_EINVAL;
	return sub->tmpfile;
}
uint64_t SLNSubmissionGetFileID(SLNSubmissionRef const sub) {
	assert(sub);
	return sub->fileID;
}

int SLNSubmissionWrite(SLNSubmissionRef const sub, byte_t const *const buf, size_t const len) {
	if(!sub) return 0;
	assert(sub->tmpfile >= 0);
	assert(sub->type);
	assert(sub->hasher);

	uv_buf_t parts[] = { uv_buf_init((char *)buf, len) };
	int rc = async_fs_writeall(sub->tmpfile, parts, numberof(parts), -1);
	if(rc < 0) {
		alogf("SLNSubmission write error: %s\n", sln_strerror(rc));
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
	return SLN_HASHMISMATCH;
}
int SLNSubmissionEnd(SLNSubmissionRef const sub) {
	if(!sub) return 0;
	if(sub->size <= 0) return UV_EINVAL;
	assert(sub->tmppath);
	assert(sub->tmpfile >= 0);
	assert(sub->type);
	assert(sub->hasher);

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

	// We use link(2) rather than rename(2) because link gives an error
	// if there's a name collision, rather than overwriting. We want to
	// keep the oldest file for any given hash, rather than the newest.
	rc = async_fs_link_mkdirp(sub->tmppath, internalPath);
	if(UV_EEXIST == rc) {
		rc = 0;
		goto cleanup;
	}
	if(rc < 0) {
		alogf("SLNSubmission couldn't move '%s' to '%s' (%s)\n", sub->tmppath, internalPath, sln_strerror(rc));
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
	if(!sub->primaryURI) {
		if(!sub->internalHash) return NULL;
		sub->primaryURI = SLNFormatURI(SLN_INTERNAL_ALGO, sub->internalHash);
	}
	return sub->primaryURI;
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

int SLNSubmissionStore(SLNSubmissionRef const sub, KVS_txn *const txn) {
	assert(sub);
	assert(txn);
	assert(!sub->tmppath);
	// Session permissions were already checked when the sub was created.

	int64_t fileID = kvs_next_id(SLNFileByID, txn);
	int rc;

	KVS_val dupFileID_val[1];
	SLNFileIDByInfoValPack(dupFileID_val, txn, fileID);

	KVS_val fileInfo_key[1];
	SLNFileIDByInfoKeyPack(fileInfo_key, txn, sub->internalHash, sub->type);
	rc = kvs_put(txn, fileInfo_key, dupFileID_val, KVS_NOOVERWRITE);
	if(rc >= 0) {
		KVS_val fileID_key[1];
		SLNFileByIDKeyPack(fileID_key, txn, fileID);
		KVS_val file_val[1];
		SLNFileByIDValPack(file_val, txn, sub->internalHash, sub->type, sub->size);
		rc = kvs_put(txn, fileID_key, file_val, KVS_NOOVERWRITE_FAST);
		if(rc < 0) return rc;
	} else if(KVS_KEYEXIST == rc) {
		fileID = kvs_read_uint64(dupFileID_val);
	} else return rc;

	KVS_val null[1];

	uint64_t const sessionID = SLNSessionGetID(sub->session);
	KVS_val session_key[1];
	SLNFileIDAndSessionIDKeyPack(session_key, txn, fileID, sessionID);
	kvs_nullval(null);
	rc = kvs_put(txn, session_key, null, 0);
	if(rc < 0) return rc;

	for(size_t i = 0; sub->URIs[i]; ++i) {
		strarg_t const URI = sub->URIs[i];

		KVS_val fwd[1];
		SLNFileIDAndURIKeyPack(fwd, txn, fileID, URI);
		kvs_nullval(null);
		rc = kvs_put(txn, fwd, null, KVS_NOOVERWRITE_FAST);
		if(rc < 0 && KVS_KEYEXIST != rc) return rc;

		KVS_val rev[1];
		SLNURIAndFileIDKeyPack(rev, txn, URI, fileID);
		kvs_nullval(null);
		rc = kvs_put(txn, rev, null, KVS_NOOVERWRITE_FAST);
		if(rc < 0 && KVS_KEYEXIST != rc) return rc;
	}

	uint64_t metaFileID = 0;
	rc = SLNSubmissionParseMetaFile(sub, fileID, txn, &metaFileID);
	if(rc < 0) {
		alogf("Submission meta-file error: %s\n", sln_strerror(rc));
		return rc;
	}

	sub->fileID = fileID;
	sub->metaFileID = metaFileID;

	return 0;
}
int SLNSubmissionStoreBatch(SLNSubmissionRef const *const list, size_t const count) {
	if(!count) return 0;

	SLNSessionRef const session = list[0]->session;
	SLNRepoRef const repo = SLNSessionGetRepo(session);
	KVS_env *db = NULL;
	int rc = SLNSessionDBOpen(session, SLN_WRONLY, &db);
	if(rc < 0) return rc;
	KVS_txn *txn = NULL;
	rc = kvs_txn_begin(db, NULL, KVS_RDWR, &txn);
	if(rc < 0) {
		SLNSessionDBClose(session, &db);
		return rc;
	}
	uint64_t sortID = 0;
	rc = KVS_NOTFOUND;
	for(size_t i = 0; i < count; i++) {
		if(!list[i]) continue;
		assert(repo == SLNSessionGetRepo(list[i]->session));
		rc = SLNSubmissionStore(list[i], txn);
		if(rc < 0) break;
		sortID = MAX(sortID, SLNSubmissionGetFileID(list[i]));
	}
	if(rc >= 0) {
		rc = kvs_txn_commit(txn); txn = NULL;
	} else {
		kvs_txn_abort(txn); txn = NULL;
	}
	SLNSessionDBClose(session, &db);
	if(rc >= 0) SLNRepoSubmissionEmit(repo, sortID);
	return rc;
}

