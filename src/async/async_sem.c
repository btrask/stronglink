#include <assert.h>
#include <stdlib.h>
#include "async.h"

#define UNUSED(x) ((void)(x))

struct async_thread_list {
	async_sem_t *sem;
	async_t *thread;
	async_thread_list *prev;
	async_thread_list *next;
	int res;
};

void async_sem_init(async_sem_t *const sem, unsigned const value, unsigned const flags) {
	assert(sem);
	sem->head = NULL;
	sem->tail = NULL;
	sem->value = value;
	sem->flags = flags;
}
void async_sem_destroy(async_sem_t *const sem) {
	if(!sem) return;
	assert(!sem->head);
	assert(!sem->tail);
}

void async_sem_post(async_sem_t *const sem) {
	assert(sem);
	if(!sem->head) {
		++sem->value;
		return;
	}
	assert(0 == sem->value && "Thread shouldn't have been waiting");
	assert(sem->tail && "Tail not set");
	async_thread_list *const us = sem->head;
	sem->head = us->next;
	if(sem->head) sem->head->prev = NULL;
	if(!sem->head) sem->tail = NULL;
	async_wakeup(us->thread);
}
static void timeout_cb(uv_timer_t *const timer) {
	async_thread_list *const us = timer->data;
	async_sem_t *const sem = us->sem;
	if(us->prev) us->prev->next = us->next;
	if(us->next) us->next->prev = us->prev;
	if(us == sem->head) sem->head = us->next;
	if(us == sem->tail) sem->tail = NULL;
	us->res = UV_ETIMEDOUT;
	async_switch(us->thread);
}

void async_sem_wait(async_sem_t *const sem) {
	int const rc = async_sem_timedwait(sem, UINT64_MAX);
	assert(rc >= 0);
	UNUSED(rc);
}
int async_sem_trywait(async_sem_t *const sem) {
	assert(sem);
	if(0 == sem->value) return -1;
	--sem->value;
	return 0;
}
int async_sem_timedwait(async_sem_t *const sem, uint64_t const future) {
	assert(sem);
	assert(yield);
	assert(async_active() != yield); // TODO: Seems to be triggering...?
	if(sem->value) {
		--sem->value;
		return 0;
	}
	uint64_t now = 0;
	if(future < UINT64_MAX) {
		now = uv_now(loop);
		if(now >= future) return UV_ETIMEDOUT;
	}
	async_thread_list us[1];
	us->sem = sem;
	us->thread = async_active();
	us->prev = sem->tail;
	us->next = NULL;
	us->res = 0;
	if(!sem->head) sem->head = us;
	if(sem->tail) sem->tail->next = us;
	sem->tail = us;

	uv_timer_t timer[1];
	if(future < UINT64_MAX) {
		timer->data = us;
		uv_timer_init(loop, timer);
		uv_timer_start(timer, timeout_cb, future - now, 0);
	}
	async_yield();
	if(future < UINT64_MAX) {
		async_close((uv_handle_t *)timer);
	}
	return us->res;
}

