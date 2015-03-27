






struct EFSSync {
	
};


static void read(void *ptr) {
	EFSSyncRef const sync = ptr;




}
static void check(void *ptr) {
	EFSSyncRef const sync = ptr;





	EFSRepoRef const repo = EFSSessionGetRepo(sync->session);
	EFSConnection *dbconn = EFSRepoDBOpen(repo);
	int rc;
	DB_txn *txn = NULL;
	rc = db_txn_open(dbconn->env, NULL, DB_RDONLY, &txn);
	if(DB_SUCCESS != rc) {
	
	}


	for(size_t i = 0; i < count; ++i) {



		DB_RANGE(fileIDs, DB_VARINT_MAX + DB_INLINE_MAX);
		db_bind_uint64(fileIDs->min, EFSURIAndFileID);
		db_bind_string(txn, fileIDs->min, URIs[i]);
		db_range_genmax(fileIDs);
		DB_val URIAndFileID_key[1];
		rc = db_cursor_firstr(cursor, fileIDs, URIAndFileID_key, NULL, +1);
		DB_val file_val[1];
		if(DB_SUCCESS == rc) {
			uint64_t const table = db_read_uint64(URIAndFileID_key);
			assert(EFSURIAndFileID == table);
			strarg_t const URI2 = db_read_string(txn, URIAndFileID_key);
			assert(0 == strcmp(URI, URI2));
			uint64_t const fileID = db_read_uint64(URIAndFileID_key);
			UNUSED(fileID);
		} else {
			// TODO: Check if the URI has been "seen" from this remote before...
			// Enqueue item for real
		}



	}


	db_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(repo, &dbconn);


}



// I kind of feel like this approach is a dead end, for several reasons...

// just start smaller?





#define QUEUE_MAX 32
#define NEXT(x) ((x+1) % QUEUE_MAX)

typedef struct {
	str_t URI[URI_MAX];
	HTTPConnectionRef conn;
} transfer;
struct EFSSync {
	uint64_t syncID;
	EFSSessionRef session;
	str_t *host;
	str_t *username;
	str_t *password;
	str_t *cookie;
	str_t *query;

	HTTPConnectionRef conn;
	async_mutex_t lock[1];
	async_cond_t store[1];
	async_cond_t fetch[1];
	unsigned tasks;
	size_t r, f, s; // read >= fetch >= store
	bool stop;
	transfer queue[QUEUE_MAX][1];
}

