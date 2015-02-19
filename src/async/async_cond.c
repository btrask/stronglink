#include <assert.h>
#include <stdlib.h>
#include "async.h"

#define UNUSED(x) ((void)(x))

// https://en.wikipedia.org/wiki/Monitor_%28synchronization%29
// https://en.wikipedia.org/w/index.php?title=Monitor_%28synchronization%29&oldid=622007170
// Monitor (synchronization)

struct async_cond_s {
	unsigned numWaiters;
	async_sem_t sem[1];
	async_mutex_t internalMutex[1];
};

async_cond_t *async_cond_create(void) {
	async_cond_t *const cond = calloc(1, sizeof(struct async_cond_s));
	if(!cond) return NULL;
	cond->numWaiters = 0;
	async_sem_init(cond->sem, 0, 0);
	async_mutex_init(cond->internalMutex, 0);
	return cond;
}
void async_cond_free(async_cond_t *const cond) {
	if(!cond) return;
	assert(0 == cond->numWaiters);
	async_sem_destroy(cond->sem);
	async_mutex_destroy(cond->internalMutex);
	free(cond);
}
void async_cond_signal(async_cond_t *const cond) {
	async_mutex_lock(cond->internalMutex);
	if(cond->numWaiters > 0) {
		cond->numWaiters--;
		async_sem_post(cond->sem);
	}
	async_mutex_unlock(cond->internalMutex);
}
void async_cond_broadcast(async_cond_t *const cond) {
	async_mutex_lock(cond->internalMutex);
	while(cond->numWaiters > 0) {
		cond->numWaiters--;
		async_sem_post(cond->sem);
	}
	async_mutex_unlock(cond->internalMutex);
}
void async_cond_wait(async_cond_t *const cond, async_mutex_t *const mutex) {
	int rc = async_cond_timedwait(cond, mutex, UINT64_MAX);
	assert(rc >= 0);
	UNUSED(rc);
}
int async_cond_timedwait(async_cond_t *const cond, async_mutex_t *const mutex, uint64_t const future) {
	assert(async_mutex_check(mutex));

	async_mutex_lock(cond->internalMutex);
	cond->numWaiters++;
	async_mutex_unlock(mutex);
	async_mutex_unlock(cond->internalMutex);

	int const rc = async_sem_timedwait(cond->sem, future);

	async_mutex_lock(mutex);
	return rc;
}

