#include <assert.h>
#include <stdlib.h>
#include "async.h"

#define WORKER_COUNT 4

struct async_pool_s {
	async_worker_t *workers[WORKER_COUNT];
	count_t count;
	async_sem_t *sem;
};

static thread_local async_pool_t *shared = NULL;
static thread_local async_worker_t *worker = NULL;
static thread_local unsigned depth = 0;

async_pool_t *async_pool_get_shared(void) {
	assert(!worker);
	assert(0 == depth);
	if(!shared) shared = async_pool_create();
	return shared;
}

async_pool_t *async_pool_create(void) {
	async_pool_t *const pool = calloc(1, sizeof(struct async_pool_s));
	if(!pool) return NULL;
	for(index_t i = 0; i < WORKER_COUNT; ++i) {
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
	assert(WORKER_COUNT == pool->count)
	for(index_t i = 0; i < WORKER_COUNT; ++i) {
		async_worker_free(pool->workers[i]); pool->workers[i] = NULL;
	}
	async_sem_free(pool->sem); pool->sem = NULL;
	free(pool);
}

void async_pool_enter(async_pool_t *const pool) {
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
	worker = w;
	depth++;
	assert(1 == depth);
}
void async_pool_leave(async_pool_t *const pool) {
	assert(depth > 0);
	if(--depth > 0) return;
	async_worker_t *const w = worker;
	assert(w);
	async_worker_leave(w);
	assert(pool->count < WORKER_COUNT);
	pool->workers[pool->count++] = w;
	if(1 == pool->count) async_sem_post(pool->sem);
}

