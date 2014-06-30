#include <assert.h>
#include <stdlib.h>
#include "async.h"

#define READERS_MAX 16
// This number can't scale too much, because we use linear search to find the current reader when locking and unlocking. We also use linear search to find available reader slots and to check whether we have any unused readers, but those could be replaced with another ring buffer. This should be good for now.

typedef struct {
	cothread_t thread;
	unsigned depth;
} active_thread;
struct async_rwlock_s {
	active_thread *rdactive;
	unsigned rdsize;
	cothread_t *rdqueue;
	unsigned rdcur;
	unsigned rdcount;

	active_thread wractive;
	unsigned wrsize;
	cothread_t *wrqueue;
	unsigned wrcur;
	unsigned wrcount;
};

static unsigned active_reader_index(async_rwlock_t *const lock, cothread_t const thread) {
	unsigned i = 0;
	for(; i < READERS_MAX; ++i) if(thread == lock->rdactive[i].thread) break;
	return i;
}
static unsigned unused_reader_index(async_rwlock_t *const lock) {
	unsigned i = 0;
	for(; i < READERS_MAX; ++i) if(!lock->rdactive[i].thread) break;
	return i;
}
static int has_active_readers(async_rwlock_t *const lock) {
	unsigned i = 0;
	for(; i < READERS_MAX; ++i) if(lock->rdactive[i].thread) return 1;
	return 0;
}
static int lock_next(async_rwlock_t *const lock, unsigned const unused_reader) {
	assert(unused_reader < READERS_MAX && "Invalid unused reader");
	active_thread *const state = &lock->rdactive[unused_reader];
	if(lock->wrcount) {
		if(has_active_readers(lock)) return 0;
		lock->wractive.thread = lock->wrqueue[lock->wrcur];
		lock->wractive.depth = 1;
		lock->wrcur = (lock->wrcur + 1) % lock->wrsize;
		lock->wrcount--;
		async_wakeup(lock->wractive.thread);
		return 0;
	} else if(lock->rdcount) {
		state->thread = lock->rdqueue[lock->rdcur];
		state->depth = 1;
		lock->rdcur = (lock->rdcur + 1) % lock->rdsize;
		lock->rdcount--;
		async_wakeup(state->thread);
	}
	return 1;
}

async_rwlock_t *async_rwlock_create(void) {
	async_rwlock_t *const lock = calloc(1, sizeof(struct async_rwlock_s));
	if(!lock) return NULL;
	lock->rdactive = calloc(READERS_MAX, sizeof(active_thread));
	lock->rdsize = 50;
	lock->rdqueue = calloc(lock->rdsize, sizeof(cothread_t));
	lock->wrsize = 10;
	lock->wrqueue = calloc(lock->wrsize, sizeof(cothread_t));
	if(!lock->rdactive || !lock->rdqueue || !lock->wrqueue) {
		async_rwlock_free(lock);
		return NULL;
	}
	return lock;
}
void async_rwlock_free(async_rwlock_t *const lock) {
	if(!lock) return;
	free(lock->rdactive); lock->rdactive = NULL;
	free(lock->rdqueue); lock->rdqueue = NULL;
	free(lock->wrqueue); lock->wrqueue = NULL;
	free(lock);
}
void async_rwlock_rdlock(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	cothread_t const thread = co_active();
	unsigned i = active_reader_index(lock, thread);
	if(i < READERS_MAX) {
		active_thread *const state = &lock->rdactive[i];
		state->depth++;
		return;
	}
	i = lock->rdcount || lock->wractive.thread ?
		READERS_MAX :
		unused_reader_index(lock);
	if(i >= READERS_MAX) {
		if(lock->rdcount >= lock->rdsize) {
			assert(0 && "Read queue growth not yet implemented");
		}
		lock->rdqueue[(lock->rdcur + lock->rdcount) % lock->rdsize] = thread;
		lock->rdcount++;
	} else {
		active_thread *const state = &lock->rdactive[i];
		state->thread = thread;
		state->depth = 1;
	}
}
int async_rwlock_tryrdlock(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	if(!async_rwlock_rdcheck(lock)) {
		if(lock->rdcount) return -1;
		if(lock->wractive.thread) return -1;
		if(unused_reader_index(lock) >= READERS_MAX) return -1;
	}
	async_rwlock_rdlock(lock);
	return 0;
}
void async_rwlock_rdunlock(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	assert(!lock->wractive.thread && "Read unlocked while writer active");
	assert(!lock->wractive.depth && "Read unlocked while writer inconsistent");

	cothread_t const thread = co_active();
	unsigned const i = active_reader_index(lock, thread);
	assert(i < READERS_MAX && "Reader unlocked without being locked");
	active_thread *const state = &lock->rdactive[i];

	assert(state->depth && "Active reader not holding lock");
	if(--state->depth) return;
	state->thread = NULL;

	lock_next(lock, i);
}
void async_rwlock_wrlock(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	cothread_t const thread = co_active();
	if(thread == lock->wractive.thread) {
		lock->wractive.depth++;
	} else if(lock->rdcount || lock->wractive.thread || has_active_readers(lock)) {
		if(lock->wrcount >= lock->wrsize) {
			assert(0 && "Write queue growth not yet implemented");
		}
		lock->wrqueue[(lock->wrcur + lock->wrcount) % lock->wrsize] = thread;
		lock->wrcount++;
	} else {
		lock->wractive.thread = thread;
		lock->wractive.depth = 1;
	}
}
int async_rwlock_trywrlock(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	if(!async_rwlock_wrcheck(lock)) {
		if(lock->rdcount) return -1;
		if(lock->wractive.thread) return -1;
	}
	async_rwlock_wrlock(lock);
	return 0;
}
void async_rwlock_wrunlock(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	cothread_t const thread = co_active();
	active_thread *const state = &lock->wractive;
	assert(thread == state->thread && "Writer unlocked without being active");
	assert(state->depth && "Active writer not holding lock");
	if(--state->depth) return;
	state->thread = NULL;

	for(unsigned i = 0; i < READERS_MAX; ++i) if(!lock_next(lock, i)) break;
}

int async_rwlock_rdcheck(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	cothread_t const thread = co_active();
	unsigned const i = active_reader_index(lock, thread);
	return i < READERS_MAX;
}
int async_rwlock_wrcheck(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	cothread_t const thread = co_active();
	return thread == lock->wractive.thread;
}

