


struct EFSRepo {



	async_mutex_t *sub_mutex;
	async_cond_t *sub_cond;
	uint64_t sub_latest;
};

void EFSRepoSubmissionEmit(EFSRepoRef const repo, uint64_t const sortID) {
	assert(repo);
	async_mutex_lock(repo->sub_mutex);
	assert(sortID > repo->sub_latest);
	repo->sub_latest = sortID;
	async_cond_broadcast(repo->sub_cond);
	async_mutex_unlock(repo->sub_mutex);
}
bool_t EFSRepoSubmissionWait(EFSRepoRef const repo, uint64_t const sortID, uint64_t const future) {
	assert(repo);
	async_mutex_lock(repo->sub_mutex);
	while(repo->sub_latest <= sortID && async_cond_timedwait(repo->sub_cond, repo->sub_mutex, future) < 0);
	bool_t const res = repo->sub_latest > sortID;
	async_mutex_unlock(repo->sub_mutex);
	return res;
}

