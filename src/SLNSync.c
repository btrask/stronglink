// Copyright 2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include "StrongLink.h"
#include "SLNDB.h"

typedef struct {
	SLNSubmissionRef sub;
	async_sem_t ingest_sem[1];
	async_sem_t work_sem[1];
	async_sem_t done_sem[1];
} sync_queue;

struct SLNSync {
	SLNSessionRef session;
	sync_queue fileq[1];
	sync_queue metaq[1];
	async_sem_t shared_sem[1];
};

static void queue_init(SLNSyncRef const sync, sync_queue *const queue) {
	queue->sub = NULL;
	async_sem_init(queue->ingest_sem, 1, 0);
	async_sem_init(queue->work_sem, 0, 0);
	async_sem_init(queue->done_sem, 0, 0);
}
static void queue_destroy(SLNSyncRef const sync, sync_queue *const queue) {
	queue->sub = NULL;
	async_sem_destroy(queue->ingest_sem);
	async_sem_destroy(queue->work_sem);
	async_sem_destroy(queue->done_sem);
}
static int queue_submission(SLNSyncRef const sync, sync_queue *const queue, SLNSubmissionRef const sub) {
	int rc = async_sem_wait(queue->ingest_sem);
	if(rc < 0) return rc;

	assert(!queue->sub);
	queue->sub = sub; //sub = NULL;
	async_sem_post(queue->work_sem);
	async_sem_post(sync->shared_sem);

	rc = async_sem_wait(queue->done_sem);
	assert(rc >= 0); // TODO: Clarify ownership

	//sub = queue->sub;
	queue->sub = NULL;
	async_sem_post(queue->ingest_sem);

	return rc;
}
static int queue_ingest(SLNSyncRef const sync, sync_queue *const queue, strarg_t const URI, strarg_t const targetURI) {
	SLNSubmissionRef sub = NULL;
	int rc = SLNSubmissionCreate(sync->session, URI, targetURI, &sub);
	if(rc < 0) return rc;

	rc = queue_submission(sync, queue, sub);
	if(rc < 0) return rc;

	rc = SLNSyncStoreSubmission(sync, sub);
	SLNSubmissionFree(&sub);
	if(rc < 0) return rc;

	return rc;
}

static int get_hints_synced(SLNSyncRef const sync, DB_txn *const txn, strarg_t const URI) {
	uint64_t const sessionID = SLNSessionGetID(sync->session);
	uint64_t fileID = 0;
	int rc = SLNURIGetFileID(URI, txn, &fileID);
	if(rc < 0) return rc;
	DB_val key[1];
	SLNSessionIDAndHintsSyncedFileIDKeyPack(key, txn, sessionID, fileID);
	rc = db_get(txn, key, NULL);
	return rc;
}
static int set_hints_synced(SLNSyncRef const sync, DB_txn *const txn, strarg_t const URI) {
	uint64_t const sessionID = SLNSessionGetID(sync->session);
	uint64_t fileID = 0;
	int rc = SLNURIGetFileID(URI, txn, &fileID);
	if(rc < 0) return rc;
	DB_val key[1], val[1];
	SLNSessionIDAndHintsSyncedFileIDKeyPack(key, txn, sessionID, fileID);
	db_nullval(val);
	rc = db_put(txn, key, val, 0);
	return rc;
}
static int add_hint(SLNSyncRef const sync, DB_txn *const txn, strarg_t const metaURI, strarg_t const targetURI) {
	if(!sync) return DB_EINVAL;
	if(!metaURI) return DB_EINVAL;
	if(!targetURI) return DB_EINVAL;
	uint64_t const sessionID = SLNSessionGetID(sync->session);
	int rc;

	uint64_t nextID = SLNNextHintID(txn, sessionID);
	if(!nextID) return DB_EIO;

	DB_val mainkey[1], mainval[1];
	SLNSessionIDAndHintIDToMetaURIAndTargetURIKeyPack(mainkey, txn, sessionID, nextID);
	SLNSessionIDAndHintIDToMetaURIAndTargetURIValPack(mainval, txn, metaURI, targetURI);
	rc = db_put(txn, mainkey, mainval, DB_NOOVERWRITE_FAST);
	if(rc < 0) return rc;

	DB_val fwdkey[1], fwdval[1];
	SLNMetaURIAndSessionIDToHintIDKeyPack(fwdkey, txn, metaURI, sessionID);
	SLNMetaURIAndSessionIDToHintIDValPack(fwdval, txn, nextID);
	rc = db_put(txn, fwdkey, fwdval, DB_NOOVERWRITE_FAST);
	if(rc < 0) return rc;

	DB_val revkey[1], revval[1];
	SLNTargetURISessionIDAndHintIDKeyPack(revkey, txn, targetURI, sessionID, nextID);
	db_nullval(revval);
	rc = db_put(txn, revkey, revval, DB_NOOVERWRITE_FAST);
	return rc;
}