static void read(EFSSyncRef const sync) {
	for(;;) {
		if(sync->stop) break;
		if(!sync->conn) connect(sync, &sync->conn);

		str_t URI[URI_MAX];
		int rc = HTTPConnectionReadBodyLine(sync->conn, URI, URI_MAX);
		if(rc < 0) {
			if(sync->stop) break;
			fprintf(stderr, "Sync connection error %s\n", uv_strerror(rc));
			HTTPConnectionFree(&sync->conn);
			continue;
		}

		async_mutex_lock(sync->lock);
		while(!sync->stop && NEXT(sync->r) == sync->s) async_cond_wait(sync->store, sync->lock);
		memcpy(sync->queue[sync->r++]->URI, URI, URI_MAX);
		async_cond_signal(sync->fetch);
		async_mutex_unlock(sync->lock);
	}
}
static int fetch0(EFSSyncRef const sync, strarg_t const URI, HTTPConnectionRef *const conn, EFSSubmissionRef *const out) {
	int rc;
	str_t algo[EFS_ALGO_SIZE];
	str_t hash[EFS_HASH_SIZE];
	if(!EFSParseURI(URI, algo, hash)) return UV_EINVAL;

	str_t path[URI_MAX];
	rc = snprintf(path, URI_MAX, "/efs/file/%s/%s", algo, hash);
	if(rc < 0) return rc;
	if(rc >= URI_MAX) return -1;

retry:
	EFSSubmissionFree(out); // No-op the first time...
	if(sync->stop) return UV_ECANCELLED;
	if(!*conn) connect(sync, conn);

	// TODO: Report host and URI in errors
	rc = 0;
	rc = rc < 0 ? rc : HTTPConnectionWriteRequest(*conn, HTTP_GET, path, sync->host);
	rc = rc < 0 ? rc : HTTPConnectionWriteHeader(*conn, "Cookie", sync->cookie);
	rc = rc < 0 ? rc : HTTPConnectionBeginBody(*conn);
	rc = rc < 0 ? rc : HTTPConnectionEnd(*conn);
	if(rc < 0) {
		fprintf(stderr, "Sync fetch request error %s\n", uv_strerror(rc));
		HTTPConnectionFree(conn);
		goto retry;
	}
	int const status = HTTPConnectionReadResponseStatus(*conn, NULL);
	if(status < 0) {
		fprintf(stderr, "Sync fetch response error %s\n", uv_strerror(status));
		HTTPConnectionFree(conn);
		goto retry;
	}
	if(status < 200 || status >= 300) {
		fprintf(stderr, "Sync fetch status error %d\n", status);
		goto retry;
	}

	static str_t const fields[][FIELD_MAX] = {
		"content-type",
		"content-length",
	};
	str_t headers[numberof(fields)][VALUE_MAX];
	rc = HTTPConnectionReadHeaders(*conn, headers, fields, numberof(fields), NULL);
	if(rc < 0) {
		fprintf(stderr, "Sync fetch headers error %s\n", uv_strerror(rc));
		HTTPConnectionFree(conn);
		goto retry;
	}

	*out = EFSSubmissionCreate(sync->session, headers[0]);
	if(!sub) {
		fprintf(stderr, "Sync submission error\n");
		goto retry;
	}
	for(;;) {
		if(sync->stop) goto retry;
		uv_buf_t buf[1];
		rc = HTTPConnectionReadBody(*conn, buf, NULL);
		if(rc < 0) {
			fprintf(stderr, "Sync download error %s\n", uv_strerror(rc));
			HTTPConnectionFree(conn);
			goto retry;
		}
		if(0 == buf->len) break;
		rc = EFSSubmissionWrite(sub, (byte_t *)buf->base, buf->len);
		if(rc < 0) {
			fprintf(stderr, "Sync write error %s\n", uv_strerror(rc));
			goto retry;
		}
	}
	rc = EFSSubmissionEnd(sub);
	if(rc < 0) {
		fprintf(stderr, "Sync submission error\n");
		HTTPConnectionFree(conn);
		goto retry;
	}

	return UV_OK;
}
static void fetch(EFSSyncRef const sync) {
	int rc;
	for(;;) {
		if(sync->stop) break;
		async_mutex_lock(sync->lock);
		while(async_cond_wait(sync->fetch, sync->lock) < 0);
		size_t const pos = sync->f;
		sync->f = NEXT(sync->f);
		async_mutex_unlock(sync->lock);

		strarg_t const URI = sync->queue[pos]->URI;
		HTTPConnectionRef *const conn = &sync->queue[pos]->conn;

		rc = EFSSessionGetFileInfo(sync->session, URI, NULL);
		if(rc >= 0) goto done;
		if(DB_NOTFOUND != rc) {
			fprintf(stderr, "Fetch check error %s\n", db_strerror(rc));
			abort(); // TODO
		}


		// TODO: This entire function should be modular.
		EFSSubmissionRef sub;
		rc = fetch0(sync, URI, conn, &sub);
		if(rc < 0) {
			if(sync->stop) break;
			fprintf(stderr, "Fetch download error %s\n", uv_strerror(rc));
			abort();
		}

		async_mutex_lock(sync->lock);
		while(!sync->stop && pos != sync->s) async_cond_wait(sync->store, sync->lock);
		async_mutex_unlock(sync->lock);
		if(sync->stop) break;
		EFSSubmissionBatchStore(&sub, 1);
		EFSSubmissionFree(&sub);
	done:
		async_mutex_lock(sync->lock);
		assert(pos == sync->s);
		sync->s = NEXT(sync->s);
		async_cond_broadcast(sync->store);
		async_mutex_unlock(sync->lock);
	}
}



