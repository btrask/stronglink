
typedef struct EFSPush* EFSPushRef;

struct EFSPush {
	EFSRepoRef repo;
	uint64_t sequence;
	
};

EFSSubmissionRef EFSPushWait(EFSPushRef const push) {
	assert(push);
	EFSRepoRef const repo = push->repo;
	// messing with internal repo state...
}


