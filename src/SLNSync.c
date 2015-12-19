// Copyright 2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include "StrongLink.h"

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

	rc = SLNSubmissionStoreBatch(&sub, 1);
	SLNSubmissionFree(&sub);
	if(rc < 0) return rc;

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
//	DB_env *db = NULL;
//	DB_txn *txn = NULL;
	int rc;

//	rc = SLNSessionDBOpen(sync->session, SLN_RDWR, &db);
//	if(rc < 0) goto cleanup;
//	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
//	if(rc < 0) goto cleanup;

	rc = SLNSessionGetFileInfo(sync->session, URI, NULL);
	if(DB_NOTFOUND == rc) {
		rc = targetURI ?
			SLNSessionGetFileInfo(sync->session, targetURI, NULL) :
			0;
		if(DB_NOTFOUND == rc) {
//			db_txn_abort(txn); txn = NULL;
			rc = SLNSessionAddHint(sync->session, URI, targetURI);
			if(DB_NOTFOUND == rc) rc = DB_PANIC;
		} else if(rc >= 0) {
			rc = DB_NOTFOUND;
		}



	// TODO: Handle adding dependent meta-files once the file is added.
/*	} else if(SLN_NOSESSION == rc) {
		// We already have the meta-file,
		// just not under this session.

		// TODO: Verify target URI if set.
		db_txn_abort(txn); txn = NULL;

		for(uint64_t hintID = 0;; hintID++) {
			str_t metaURI[SLN_URI_MAX];
			rc = SLNSessionGetNextHintURI(sync->session, URI, &hintID, metaURI, sizeof(metaURI));
			if(DB_NOTFOUND == rc) break;
			if(rc < 0) goto cleanup;

			// TODO: Do we need to call check-file as part of ingest?
			// Probably not since the meta-files should be in a known state...
			rc = queue_ingest(sync, sync->metaq, metaURI, URI);
			if(rc < 0) goto cleanup;
		}

		rc = setsubmittedfile(sync->session, URI);
		if(DB_NOTFOUND == rc) rc = DB_PANIC;*/



	} else if(rc >= 0) {

		// TODO: Verify target URI if set.

	}

//cleanup:
//	db_txn_abort(txn); txn = NULL;
//	SLNSessionDBClose(sync->session, &db);
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