void nextURI(void *ctx, EFSSyncRef const sync, str_t *const URI, size_t const max);
void download(void *ctx, EFSSyncRef const sync, strarg_t const URI, id???);

// TODO: The sync object cant track hostname or other info because it is transport-specific
// In fact, the sync doesn't have to pass any sort of remote-id at all, since that is all handled by the transport layer


typedef int (*EFSTransportNextURI)(void *ctx, EFSSyncRef const sync, str_t *const URI, size_t const max);
typedef int (*EFSTransportFetch)(void *ctx, EFSSyncRef const sync, strarg_t const URI, EFSSubmissionRef *const out);

// should we own the transport or should the transport own us?
// who should close who?
// who tracks the session and the repo?
// when a sync is stopped, what should happen to the transport?


// the owner is the one who has to interface with the repo object

// for pushes, having the sync owned by the transport makes sense
// but for pulls, having the transport owned by the sync makes sense
// basically either one needs to be able to close the other


// regarding stopping transports
// perhaps it depends on the reason its being stopped?
// application quitting: yes, just stop it
// some sort of management interface: yes, we should be able to kill pushes server-side

// after a transport is stopped, does the sync have to be discarded/recreated?

// in response to the transport being stopped, it should be able to remove the sync object?


// ...can we just rework the sync for cleaner layering?

// also note that pushes have flow in two directions
// fetches waking the reader, and the reader waking fetches


// it'd be nice if sync functions were just something you could call
// e.g. EFSPull:
// - create fiber and call "enqueue" in a loop
// - create workers and call dequeue/store in a loop




int EFSSyncEnqueue(EFSSyncRef const sync, strarg_t const URI);
int EFSSyncDequeue(EFSSyncRef const sync, strarg_t *const URI);
int EFSSyncStore(EFSSyncRef const sync, EFSSubmissionRef const sub);


// What if we just had a general ordered queue and used two of them...


// One problem with calling the sync from the transport is that transport threads can come and go

// Is that a problem?
// Oh, yeah, because the fiber belongs to the transport but we can block it indefinitely in the sync
// we'd need to provide a way to cancel condition locks for individual fibers when the transport's connection closed

// oh but on the other hand
// if the sync owns the fiber, then we may have to block in the transport while waiting for a connection to become available
// and if the fiber is stopped, we have to tell the transport to let us go
// although the transport is stopped when the sync is stopped anyway
// and that is global rather than fiber-specific

// but it still seems like it might be worth having the transport own the fiber
// because wrapping the sync is a lot simpler than having the sync call special dynamic functions

// also our web server is already spawning a fiber per connection
// so just using that and blocking until there's something to sync over it makes sense


// is this actually a job for subclassing?
// could/should we use objective-c here?


#define QUEUE_MAX 32
#define NEXT(x) ((x + 1) % QUEUE_MAX)

@interface EFSSync : EFSObject
{
	EFSSessionRef session;
	async_mutex_t lock[1];
	async_cond_t fetch[1];
	async_cond_t store[1];
	unsigned tasks;
	size_t r, f, s; // read, fetch, store
	str_t *URIs[URI_MAX];
}
- (id)initWithSession:(EFSSessionRef const)session;
- (EFSSessionRef)session;
- (void)start;
- (void)stop;
- (bool)stopped;
@end
@interface EFSSync (EFSTransport)
- (int)nextURI:(str_t *const)URI :(size_t const)max;
- (int)fetch:(strarg_t const)URI :(EFSSubmissionRef *const)out;
@end


// that... makes a lot of sense

@interface EFSPull : EFSSync
{
	uint64_t pullID;
	str_t *host;
	str_t *username;
	str_t *password;
	str_t *cookie;
	str_t *query;
	HTTPConnectionRef main;
	HTTPConnectionRef *connections;
}
@end
@interface EFSPush : EFSSync
{
	
}
@end







