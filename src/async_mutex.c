#include <assert.h>
#include <stdlib.h>
#include "async.h"

struct async_mutex_s {
	unsigned size;
	cothread_t *queue;
	unsigned depth;
	unsigned cur;
	unsigned count;
};

async_mutex_t *async_mutex_create(void) {
	async_mutex_t *const mutex = calloc(1, sizeof(struct async_mutex_s));
	if(!mutex) return NULL;
	mutex->cur = 0;
	mutex->count = 0;
	mutex->size = 10;
	mutex->depth = 0;
	mutex->queue = calloc(mutex->size, sizeof(cothread_t));
	if(!mutex->queue) {
		async_mutex_free(mutex);
		return NULL;
	}
	return mutex;
}
void async_mutex_free(async_mutex_t *const mutex) {
	if(!mutex) return;
	assert(!mutex->count && "Mutex freed while held");
	free(mutex);
}
void async_mutex_lock(async_mutex_t *const mutex) {
	assert(mutex && "Mutex must not be null");
	cothread_t const thread = co_active();
	if(mutex->count && thread == mutex->queue[mutex->cur]) {
		++mutex->depth;
		return;
	}
	if(++mutex->count > mutex->size) {
		assert(0 && "Mutex queue growth not yet implemented");
		// TODO: Grow.
	}
	mutex->queue[(mutex->cur + mutex->count - 1) % mutex->size] = thread;
	if(mutex->count > 1) co_switch(yield);
	assert(thread == mutex->queue[mutex->cur] && "Wrong thread acquired lock");
	assert(!mutex->depth && "Acquired lock in invalid state");
	mutex->depth = 1;
}
int async_mutex_trylock(async_mutex_t *const mutex) {
	assert(mutex && "Mutex must not be null");
	if(mutex->count && co_active() != mutex->queue[mutex->cur]) return -1;
	async_mutex_lock(mutex);
	return 0;
}
void async_mutex_unlock(async_mutex_t *const mutex) {
	assert(mutex && "Mutex must not be null");
	cothread_t const thread = co_active();
	assert(mutex->count && "Leaving empty mutex");
	assert(thread == mutex->queue[mutex->cur] && "Leaving someone else's mutex");
	if(--mutex->depth) return;
	mutex->cur = (mutex->cur + 1) % mutex->size;
	if(!--mutex->count) return;
	async_wakeup(mutex->queue[mutex->cur]);
}
int async_mutex_check(async_mutex_t *const mutex) {
	assert(mutex && "Mutex must not be null");
	return mutex->count && co_active() == mutex->queue[mutex->cur];
}

