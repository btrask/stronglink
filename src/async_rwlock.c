#include <assert.h>
#include <stdio.h> /* For debugging */
#include <stdlib.h>
#include "async.h"

#define READERS_MAX 16
// This number can't scale too much, because we use linear search to find the current reader when locking and unlocking. We also use linear search to find available reader slots and to check whether we have any unused readers, but those could be replaced with another ring buffer. This should be good for now.

struct async_rwlock_s {
	cothread_t *rdactive;
	unsigned rdsize;
	cothread_t *rdqueue;
	unsigned rdcur;
	unsigned rdcount;

	cothread_t wractive;
	unsigned wrsize;
	cothread_t *wrqueue;
	unsigned wrcur;
	unsigned wrcount;

	cothread_t upgrade;
};

static unsigned active_reader_index(async_rwlock_t *const lock, cothread_t const thread) {
	unsigned i = 0;
	for(; i < READERS_MAX; ++i) if(thread == lock->rdactive[i]) break;
	return i;
}
static unsigned unused_reader_index(async_rwlock_t *const lock) {
	unsigned i = 0;
	for(; i < READERS_MAX; ++i) if(!lock->rdactive[i]) break;
	return i;
}
static int has_active_readers(async_rwlock_t *const lock) {
	unsigned i = 0;
	for(; i < READERS_MAX; ++i) if(lock->rdactive[i]) return 1;
	return 0;
}
static int lock_next(async_rwlock_t *const lock, unsigned const i) {
	assert(i < READERS_MAX && "Invalid unused reader");
	assert(!lock->rdactive[i] && "Reader in use");
	if(lock->wrcount) {
		if(has_active_readers(lock)) return 0;
		lock->wractive = lock->wrqueue[lock->wrcur];
		lock->wrcur = (lock->wrcur + 1) % lock->wrsize;
		lock->wrcount--;
		async_wakeup(lock->wractive);
		return 0;
	} else if(lock->rdcount) {
		lock->rdactive[i] = lock->rdqueue[lock->rdcur];
		lock->rdcur = (lock->rdcur + 1) % lock->rdsize;
		lock->rdcount--;
		async_wakeup(lock->rdactive[i]);
	}
	return 1;
}

async_rwlock_t *async_rwlock_create(void) {
	async_rwlock_t *lock = calloc(1, sizeof(struct async_rwlock_s));
	if(!lock) return NULL;
	lock->rdactive = calloc(READERS_MAX, sizeof(cothread_t));
	lock->rdsize = 50;
	lock->rdqueue = calloc(lock->rdsize, sizeof(cothread_t));
	lock->wrsize = 10;
	lock->wrqueue = calloc(lock->wrsize, sizeof(cothread_t));
	if(!lock->rdactive || !lock->rdqueue || !lock->wrqueue) {
		async_rwlock_free(lock); lock = NULL;
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
	assert(!async_rwlock_rdcheck(lock) && "Recursive lock");
	assert(!async_rwlock_wrcheck(lock) && "Recursive lock");
	unsigned const i = lock->rdcount || lock->wractive || lock->upgrade ?
		READERS_MAX :
		unused_reader_index(lock);
	if(i >= READERS_MAX) {
		if(lock->rdcount >= lock->rdsize) {
			assert(0 && "Read queue growth not yet implemented");
		}
		lock->rdqueue[(lock->rdcur + lock->rdcount) % lock->rdsize] = co_active();
		lock->rdcount++;
		async_yield();
		assert(!lock->upgrade && "Write lock acquired while upgrade is pending");
	} else {
		assert(!lock->upgrade && "Read lock acquired while upgrade pending");
		lock->rdactive[i] = co_active();
	}
}
int async_rwlock_tryrdlock(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	assert(!async_rwlock_rdcheck(lock) && "Recursive lock");
	assert(!async_rwlock_wrcheck(lock) && "Recursive lock");
	if(lock->rdcount) return -1;
	if(lock->wractive) return -1;
	if(unused_reader_index(lock) >= READERS_MAX) return -1;
	async_rwlock_rdlock(lock);
	return 0;
}
void async_rwlock_rdunlock(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	assert(async_rwlock_rdcheck(lock) && "Not locked");
	assert(!async_rwlock_wrcheck(lock) && "Wrong unlock");
	unsigned const i = active_reader_index(lock, co_active());
	lock->rdactive[i] = NULL;

	if(lock->upgrade) {
		if(has_active_readers(lock)) return;
		lock->wractive = lock->upgrade;
		lock->upgrade = NULL;
		async_wakeup(lock->wractive);
	} else {
		lock_next(lock, i);
	}
}
void async_rwlock_wrlock(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	assert(!async_rwlock_rdcheck(lock) && "Recursive lock");
	assert(!async_rwlock_wrcheck(lock) && "Recursive lock");
	if(lock->rdcount || lock->wractive || has_active_readers(lock)) {
		if(lock->wrcount >= lock->wrsize) {
			assert(0 && "Write queue growth not yet implemented");
		}
		lock->wrqueue[(lock->wrcur + lock->wrcount) % lock->wrsize] = co_active();
		lock->wrcount++;
		async_yield();
	} else {
		assert(!lock->upgrade && "Write lock acquired with pending upgrade");
		lock->wractive = co_active();
	}
}
int async_rwlock_trywrlock(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	assert(!async_rwlock_rdcheck(lock) && "Recursive lock");
	assert(!async_rwlock_wrcheck(lock) && "Recursive lock");
	if(lock->rdcount) return -1;
	if(lock->wractive) return -1;
	if(lock->upgrade) return -1;
	if(has_active_readers(lock)) return -1;
	async_rwlock_wrlock(lock);
	return 0;
}
void async_rwlock_wrunlock(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	assert(!async_rwlock_rdcheck(lock) && "Wrong unlock");
	assert(async_rwlock_wrcheck(lock) && "Not locked");
	lock->wractive = NULL;

	assert(!lock->upgrade && "Upgrade pending during write lock");
	for(unsigned i = 0; i < READERS_MAX; ++i) if(!lock_next(lock, i)) break;
}

int async_rwlock_rdcheck(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	unsigned const i = active_reader_index(lock, co_active());
	return i < READERS_MAX;
}
int async_rwlock_wrcheck(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	return co_active() == lock->wractive;
}

int async_rwlock_upgrade(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	assert(async_rwlock_rdcheck(lock) && "Not locked");
	assert(!async_rwlock_wrcheck(lock) && "Already locked");
	cothread_t const thread = co_active();
	if(lock->upgrade) return -1;
	unsigned const i = active_reader_index(lock, thread);
	lock->rdactive[i] = NULL;
	if(has_active_readers(lock)) {
		lock->upgrade = thread;
		async_yield();
		assert(!lock->upgrade && "Upgrade not cleared");
		assert(thread == lock->wractive && "Wrong upgrade woken");
	} else {
		lock->wractive = thread;
	}
	return 0;
}
int async_rwlock_downgrade(async_rwlock_t *const lock) {
	assert(lock && "Lock object required");
	assert(!async_rwlock_rdcheck(lock) && "Wrong lock");
	assert(async_rwlock_wrcheck(lock) && "Not locked");
	lock->wractive = NULL;
	unsigned i = unused_reader_index(lock);
	assert(0 == i && "Downgraded write lock should have no active readers");
	lock->rdactive[i++] = co_active();

	for(; i < READERS_MAX; ++i) if(!lock_next(lock, i)) break;
	return 0;
}

