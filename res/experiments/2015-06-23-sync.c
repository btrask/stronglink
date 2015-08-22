
// gotta be simple
// gotta be fast

// in theory dependencies could be pretty complicated
// since you can embed a thousand files and it can take ages to sync
// we could even store the dependency list in the database
// to be robust across restarts or whatever while pulling a huge list

// instead we could keep it in memory and cap it to like 100 files
// if we crash in the middle, we presumably reload the meta-file and start over
// of course any dependencies that have been committed can be skipped

// that means we're not using nested transactions
// dependencies might be committed without the main file being committed
// that seems okay
// as long as we guarantee ordering


// we need to be able to create a submission with a known hash
// and have the submission do something intelligent if it doesn't verify
// useful for PUT requests as well as verifying pulls

// also lets us be faster? conditional PUTs?


// okay, i just added support for "known URIs" during submission


// well it's been a very productive day so far
// except for rewriting the sync system...



// okay
// keep it simple

// remember, the sync algorithm doesnt deal in uri lists or http connections
// it just takes a list of uris and issues commands to pull them


// maybe we need a submissionbatch as a first-class object...


// we need sync-ids somewhere...
// part of the pull and each submission?

// if we create a sub with the same known URI and sync id
// and there's a temp file for it that already exists
// we should be able to resume it...

// of course we have to be careful that we don't resume a file thats already
// in progress
// especially since we dont hash the data after its already been written

// to resume a file, do we have to rehash it?
// thats starting to seem like quite a pain


// okay so forget about sync-ids for now


#define CONNECTION_MAX 16

struct SLNPull {
	uint64_t pullID;
	SLNSyncRef sync;
	strarg_t host;
	strarg_t session;
	strarg_t query;
	strarg_t lang;
	bool stop;
	async_mutex_t conn_lock[1];
	size_t conn_count;
	HTTPConnectionRef conn_list[CONNECTION_MAX];
};

static void reconnect(SLNPullRef const pull, size_t const x) {
	
}
static void pull_files(SLNPullRef const pull) {
	async_mutex_lock(pull->conn_lock);
	size_t const x = pull->conn_count++;
	async_mutex_unlock(pull->conn_lock);
	for(;;) {
		if(stop) break;
		str_t URI[SLN_URI_MAX];
		int rc = HTTPConnectionReadBodyLine(pull->conn_list[x], URI, sizeof(URI));
		if(rc < 0) { reconnect(pull, x); continue; }
		SLNSyncFileURIAvailable(pull->sync, URI);
	}
}
static void pull_metafiles(SLNPullRef const pull) {
	for(;;) {
		str_t line[1023+1];
		int rc = HTTPConnectionReadLine();
		if(rc < 0) { reconnect(pull, x); continue; }
		str_t URI[SLN_URI_MAX];
		str_t target[SLN_URI_MAX];
		sscanf(line, "%s -> %s%n", URI, target, &len);
		SLNSyncMetaFileURIAvailable(pull->sync, URI, target);
	}
}
static void pull_fetch(SLNPullRef const pull) {
	for(;;) {
		strarg_t const URI = SLNSyncFetchableURI(pull->sync);
		SLNSubmissionRef sub = ...;
		HTTPConnectionWriteRequest(conn, );
		// ...
		SLNSubmissionEnd(sub);
		SLNSyncSubmissionReady(sync, sub);
	}
}


// how can we turn this code GOOD?

// figure out how to coordinate threads without unnecessary mutexes or slot IDs
// try to eliminate redundant error handling, somehow

// it should be 1. read line, 2. tell sync obj



// other than that i guess it's good?
// i guess each version has slightly different connection logic, sigh...


#define SWAP(x, y) do { \
	__typeof__(x) const __x = *(x); \
	*(x) = *(y); \
	*(y) = __x; \
} while(0);

struct xfer {
	str_t *URI;
	SLNSubmissionRef sub;
};
struct SLNSync {
	SLNSessionRef session;
	struct xfer *front;
	struct xfer *back;
	struct xfer *split;
};


