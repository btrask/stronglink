// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdlib.h>
#include "async.h"

// https://en.wikipedia.org/wiki/Monitor_%28synchronization%29
// https://en.wikipedia.org/w/index.php?title=Monitor_%28synchronization%29&oldid=622007170
// Monitor (synchronization)

void async_cond_init(async_cond_t *const cond, unsigned const flags) {
	assert(cond);
	cond->numWaiters = 0;
	async_sem_init(cond->sem, 0, flags);
	async_mutex_init(cond->internalMutex, 0);
}
void async_cond_destroy(async_cond_t *const cond) {
	if(!cond) return;
	assert(0 == cond->numWaiters);
	async_sem_destroy(cond->sem);
	async_mutex_destroy(cond->internalMutex);
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
int async_cond_wait(async_cond_t *const cond, async_mutex_t *const mutex) {
	return async_cond_timedwait(cond, mutex, UINT64_MAX);
}
int async_cond_timedwait(async_cond_t *const cond, async_mutex_t *const mutex, uint64_t const future) {
	assert(async_mutex_check(mutex));

	async_mutex_lock(cond->internalMutex);
	cond->numWaiters++;
	async_mutex_unlock(mutex);
	async_mutex_unlock(cond->internalMutex);

	int rc = async_sem_timedwait(cond->sem, future);

	async_mutex_lock(mutex);
	return rc;
}