static bool push(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	// POST /efs/push/
	EFSSessionRef session = ...; // auth
	EFSSyncRef sync = EFSSyncCreate(session);
	// TODO: register under pushID
	// the push also needs to know its id
	// for 1. knowing where to save the file so it can be resumed
	// and 2. checking for previously seen uris

	for(;;) {
		str_t URI[URI_MAX];
		rc = HTTPConnectionReadBodyLine(conn, URI, URI_MAX);
		if(rc < 0) break;
		if('\0' == URI[0]) continue;
		EFSSyncFetchURI(sync, URI);
	}

	EFSSyncFree(&sync);
	return true;
}
#define PING_INTERVAL (1000 * 30)
typedef struct {
	HTTPConnectionRef conn;
	EFSSyncRef sync;
	cothread_t thread;
} ping_info;
static void ping_cb(uv_timer_t *const timer) {
	HTTPConnectionRef const conn = timer->data;
	
}
static bool pushreq() {
	EFSSessionRef session = ...;
	EFSSyncRef const sync = EFSSessionGetPush(session, pushID);

	uv_timer_t timer[1];
	uv_timer_init(loop, timer);
	uv_timer_start(timer, ping_cb, PING_INTERVAL, PING_INTERVAL);

	str_t URI[URI_MAX] = "";
	EFSSyncGetFetchURI(sync, ); // TODO: This has to be cancelable too...???

	async_close((uv_handle_t *)timer);

	if('\0' != URI[0]) {
		HTTPConnectionWriteChunk(conn, URI, strlen(URI));
		HTTPConnectionEnd(conn);
	}

	return true;
}
static bool fetch() {
	str_t algo[EFS_ALGO_MAX];
	str_t hash[EFS_HASH_MAX];
	// TODO: URI checking and parsing
	// HTTP_POST


	EFSSessionRef session = ...;
	EFSSyncRef const sync = EFSSessionGetPush(session, pushID);



	// basically just create the submission as normal
	EFSSubmissionRef sub = ...;

	EFSSyncSubmit(sync, URI, sub);

	return true;
}


EFSSyncRef EFSSessionGetPush(EFSSessionRef const session, strarg_t const pushID) {
	if(!session) return NULL;
	if(!pushID) return NULL;
	EFSPushRef const push = ...;
	EFSSessionRef const previous = EFSPushGetSession(push);
	uint64_t const newID = session->userID;
	uint64_t const oldID = previous->userID;
	if(newID != oldID) {
		fprintf(stderr, "Push session user ID mismatch\n");
		return NULL;
	}
	// TODO
}







// our http server interface might be nicer if we returned an int
// -1 for not recognized
// 0 for response complete
// 100+ as status codes, so you can just do `return 404;`
// or maybe use named values for 'response complete', 'connection detatched', 'unmatched'





// 2015-02-20
// after getting sync cancellation working (or at least built)
// now we're back to this

// the above code is pretty confusing
// so maybe we should start by defining the rest endpoints

/*

well first off even /efs/ is outdated


/slapi/ ?
lol

/slink/


/stronglink/file/hash

/stlk/


probably /stronglink/ is the best, dont be too clever

we also need a prefix for our c api...
SLK?
SLN?

/sln/ is pretty good too...

okay, that's decided
but we'll wait to switch until we've got our syncing back on track


POST /sln/push/[pushid]
GET /sln/push/[pushid]/tx
POST /sln/push/[pushid]/algo/hash


*/


bool postPush();
bool getPushTransfer();
bool postPushTransfer();