// okay so we're merrily syncing along
// then all the sudden we get a huge list of dependencies
// what do we do?

// the idea of splitting the queue is good
// but it doesn't help if the list of dependencies is long
// or worse, what if many currently pending subs have lots of dependencies?

// plus we don't parse the dependencies until its basically too late
// during the transaction where its getting added


// approach
// nested transactions
// when we hit some missing depenencies
// record them into the sub and abort
// then the sync can commit the outer transaction
// and start transferring the listed deps

// so much for single pass parsing....


// what now?


// actually, is it sort of good?
// because we need to be in a transaction to check whether dependencies are satisfied


// okay, so
// when we go to add a meta-file
// first, if its target doesn't exist, we add it as a "meta-file future"
// that means checking the target before doing any basic submission stuff

// also note that meta-file futures should be ordered for a given target
// so when the target finally appears, we can load the meta-files in order


// then when we're adding it for real
// we check the dependencies as we add them
// keep a list of unmet dependencies
// and as soon as we've checked them all, if there are any umet dependencies abort

// that sub txn gets rolled back
// any submissions up to that point get committed

// then we have the sub that failed and its unmet dependency list


// do we want to handle deps recursively?
// frankly i'm not sure it adds any complexity



// okay, so we have a shared sub writer for the whole repo
// note that each sub under this writer is independent...
// wait, that doesn't work


// okay, so forget optimizations between syncs



struct SLNSync {
	SLNSessionRef session;
	struct xfer *front;
	struct xfer *back;
	struct xfer *deps;
};

// front and back are double buffered together
// deps is "interrupt priority" and unordered


// was just thinking about how to deal with parse errors in meta-files
// i know people want strict conformance checks but like
// didnt you guys learn anything from xhtml?
// the concept of validation is bunk
// every possible input should have a well-defined result



// note that the above design is non-hierarchical
// it doesn't support recursive dependencies
// because theres only one interrupt layer

// dammit
// recursive dependencies are actually necessary
// because if you just list them all at the top level
// then full ordering cant be guaranteed

// in theory the order between siblings is not fixed
// so we could still commit those out of order for performance
// if the complexity isnt too high...


// basically we need a tree, not a queue

// pruning design
// 1. not all of the tree is loaded at once
// 2. we can unload portions of the tree up to the roots

// shall we just forget about that for now?
// resounding yes



// if we support more state in our submission objects
// then we dont need as much external management

// - getter for known uri
// - getter for completion status
// - directly store the list of dependent submissions


// then for now, we can just recursively store all deps in one big transaction
// although later we'll want to split it


// at this point i guess its mainly hinging on what json library we want to use
// once the file is uploaded/written
// mmap and parse
// extract target and deps
// begin transaction and check them

// although we cant do anything with the target up front
// its a read-only transaction

// anyway then we create new sub-subs for any missing deps

// then the client is responsible for handling any unresolved dependencies
// if you try to store the sub with the deps unloaded, it errors
// obviously the sync will check them and load them
// but if the user uploads a dep with unmet deps directly
// its just an error and it wont be accepted




// okay great
// not only do we have dependencies that must be loaded first
// we also have dependencies that must be loaded after
// when we load a file that a meta-file was blocked on, we should load the meta-file too
// but only if they're both from the same origin (otherwise it leaks info)

// we also need to check whether the target exists before the dep check
// since if it doesn't exist, the meta-file should be delayed, and all of the deps are moot

// basically, this is way to complicated






// okay, solution
// cut dependency resolution for now

// in which case we just need to rewrite the sync system
// and add a check while parsing meta-files

// if the meta-file doesn't have a target
// we cant assign it a file id
// but instead we want to add it to a list of meta-files to fetch later

// then while pulling files
// we want to check if there are any meta-files for the current file
// and if so pull them too, immediately after


// sounds... about a thousand times easier than before
// and maybe even easy enough to build









