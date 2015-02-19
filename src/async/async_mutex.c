#include <assert.h>
#include <stdlib.h>
#include "async.h"

struct async_mutex_s {
	async_sem_t *sem;
	async_t *active;
	int depth;
};

async_mutex_t *async_mutex_create(void) {
	async_mutex_t *mutex = calloc(1, sizeof(struct async_mutex_s));
	if(!mutex) return NULL;
	mutex->sem = async_sem_create(1);
	mutex->active = NULL;
	mutex->depth = 0;
	if(!mutex->sem) {
		async_mutex_free(mutex);
		return NULL;
	}
	return mutex;
}
void async_mutex_free(async_mutex_t *const mutex) {
	if(!mutex) return;
	assert(!mutex->active);
	assert(0 == mutex->depth);
	async_sem_free(mutex->sem); mutex->sem = NULL;
	free(mutex);
}
void async_mutex_lock(async_mutex_t *const mutex) {
	assert(mutex);
	async_t *const thread = async_active();
	if(thread != mutex->active) {
		async_sem_wait(mutex->sem);
		mutex->active = thread;
		mutex->depth = 1;
	} else {
		++mutex->depth;
	}
}
int async_mutex_trylock(async_mutex_t *const mutex) {
	assert(mutex);
	async_t *const thread = async_active();
	if(thread != mutex->active) {
		if(async_sem_trywait(mutex->sem) < 0) return -1;
		mutex->active = async_active();
		mutex->depth = 1;
	} else {
		++mutex->depth;
	}
	return 0;
}
void async_mutex_unlock(async_mutex_t *const mutex) {
	assert(mutex);
	async_t *const thread = async_active();
	assert(thread == mutex->active && "Leaving someone else's mutex");
	assert(mutex->depth > 0 && "Mutex recursion depth going negative");
	if(--mutex->depth) return;
	mutex->active = NULL;
	async_sem_post(mutex->sem);
}
int async_mutex_check(async_mutex_t *const mutex) {
	assert(mutex);
	return async_active() == mutex->active;
}

