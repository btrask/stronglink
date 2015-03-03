

void sync(URIList *const URIs, x) {

	queue;

	strarg_t URI;
	while((URI = URIListNext(URIs))) {
		enqueue(queue, URI);
	}

}

typedef struct queue queue;
struct queue {

};
void enqueue(queue) {

}
void flush(queue) {

}


// reqs
// - double buffering (transmitting while storing)
// - cancelation
// - real-time (commit timeouts?)

// we should probably just have one dedicated commit fiber
// instead of having two separate queues, just use one 2x sized ring buffer?

#define CONNECTION_MAX 64
#define QUEUE_SIZE 64
#define QUEUE_BUFFERS 2 // Double buffer


// this is basically the same as what we were already doing...


// do we need a fiber pool abstraction layer for arbitrary jobs?
// oh, or we could just call async_thread and assume it's fast, right?
// well, uh, maybe? we're going to have to create 8000 threads for a single pull...
// on the other hand we already arguably need some caching for our server
// it would arguably result in fewer total fibers...

// should we impose a fiber limit and have async_thread block or fail?


// how does our main sync function look, and what are our pre/post-conditions?
// if we lose connection, what should happen?

// 1. we want to reconnect asap (if possible, depending on connection type)
// 2. we want to finish existing downloads (if they dont get disconnected too)
// 3. we want to write the new meta-data

// 1 and 2 are at odds, so we need to have state external to the sync function

typedef struct EFSSync *EFSSyncRef;
struct EFSSync {

};

void EFSSyncEnqueue(EFSSyncRef const sync, URIList *const URIs);

// we also want to be able to resume canceled/paused downloads later

// what if one sync structure could be used by several connections combined?
// the amazing sync engine~


// i think we have to keep each client separate
// modularity, that's how the world works

// also, instead of our sync function directly calling polymorphic download functions
// take our existing pull system and gut the download function there
// we can even reuse the main sync function if we factor our the connection creation

// well, one wrinkle is that our old sync function was spread across every fiber
// put it all on one thread...


static void read(struct readinfo const *const info) {
	EFSSyncDownload(info->sync, info->URI);
}
void EFSSyncRead(EFSSyncRef const sync, URIList *const URIs) {


	for(;;) {


		str_t URI[URI_MAX];
		if(URIListNext(URIs, URI) < 0) break;


		struct {} readinfo = {};
		async_thread(STACK_DEFAULT, read, &readinfo);



	}


}
void EFSSyncDownload(EFSSyncRef const sync, strarg_t const URI) {


	if(EFSSessionGetFileInfo(pull->session, URI, NULL) >= 0) goto enqueue;


	str_t algo[EFS_ALGO_SIZE];
	str_t hash[EFS_HASH_SIZE];
	if(!EFSParseURI(URI, algo, hash)) goto enqueue;



	str_t *tmp = async_tmpnam();







	// 1. check if we already have the uri
	// 2. use a callback to download it under a given filename?
	// 3. if it succeeds, rename and enqueue
	// 4. wake up writer thread if the queue is full

	// if we fail, then what? if they fail?
	// should the callback be passed a filename or an fd or something else?
	// we have to record the pending download somehow, so we can resume it later
	// we have to be able to tell the callback to cancel (another callback?)
	// can and should we have a shared writer for all sync objects?


	// database schema?
	// 1. only download a given hash once at a time
	// 2. after it's received, verify it
	// 3. if it matches the hash from another source, consider that req fulfilled
	// 4. if it doesnt match or we cant verify it, download from the other source too

	// something like that?
	// would it be better to just have each source completely independent?
	// thundering herds...
	// well we should know up front whether we have the algorithm for a hash or not

	// so we need a global download manager that prevents duplicates
	// and coordinates files from multiple sources, right?

	// btw, if we download a file and it doesnt match its hash, throw it away and download it again (from another source if possible)
	// eventually disable the pull????


	// should tracking of pending transfers be done outside of the sync system?
	// not if it would apply to every type of transfer, right?

}


static void EFSSyncRegisterDownload(EFSSyncRef const sync, strarg_t const URI, ) {
	EFSRepoRef const repo = EFSSessionGetRepo(sync->session);
	EFSConnection const *conn = EFSRepoDBOpen(repo);
	int rc;
	DB_txn *txn = NULL;
	rc = db_txn_begin(conn->env, NULL, DB_RDWR, &txn);
	if(DB_SUCCESS != rc) {
		EFSRepoDBClose(repo, &conn);
		goto bail;
	}

	DB_VAL(download_key, DB_VARINT_MAX * 2 + asdf);
	db_bind_uint64(download_key, EFSDownloadByID);




	rc = db_txn_commit(txn);
	if(DB_SUCCESS != rc) {
	
	}
	EFSRepoDBClose(repo, &conn);
}



// if there is already a pending download, we should just wait for it to finish?
// if there is a canceled download, we should return the existing file path?
// otherwise, we should return a new file path?
// EFSSyncPathForDownload ?

// well, pending downloads should be stored in memory

// well, no
// first, storing them in memory is a pain to search efficiently
// second, if we crash and restart, we should restart the pending downloads too
// right? sort of?
// we cant simply restart pushes...

// thus we do in fact have to track active downloads in memory
// but its still probably fine to use an unsorted array and linear search

// so what operations do we have?
// 1. start/resume download
// 2. check for existing paused download