static bool postPush(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	// POST /efs/push/
	EFSSessionRef session = ...; // auth
	EFSSyncRef sync = EFSSessionCreatePush(session, pushID);
	// TODO: register under pushID
	// the push also needs to know its id
	// for 1. knowing where to save the file so it can be resumed
	// and 2. checking for previously seen uris

	// do we need to ping this connection while we're reading from it?
	// we can block while dealing with the sync object, at least in theory...?
	// well, dont worry about it for now

	for(;;) {
		str_t URI[URI_MAX];
		rc = HTTPConnectionReadBodyLine(conn, URI, URI_MAX);
		if(rc < 0) break;
		if('\0' == URI[0]) continue;
		EFSSyncFetchURI(sync, URI);
	}

	EFSSessionReleasePush(session, &sync);
	EFSSessionFree(&session);
	return true;
}
#define PING_INTERVAL (1000 * 30)
typedef struct {
	HTTPConnectionRef conn;
	EFSSyncRef sync;
	cothread_t thread;
} ping_info;
static void ping_cb(uv_timer_t *const timer) {
	HTTPConnectionRef const conn = timer->data;
	
}
static bool getPushTransfer() {
	EFSSessionRef session = ...;
	EFSSyncRef const sync = EFSSessionRetainPush(session, pushID);
	int rc;

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/uri-list; charset=utf-8");
	HTTPConnectionBeginBody(conn);

	uv_timer_t timer[1];
	uv_timer_init(loop, timer);
	uv_timer_start(timer, ping_cb, PING_INTERVAL, PING_INTERVAL);

	str_t URI[URI_MAX];
	rc = EFSSyncGetFetchURI(sync, URI, URI_MAX);
	if(rc < 0) URI[0] = '\0';

	async_close((uv_handle_t *)timer);

	if('\0' != URI[0]) {
		HTTPConnectionWriteChunk(conn, URI, strlen(URI));
	}
	HTTPConnectionEnd(conn);
	EFSSessionReleasePush(session, &sync);
	EFSSessionFree(&session);

	return true;
}
static bool postPushTransfer() {
	str_t algo[EFS_ALGO_MAX];
	str_t hash[EFS_HASH_MAX];
	// TODO: URI checking and parsing
	// HTTP_POST


	EFSSessionRef session = ...;
	EFSSyncRef const sync = EFSSessionRetainPush(session, pushID);



	// basically just create the submission as normal
	EFSSubmissionRef sub = ...;

	EFSSyncSubmit(sync, URI, sub);

	EFSSessionReleasePush(session, &sync);
	EFSSessionFree(&session);

	return true;
}





// how to coordinate destruction of sync objects?
// one approach might be locking
// but i think reference counting would actually be simpler and more useful in this case


// the session shouldn't be owning the sync
// even if we might use the session to gain access to it...


// for our streams
// we need something like HTTPConnectionLayer7Keepalive(conn, flag)
// and internally it should maintain a signed counter
// and disable it before writing and re-enable it after
// if it wasnt enabled in the first place it will go negative which is fine

// also need HTTPConnectionAbort(conn)
// just set EOF




// good idea for efficient batched database access
// use condition locks and timers/timeouts to signal from several threads to a main reader/writer

/*

importer {
	lock
	timedwait(a, 200ms)
	while not done {
		broadcast(b)
		wait(a)
	}
	unlock
}
db-reader {
	lock
	broadcast(a)
	wait(b)
	unlock
	do work
}


*/



// and we might want one writer for the entire repo

EFSWriterCreate(repo)
EFSWriterSubmit(writer, sub)

// or alternatively, we could have some sort of general purpose transaction batching?

// batch based on write/read transaction type?
// use nested transactions for rollback?

// how good of an idea is this?
// how necessary is it?

// read transactions are supposed to be light-weight
// isn't it better to solve transaction performance at the db level?

// oh, the other problem is a lot more serious
// when you commit a nested transaction, it shouldn't be possible to be lost
// but if it's just a nested transaction, than something can happen later and mess it up

// unless every fiber waits for all of the other fibers in the batch to finish
// but that's probably slower than doing nothing

// on the other hand, submission batching...?
// kind of has the same problem, but maybe not as bad


// okay basically i think we're good
// efswriter is the way to go



// we might need to switch to websocket
// so we should be sure to support transport-agnosticism there too




// another big deal
// dependencies



