int SLNSyncCreate(SLNSessionRef const session, SLNSyncRef *const out) {
	assert(out);
	if(!session) return DB_EINVAL;
	SLNSyncRef sync = calloc(1, sizeof(struct SLNSync));
	if(!sync) return DB_ENOMEM;
	sync->session = session;
	queue_init(sync, sync->fileq);
	queue_init(sync, sync->metaq);
	async_sem_init(sync->shared_sem, 0, 0);
	*out = sync;
	return 0;
}
void SLNSyncFree(SLNSyncRef *const syncptr) {
	assert(syncptr);
	SLNSyncRef sync = *syncptr;
	if(!sync) return;
	sync->session = NULL;
	queue_destroy(sync, sync->fileq);
	queue_destroy(sync, sync->metaq);
	async_sem_destroy(sync->shared_sem);
	assert_zeroed(sync, 1);
	FREE(syncptr); sync = NULL;
}
int SLNSyncFileAvailable(SLNSyncRef const sync, strarg_t const URI, strarg_t const targetURI) {
	if(!URI) return DB_EINVAL;
	DB_env *db = NULL;
	DB_txn *txn = NULL;
	int rc;

	// Files never need writing at this stage. Just check if they exist.
	// For meta-files, the situation is less clear. Obviously it'd be
	// ideal to use read-only transactions for them too. However, we need
	// to check whether the target has "synced" its hints, and if not
	// immediately (atomically) add our own hint to the queue.
	bool const isMeta = !!targetURI;
	unsigned const mode = isMeta ? DB_RDWR : DB_RDONLY;

	rc = SLNSessionDBOpen(sync->session, SLN_RDWR, &db);
	if(rc < 0) goto cleanup;
	rc = db_txn_begin(db, NULL, mode, &txn);
	if(rc < 0) goto cleanup;

	uint64_t fileID = 0;
	rc = SLNURIGetFileID(URI, txn, &fileID);
	if(rc >= 0) {
		// We have the (meta-)file. We're OK.
		// TODO: Verify target URI if set.
		rc = 0;
	} else if(DB_NOTFOUND == rc) {
		if(!targetURI) {
			// Ordinary file, needs adding as usual.
			rc = DB_NOTFOUND;
		} else {
			// Meta-file, need to decide whether to add or queue.
			// Needs to be atomic with the below add_hint.
			rc = get_hints_synced(sync, txn, targetURI);
			if(rc >= 0) {
				// We have the previous hints,
				// meaning we're ready to add.
				rc = DB_NOTFOUND;
			} else if(DB_NOTFOUND == rc) {
				// We don't have the previous hints,
				// meaning we should queue ours.
				rc = add_hint(sync, txn, URI, targetURI);
				if(DB_NOTFOUND == rc) rc = DB_PANIC;
				if(rc < 0) goto cleanup;
				rc = db_txn_commit(txn); txn = NULL;
				if(rc < 0) goto cleanup;

				// Can't do anything until later.
				rc = 0;
			}
		}
	}

cleanup:
	db_txn_abort(txn); txn = NULL;
	SLNSessionDBClose(sync->session, &db);
	return rc;
}

int SLNSyncIngestFileURI(SLNSyncRef const sync, strarg_t const fileURI) {
	if(!sync) return DB_EINVAL;
	if(!fileURI) return DB_EINVAL;
	alogf("file: %s\n", fileURI);
	int rc = SLNSyncFileAvailable(sync, fileURI, NULL);
	if(rc >= 0) return rc;
	if(DB_NOTFOUND != rc) return rc;
	return queue_ingest(sync, sync->fileq, fileURI, NULL);
}
int SLNSyncIngestMetaURI(SLNSyncRef const sync, strarg_t const metaURI, strarg_t const targetURI) {
	if(!sync) return DB_EINVAL;
	if(!metaURI) return DB_EINVAL;
	if(!targetURI) return DB_EINVAL;
	alogf("meta: %s -> %s\n", metaURI, targetURI);
	int rc = SLNSyncFileAvailable(sync, metaURI, targetURI);
	if(rc >= 0) return rc;
	if(DB_NOTFOUND != rc) return rc;
	return queue_ingest(sync, sync->metaq, metaURI, targetURI);
}
int SLNSyncWorkAwait(SLNSyncRef const sync, SLNSubmissionRef *const out) {
	if(!sync) return DB_EINVAL;
	int rc = async_sem_wait(sync->shared_sem);
	if(rc < 0) return rc;

	// TODO: Pretty hacky...
	rc = async_sem_trywait(sync->fileq->work_sem);
	if(rc >= 0) {
		*out = sync->fileq->sub;
		return 0;
	}
	rc = async_sem_trywait(sync->metaq->work_sem);
	if(rc >= 0) {
		*out = sync->metaq->sub;
		return 0;
	}
	assert(!"sync scheduling");
	return -1;
}
int SLNSyncWorkDone(SLNSyncRef const sync, SLNSubmissionRef const sub) {
	if(!sync) return DB_EINVAL;
	// TODO: Copy and paste...
	if(sub == sync->fileq->sub) {
		async_sem_post(sync->fileq->done_sem);
		return 0;
	}
	if(sub == sync->metaq->sub) {
		async_sem_post(sync->metaq->done_sem);
		return 0;
	}
	return DB_EINVAL;
}

