#include <assert.h>
#include <stdlib.h>
#include "async.h"

typedef struct list_entry list_entry;
struct list_entry {
	cothread_t thread;
	list_entry *next;
};

struct async_mutex_s {
	list_entry active;
	list_entry *tail;
	int depth;
};

async_mutex_t *async_mutex_create(void) {
	async_mutex_t *mutex = calloc(1, sizeof(struct async_mutex_s));
	if(!mutex) return NULL;
	mutex->tail = &mutex->active;
	return mutex;
}
void async_mutex_free(async_mutex_t *const mutex) {
	if(!mutex) return;
	assert(
		!mutex->active.thread &&
		!mutex->active.next &&
		!mutex->depth &&
		"Mutex freed while held");
	free(mutex);
}
void async_mutex_lock(async_mutex_t *const mutex) {
	assert(mutex && "Mutex must not be null");
	cothread_t const thread = co_active();
	if(!mutex->active.thread) {
		mutex->active.thread = thread;
		mutex->depth = 1;
	} else if(thread == mutex->active.thread) {
		mutex->depth++;
	} else {
		assert(mutex->tail && "Mutex has no tail");
		assert(mutex->tail->thread && "Mutex tail has no thread");
		list_entry us = {
			.thread = thread,
			.next = NULL,
		};
		mutex->tail->next = &us;
		mutex->tail = &us;
		async_yield();
		assert(thread == mutex->active.thread && "Mutex wrong thread obtained lock");
	}
}
int async_mutex_trylock(async_mutex_t *const mutex) {
	assert(mutex && "Mutex must not be null");
	cothread_t const thread = co_active();
	if(mutex->active.thread && thread != mutex->active.thread) return -1;
	async_mutex_lock(mutex);
	return 0;
}
void async_mutex_unlock(async_mutex_t *const mutex) {
	assert(mutex && "Mutex must not be null");
	cothread_t const thread = co_active();
	assert(mutex->active.thread && "Leaving empty mutex");
	assert(thread == mutex->active.thread && "Leaving someone else's mutex");
	assert(mutex->depth > 0 && "Mutex recursion depth going negative");
	if(--mutex->depth) return;
	mutex->active.thread = NULL;
	if(!mutex->active.next) return;
	list_entry *const next = mutex->active.next;
	if(!next->next) mutex->tail = &mutex->active;
	mutex->active.thread = next->thread;
	mutex->active.next = next->next;
	mutex->depth = 1;
	async_wakeup(next->thread);
	// Set everything up ahead of time so we aren't dependent on whether the wakeup is synchronous or not.
}
int async_mutex_check(async_mutex_t *const mutex) {
	assert(mutex && "Mutex must not be null");
	return co_active() == mutex->active.thread;
}

