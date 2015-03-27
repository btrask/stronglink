


/*


okay
so combining what we've got from 2015-03-10-sync.c and 2015-03-21-filters.c
we need a new sync system where:
1. meta-files and transferred separately
2. the transport is abstracted to support pushes and pulls
3. database writes (and maybe reads) can be efficiently batched

this means a lot of changes at once, which will probably get confusing
but it seems more practical than trying to make the changes one at a time



first of all
the sync object should be a callee
rather than worrying about polymorphism, it should just be up to the client how its used
so for pulls, you create a sync object and then create an outgoing connection for it
for pushes, you create a sync and then feed it the incoming connection

*/

int EFSSyncReceiveFileURI(EFSSyncRef const sync, strarg_t const URI);
int EFSSyncReceiveMetaFileURI(EFSSyncRef const sync, strarg_t const URI, strarg_t const target);
int EFSSyncRequestURI(EFSSyncRef const sync, str_t *const out, size_t const max);
int EFSSyncReceiveFile(EFSSyncRef const sync, strarg_t const path);

// instead of using URIs and file paths, the sync should create submission objects?
// also "receive" and "request" are highly ambiguous

EFSSyncAddFileURI
EFSSyncAddMetaFileURI
EFSSyncSubmissionStart(sync, *sub)
EFSSyncSubmissionEnd(sync, sub)

// actually it cant create the sub because it doesnt know what type


EFSSyncRequestStart(sync, URI, max)
EFSSyncRequestEnd(sync, sub)


// another related question is, websocket?

// hang on
// the reason we might need websocket
// is when we're blocked on the receiving end for some reason
// the sender can use application-level keepalive packets
// but the receiver cant, so if we have to block while receiving we have to do something else
// but when do we actually need to block while receiving, and can we change it so we dont?

// ...i dont think we do
// in which case we dont have to worry about it, thank god

// some people will complain about abusing http for long-standing connections
// but i don't care, i think it's the best option available














typedef struct EFSSync *EFSSyncRef;


#define BATCH_MAX 32
#define LATENCY_MAX 40 /* milliseconds */

struct queues {
	size_t fileURICount;
	str_t *fileURIs[BATCH_MAX];
	size_t metaFileURICount;
	str_t *metaFileURIs[BATCH_MAX];
};

struct EFSSync {
	EFSSessionRef session;
	str_t *syncID;

	bool stop;

	async_mutex_t mutex[1];
	async_cond_t cond[1];
	struct queues *cur;
	struct queues queues[2];

	// TODO: A separate queue for resources that actually need to be downloaded?
};


int EFSSyncCreate(EFSSessionRef const session, strarg_t const syncID, EFSSyncRef *const out) {
	assert(out);
	if(!syncID) return UV_EINVAL;
	EFSSyncRef sync = calloc(1, sizeof(struct EFSSync));
	if(!sync) return UV_ENOMEM;

	sync->session = session;

	syncID = strdup(syncID);
	if(!syncID) {
		EFSSyncFree(&sync);
		return UV_ENOMEM;
	}

	async_mutex_init(sync->mutex, 0);
	async_cond_init(sync->cond, ASYNC_CANCELABLE);
	sync->cur = &sync->queues[0];

	int rc = async_spawn(STACK_DEFAULT, db_thread, sync);
	if(rc < 0) {
		EFSSyncFree(&sync);
		return rc;
	}

	*out = sync;
	return 0;
}
void EFSSyncFree(EFSSyncRef *const syncptr) {
	assert(syncptr);
	EFSSyncRef sync = *syncptr;
	if(!sync) return;

	sync->stop = true;
	// TODO: Join db_thread

	sync->session = NULL;
	FREE(&sync->syncID);
	async_mutex_destroy(sync->mutex);
	async_cond_destroy(sync->cond);
	sync->cur = NULL;

	// TODO: Clear queues

	assert_zeroed(sync, 1);
	FREE(syncptr); sync = NULL;
}


// We're building an enormous amount of structure for doing a fairly simple set of tasks
// I understand we want to do things efficiently by batching transactions
// But literally all the code we've written so far is just for batching, not actual work
// Plus error handling is going to be a pain
// Can't we simplify somehow?