int SLNSyncNextHintID(SLNSyncRef const sync, DB_txn *const txn, strarg_t const targetURI, uint64_t *const hintID) {
	assert(hintID);
	if(!sync) return DB_EINVAL;
	DB_cursor *synonyms = NULL;
	DB_cursor *cursor = NULL;
	uint64_t const sessionID = SLNSessionGetID(sync->session);
	uint64_t const first = *hintID;
	uint64_t earliest = UINT64_MAX;
	int rc;

	uint64_t fileID = 0;
	rc = SLNURIGetFileID(targetURI, txn, &fileID);
	if(rc < 0) goto cleanup;

	rc = db_cursor_open(txn, &synonyms);
	if(rc < 0) goto cleanup;
	rc = db_txn_cursor(txn, &cursor);
	if(rc < 0) goto cleanup;

	DB_range alts[1];
	DB_val alt[1];
	SLNFileIDAndURIRange1(alts, txn, fileID);
	rc = db_cursor_firstr(synonyms, alts, alt, NULL, +1);
	for(; rc >= 0; rc = db_cursor_nextr(synonyms, alts, alt, NULL, +1)) {
		uint64_t f;
		strarg_t synonym;
		SLNFileIDAndURIKeyUnpack(alt, txn, &f, &synonym);

		DB_range range[1];
		DB_val key[1];
		SLNTargetURISessionIDAndHintIDRange2(range, txn, synonym, sessionID);
		SLNTargetURISessionIDAndHintIDKeyPack(key, txn, synonym, sessionID, first);
		rc = db_cursor_seekr(cursor, range, key, NULL, +1);
		if(DB_NOTFOUND == rc) continue;
		if(rc < 0) goto cleanup;

		strarg_t u;
		uint64_t s;
		uint64_t this = 0;
		SLNTargetURISessionIDAndHintIDKeyUnpack(key, txn, &u, &s, &this);
		if(this < earliest) earliest = this;
	}
	if(UINT64_MAX == earliest) {
		rc = DB_NOTFOUND;
		goto cleanup;
	}

	*hintID = earliest;

cleanup:
	db_cursor_close(synonyms); synonyms = NULL;
	cursor = NULL; // txn-cursor doesn't need closing.
	return rc;
}
int SLNSyncStoreSubmission(SLNSyncRef const sync, SLNSubmissionRef const sub) {
	if(!sync) return DB_EINVAL;
	if(!sub) return DB_EINVAL;
	uint64_t const sessionID = SLNSessionGetID(sync->session);
	SLNSubmissionRef dep = NULL;
	DB_env *db = NULL;
	DB_txn *txn = NULL;
	int rc = 0;

	rc = SLNSessionDBOpen(sync->session, SLN_RDWR, &db);
	if(rc < 0) goto cleanup;
	rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
	if(rc < 0) goto cleanup;

	rc = SLNSubmissionStore(sub, txn);
	if(rc < 0) goto cleanup;

	strarg_t const URI = SLNSubmissionGetPrimaryURI(sub);

	// TODO: SLNSubmissionIsMetafile() ?
	bool const isMeta = !!SLNSubmissionGetKnownTarget(sub);
	if(!isMeta) {
		// Keeping this in the same function makes sense
		// because we need to manipulate the outer txn.
		uint64_t hintID = 0;
		for(;;) {
			rc = SLNSyncNextHintID(sync, txn, URI, &hintID);
			if(DB_NOTFOUND == rc) break;
			if(rc < 0) goto cleanup;

			DB_val hintkey[1], hintval[1];
			SLNSessionIDAndHintIDToMetaURIAndTargetURIKeyPack(hintkey, txn, sessionID, hintID);
			rc = db_get(txn, hintkey, hintval);
			if(rc < 0) goto cleanup;
			strarg_t u, t;
			SLNSessionIDAndHintIDToMetaURIAndTargetURIValUnpack(hintval, txn, &u, &t);
			assert(0 == strcmp(URI, t));
			str_t metaURI[SLN_URI_MAX];
			strlcpy(metaURI, u, sizeof(metaURI));

			DB_cursor *cursor = NULL;
			rc = db_txn_cursor(txn, &cursor);
			if(rc < 0) goto cleanup;
			DB_range exists[1];
			SLNURIAndFileIDRange1(exists, txn, metaURI);
			rc = db_cursor_firstr(cursor, exists, NULL, NULL, +1);
			if(rc >= 0) continue;
			if(DB_NOTFOUND != rc) goto cleanup;

			db_txn_commit(txn); txn = NULL;
			SLNSessionDBClose(sync->session, &db);


			rc = SLNSubmissionCreate(sync->session, metaURI, URI, &dep);
			if(rc < 0) goto cleanup;

			// This skips any checks about whether we have
			// the meta-file or target.
			// It'd be nice to jump the queue in this case.
			// Just as an optimization.
			rc = queue_submission(sync, sync->metaq, dep);
			if(rc < 0) goto cleanup;


			rc = SLNSessionDBOpen(sync->session, SLN_RDWR, &db);
			if(rc < 0) goto cleanup;
			rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
			if(rc < 0) goto cleanup;

			rc = SLNSubmissionStore(dep, txn);
			SLNSubmissionFree(&dep);
			if(rc < 0) goto cleanup;
		}

		// It's critical that this happens in the same transaction
		// as the previous call to SLNSessionNextHintID()!
		rc = set_hints_synced(sync, txn, URI);
		if(rc < 0) goto cleanup;
	}

	// Store last file/meta URIs.
	// It's critical that this happens after set_hints_synced,
	// so we restart the hints e.g. in the event of a crash.
	DB_val key[1], val[1];
	DB_VAL_STORAGE(key, DB_VARINT_MAX*2);
	db_bind_uint64(key, isMeta ?
		SLNLastMetaURIBySyncID :
		SLNLastFileURIBySyncID);
	db_bind_uint64(key, sessionID);
	DB_VAL_STORAGE_VERIFY(key);
	DB_VAL_STORAGE(val, DB_INLINE_MAX);
	db_bind_string(val, URI, txn);
	DB_VAL_STORAGE_VERIFY(val);
	rc = db_put(txn, key, val, 0);
	if(rc < 0) goto cleanup;

cleanup:
	db_txn_abort(txn); txn = NULL;
	SLNSessionDBClose(sync->session, &db);
	SLNSubmissionFree(&dep);
	return rc;
}
int SLNSyncCopyLastSubmissionURIs(SLNSyncRef const sync, str_t *const outFileURI, str_t *const outMetaURI) {
	uint64_t const sessionID = SLNSessionGetID(sync->session);
	DB_env *db = NULL;
	DB_txn *txn = NULL;
	int rc = SLNSessionDBOpen(sync->session, SLN_RDWR, &db);
	if(rc < 0) goto cleanup;
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(rc < 0) goto cleanup;

	if(outFileURI) {
		DB_val key[1], val[1];
		DB_VAL_STORAGE(key, DB_VARINT_MAX*2);
		db_bind_uint64(key, SLNLastFileURIBySyncID);
		db_bind_uint64(key, sessionID);
		DB_VAL_STORAGE_VERIFY(key);
		rc = db_get(txn, key, val);
		if(rc >= 0) {
			strarg_t const URI = db_read_string(val, txn);
			strlcpy(outFileURI, URI, SLN_URI_MAX);
		} else if(DB_NOTFOUND == rc) {
			outFileURI[0] = '\0';
			rc = 0;
		} else {
			goto cleanup;
		}
	}
	if(outMetaURI) {
		DB_val key[1], val[1];
		DB_VAL_STORAGE(key, DB_VARINT_MAX*2);
		db_bind_uint64(key, SLNLastMetaURIBySyncID);
		db_bind_uint64(key, sessionID);
		DB_VAL_STORAGE_VERIFY(key);
		rc = db_get(txn, key, val);
		if(rc >= 0) {
			strarg_t const URI = db_read_string(val, txn);
			strlcpy(outMetaURI, URI, SLN_URI_MAX);
		} else if(DB_NOTFOUND == rc) {
			outMetaURI[0] = '\0';
			rc = 0;
		} else {
			goto cleanup;
		}
	}

cleanup:
	db_txn_abort(txn); txn = NULL;
	SLNSessionDBClose(sync->session, &db);
	return rc;
}

