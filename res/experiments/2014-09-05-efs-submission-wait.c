
#define QUERY_TIMEOUT_MS (1000 * 30)

typedef struct thread_list thread_list;
struct thread_list {
	cothread_t thread;
	thread_list *next;
};


struct EFSRepo {


	thread_list *queries_head;
	thread_list *queries_tail;
	uv_timer_t query_timeout;
	EFSSubmissionRef sub_queue[10];
};


EFSSubmissionRef EFSRepoSubmissionWait(EFSRepoRef const repo) {
	assert(repo);
	thread_list us = {
		.thread = co_active(),
		.next = NULL,
	};
	if(repo->queries_tail) {
		repo->queries_tail.next = &us;
		repo->queries_tail = &us;
		timeout = uv_now(loop) + QUERY_TIMEOUT_MS;
	} else {
		repo->queries_head = &us;
		repo->queries_tail = &us;
		timeout = 0;
		uv_timer_start(&repo->query_timeout, timeout_cb, QUERY_TIMEOUT_MS, 0);
	}
	async_yield();
	EFSSubmissionRef sub = NULL;
	if(repo->sub_queue[x]) sub = repo->sub_queue[x];
	if(!sub && uv_now(loop) < timeout) {
		uv_timer_start(&repo->query_timeout, timeout_cb, timeout - uv_now(loop), 0);
		async_yield();
		if(repo->sub_queue[x]) sub = repo->sub_queue[x];
	}
	assert(&us == repo->queries_head);
	repo->queries_head = us.next;
	if(&us == repo->queries_tail) repo->queries_tail = NULL;
	if(us.next) async_wakeup(us.next);
	else repo->x++;
	return sub;
}