static int db_files(EFSSyncRef const sync, struct queues *const cur, DB_txn *const txn) {
	if(!cur->fileURICount) return DB_SUCCESS;

	// TODO
	// 1. Look up file ID for this URI
	// 2. If we already have it, return success
	// 3. Otherwise, queue it for fetching

	// We do NOT need to record pending downloads because the filesystem itself tracks that for us


	cur->fileURICount = 0;
	return DB_SUCCESS;
}
static int db_metafiles(EFSSyncRef const sync, struct queues *const cur, DB_txn *const txn) {

	// TODO
	// 1. If we already have the meta-file, do nothing
	// 2. If we don't have the target, record where we can get the meta-file in the future
	// 3. Otherwise, enqueue the meta-file for fetching

}
static int db_submissions(EFSSyncRef const sync, struct queues *const cur, DB_txn *const txn) {

}
static int db_work(EFSSyncRef const sync, struct queues *const cur) {
	EFSRepoRef const repo = EFSSessionGetRepo(sync->session);
	DB_env *db;
	EFSRepoDBOpen(repo, &db);

	DB_txn *txn;
	rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
	if(DB_SUCCESS != rc) {
		EFSRepoDBClose(repo, &db);
		return rc;
	}

	// TODO: Process queues in cur.

	// TODO: Submissions should go to an EFSWriter that does its own batching
	// But two layers of batching is a bad idea...
	// Instead of using EFSWriter, do custom inter-sync batching without additional latency


	rc = db_txn_commit(txn); txn = NULL;
	EFSRepoDBClose(repo, &db);
	if(DB_SUCCESS != rc) return rc;
	return DB_SUCCESS;
}
static bool empty(EFSSyncRef const sync) {
	return
		0 == sync->cur->fileURICount &&
		0 == sync->cur->metaFileURICount;
}
static bool filled(EFSSyncRef const sync) {
	return
		BATCH_MAX == sync->cur->fileURICount ||
		BATCH_MAX == sync->cur->metaFileURICount;
}
static void db_thread(EFSSyncRef const sync) {
	int rc;
	struct queues *cur;
	for(;;) {
		if(sync->stop) break;

		async_mutex_lock(sync->mutex);

		// First we wait for anything to enter the queue.
		// Then we wait an additional LATENCY_MAX for
		// the queue to fill completely before processing.

		while(empty(sync)) {
			rc = async_cond_wait(sync->cond, sync->mutex);
			if(UV_ECANCELED == rc) {
				async_mutex_unlock(sync->mutex);
				return; // TODO
			}
		}

		uint64_t const future = uv_now(loop) + LATENCY_MAX;
		while(!filled(sync)) {
			rc = async_cond_timedwait(sync->cond, sync->mutex, future);
			if(UV_ETIMEDOUT == rc) break;
			if(UV_ECANCELED == rc) {
				async_mutex_unlock(sync->mutex);
				return; // TODO
			}
		}

		// Double buffering.
		cur = sync->cur;
		sync->cur = (&sync->queues[1] == sync->cur) ?
			&sync->queues[0] :
			&sync->queues[1];

		async_mutex_unlock(sync->mutex);

		for(;;) {
			rc = db_work(sync, cur);
			if(DB_SUCCESS == rc) break;
			fprintf(stderr, "Sync database error %s\n", db_strerror(rc));
			async_sleep(1000 * 5);
		}
	}

	// TODO: Thread joining
}
int EFSSyncFileAvailable(EFSSyncRef const sync, strarg_t const URI) {
	if(!sync) return 0;
	if(!URI) return UV_EINVAL;

	str_t *URICopy = strdup(URI);
	if(!URICopy) return UV_ENOMEM;

	async_mutex_lock(sync->mutex);
	while(BATCH_MAX == sync->cur->fileURICount) {
		rc = async_cond_wait(sync->cond, sync->mutex);
		// TODO
	}
	sync->cur->fileURIs[sync->cur->fileURICount++] = URICopy;
	async_cond_broadcast(sync->cond);
	async_mutex_unlock(sync->mutex);

	return 0;
}
int EFSSyncMetaFileAvailable(EFSSyncRef const sync, strarg_t const URI, strarg_t const target) {
	if(!sync) return 0;
	if(!URI) return UV_EINVAL;
	if(!target) return UV_EINVAL;

	str_t *URICopy = strdup(URI);
	str_t *targetCopy = strdup(target);
	if(!URICopy || !targetCopy) {
		FREE(&URICopy);
		FREE(&targetCopy);
		return UV_ENOMEM;
	}

	async_mutex_lock(sync->mutex);
	while(BATCH_MAX == sync->cur->metaFileURICount) {
		rc = async_cond_wait(sync->cond, sync->mutex);
		// TODO
	}
	sync->cur->metaFileURIs[sync->cur->metaFileURICount] = URICopy;
	sync->cur->metaFileTargets[sync->cur->metaFileURICount] = targetCopy;
	sync->cur->metaFileURICount++;
	async_cond_broadcast(sync->cond);
	async_mutex_unlock(sync->mutex);

	return 0;

}






