// uh...?
// 1. check if there is an active download, and if so, just wait for it
// 2. check if there is a paused download, and if so, resume it
// 3. store info and start a new download


// above list looks really good
// what info do we need to store?


// well, if a download finishes but fails to verify
// we should rotate through the different possible sources for the file?

// even better we could partition the file and download chunks from each source...
// but this isn't exactly bittorrent here...

// okay, so after a download finishes
// we store whether it verified successfully or not
// and then wake up the other threads
// if it succeeded, the other threads are happy
// if not, another thread gets to try

// it would be nice to be able to sort the threads...

// i think we can if we record each redundant file in a download slot
// or sub-slot thing...
// point is when each download starts, we can register it in the list
// then if the download fails, pick a specific one and use the condition lock to wake it

// we could even terminate the fiber and just record who to notify later...
// or dont even spin off a fiber until we know we need it...



struct download_target {
	str_t *URI;
	EFSSubmission *submission;
	unsigned refcount;
};


static async_mutex_t *active_downloads_lock;
static download_target active_downloads[64];


void EFSSyncEnqueueDownload(EFSSyncRef const sync, strarg_t const URI) {
	lock(active_downloads_lock);
	if(active_download(URI)) {
		// have the download also notify us when it's done... linked list?
	} else {
		// add us as downloader
		// spawn fiber
		// note: we dont actually care whether we end up resuming the download or starting from scratch, and we want to perform that database lookup on a separate fiber
	}
	unlock(active_downloads_lock);
}
static void download(struct download_info const *const info) {

	// check database, resume if possible
	// otherwise write new download info

	// it really sucks that our download meta-data requires serialized write transactions...
	// it would be much nicer if we could operate purely within the file system...
	// or if we could batch the writes to the database...
	// or maybe we could check the expected file-size and only handle resuming if it's large?

	// all of these write transactions are going to interfere with our main writer thread


	// well, if we have a long download, that's going to block the writer thread anyway
	// but i still don't really like the idea...


}



// so if we start an unverifyable download, we don't want to accidentally resume it from another source
// it'd be easiest to just keep each source separate...
// but on the other hand some sources dont have unique identifiers either

// but without a unique id and a verifyable hash, we cant resume the transfer at all
// which sucks for clients doing large pushes...

// just associate a unique idea with the push client-side
// then keep every source separate?

// okay, assume that each client is uniquely identifiable

// then what?

// for hashes we recognize up front, dedupe them between sources
// for hashes we don't, unique them by unique source id

// even if an algo gets removed, we wont resume from a trusted file
// because the temp path will change

// if the client doesnt provide a unique id, we can generate one
// basically the sync api should require it



EFSSyncRef EFSSyncCreate(strarg_t const sourceID);
void EFSSyncRun(EFSSyncRef const sync, URIList *const URIs);


// /tmp/pull_localhost:8009/sha256/asdf
// like that?
// have to be sure to escape strings, but that should be okay... (uri encode)

// /tmp/verify/sha256/asdf
// hmm?


// we don't want to leave this structure of empty directories, so i think it's okay to use hashes for file names


int EFSHasherCanVerify(strarg_t const algo);

// uh... so for resuming downloads between different sources
// what about non-cryptographic hashes?
// doesnt work yo







// so how do we handle modular transfers?

typedef err_t (*EFSSyncCB)(void *ctx, EFSSyncRef const sync, strarg_t const URI, strarg_t const path);

// something like that?



#define QUEUE_SIZE 64
#define URI_MAX 1023

struct dlinfo {
	str_t URI[URI_MAX+1];
};

struct EFSSync {
	str_t *uniqueID;
	EFSSyncCB download_cb;
	void *download_ctx;

	struct dlinfo queue[QUEUE_SIZE];
};

void EFSSyncRun(EFSSyncRef const sync, URIList *const URIs) {

// separate fiber?
// queue up a bunch of uris, then query the db for them?
// need some sort of timeout on the read

// really we need a generalized way to cancel an http read WITHOUT killing the entire connection

}

// actually for the callback they're calling us
// since its their fiber (for the underlying connection)

// if we dont have anything immediate to download, what should we do?

// it would be nice to dynamically scale back the number of connections


typedef struct {
	str_t *URI;
	EFSSubmissionRef sub;
} EFSSyncTask;

EFSSyncTask *EFSSyncGetTask(EFSSyncRef const sync);
void EFSSyncTaskDone(EFSSyncRef const sync, EFSSyncTask *const task);

// buuuuuuuuuuut
// we need to handle fucking cancelation and timeouts
// e.g. for timeouts, the client might need to be pinged every 30 seconds (long polling)
// or if the client disconnects, we need to cancel the read

// the obvious way to do that is using multiple fibers
// then we dont have to worry about timeouts, only cancelation


// and we dont even have to cancel http reads in this case...?
// we just have to trigger the condition lock the sync task is waiting on?


// alternately the interval could be kept by a timer not attached to a fiber?



// ....
// okay, so with the documented design
// wrinkles:
// - the client unique id is "password equivalent" and must be compared/looked up in constant time
// - we can have any number of concurrent syncs, so our current fixed size session cache isnt good enough, and we cant reuse that (simple) design
// - even with the client id, the connection must still be authenticated (and we should probably do that first...)



// first up: a proper read() function in HTTPMessage
// simple semantics, cancelation support
// allocates its own buffers (as late as possible using alloc_cb)





















