#include <assert.h>
#include <stdlib.h>
#include "async.h"

#define WORKER_COUNT 16
// 4 threads (libuv thread pool default) isn't enough for our I/O, at least
// on my SSD.
// Having more threads than CPUs also lets the OS scheduler do its thing, which
// contrary to conventional wisdom I think is a good thing.
// Our thread pool is (at least theoretically) more efficient than libuv's
// because it never locks (AKA blocks) the main thread.

struct async_pool_s {
	async_worker_t *workers[WORKER_COUNT];
	unsigned count;
	async_sem_t sem[1];
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
	async_sem_init(pool->sem, 1, ASYNC_CANCELABLE);
	return pool;
}
void async_pool_free(async_pool_t *const pool) {
	if(!pool) return;
	assert(WORKER_COUNT == pool->count);
	for(unsigned i = 0; i < WORKER_COUNT; ++i) {
		async_worker_free(pool->workers[i]); pool->workers[i] = NULL;
	}
	async_sem_destroy(pool->sem);
	free(pool);
}

int async_pool_enter(async_pool_t *const p) {
	async_pool_t *const pool = p ? p : async_pool_get_shared();
	assert(pool);
	if(worker) {
		assert(depth > 0);
		depth++;
		return 0;
	}
	int rc = async_sem_wait(pool->sem);
	if(rc < 0) return rc;
	assert(pool->count > 0);
	async_worker_t *const w = pool->workers[--pool->count];
	pool->workers[pool->count] = NULL;
	if(pool->count > 0) async_sem_post(pool->sem);
	async_worker_enter(w);
	shared = pool;
	worker = w;
	depth++;
	assert(1 == depth);
	return 0;
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

