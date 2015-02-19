#include <assert.h>
#include <stdlib.h>
#include "async.h"

#define WORKER_COUNT 4
// Having a large number of worker threads (significantly more than the number
// of CPU cores) should help the OS scheduler account for considerable
// variability in each job (e.g. CPU- vs IO-bound, long vs short). A system like
// libuv (Node.js) can get away with ~1 thread per core because all of the jobs
// are relatively uniform, because they are as small as possible. However, I
// believe the overhead of using such small jobs negates the benefit of lower
// scheduling overhead.
// Also based on whether we're using libuv's thread pool.

struct async_pool_s {
	async_worker_t *workers[WORKER_COUNT];
	unsigned count;
	async_sem_t *sem;
};

static thread_local async_pool_t *shared = NULL;
static thread_local async_worker_t *worker = NULL;
static thread_local unsigned depth = 0;

async_pool_t *async_pool_get_shared(void) {
	if(!shared) shared = async_pool_create();
	return shared;
}

async_pool_t *async_pool_create(void) {
	async_pool_t *const pool = calloc(1, sizeof(struct async_pool_s));
	if(!pool) return NULL;
	for(unsigned i = 0; i < WORKER_COUNT; ++i) {
		pool->workers[i] = async_worker_create();
		if(!pool->workers[i]) {
			async_pool_free(pool);
			return NULL;
		}
	}
	pool->count = WORKER_COUNT;
	pool->sem = async_sem_create(1);
	if(!pool->sem) {
		async_pool_free(pool);
		return NULL;
	}
	return pool;
}
void async_pool_free(async_pool_t *const pool) {
	if(!pool) return;
	assert(WORKER_COUNT == pool->count);
	for(unsigned i = 0; i < WORKER_COUNT; ++i) {
		async_worker_free(pool->workers[i]); pool->workers[i] = NULL;
	}
	async_sem_free(pool->sem); pool->sem = NULL;
	free(pool);
}

void async_pool_enter(async_pool_t *const p) {
	async_pool_t *const pool = p ? p : async_pool_get_shared();
	assert(pool);
	if(worker) {
		assert(depth > 0);
		depth++;
		return;
	}
	async_sem_wait(pool->sem);
	assert(pool->count > 0);
	async_worker_t *const w = pool->workers[--pool->count];
	pool->workers[pool->count] = NULL;
	if(pool->count > 0) async_sem_post(pool->sem);
	async_worker_enter(w);
	shared = pool;
	worker = w;
	depth++;
	assert(1 == depth);
}
void async_pool_leave(async_pool_t *const p) {
	async_pool_t *const pool = p ? p : async_pool_get_shared();
	assert(pool);
	assert(depth > 0);
	if(--depth > 0) return;
	async_worker_t *const w = worker;
	assert(w);
	async_worker_leave(w);
	assert(pool->count < WORKER_COUNT);
	pool->workers[pool->count++] = w;
	if(1 == pool->count) async_sem_post(pool->sem);
}

async_worker_t *async_pool_get_worker(void) {
	return worker;
}

