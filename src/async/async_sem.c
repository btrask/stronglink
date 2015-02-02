#include <assert.h>
#include <stdlib.h>
#include "async.h"

#define UNUSED(x) ((void)(x))

typedef struct thread_list thread_list;
struct thread_list {
	async_sem_t *sem;
	cothread_t thread;
	thread_list *prev;
	thread_list *next;
	int res;
};

struct async_sem_s {
	thread_list *head;
	thread_list *tail;
	unsigned int value;
};

async_sem_t *async_sem_create(unsigned int const value) {
	async_sem_t *sem = calloc(1, sizeof(async_sem_t));
	if(!sem) return NULL;
	sem->head = NULL;
	sem->tail = NULL;
	sem->value = value;
	return sem;
}
void async_sem_free(async_sem_t *const sem) {
	if(!sem) return;
	assert(!sem->head);
	assert(!sem->tail);
	free(sem);
}

void async_sem_post(async_sem_t *const sem) {
	assert(sem);
	if(!sem->head) {
		++sem->value;
		return;
	}
	assert(0 == sem->value && "Thread shouldn't have been waiting");
	assert(sem->tail && "Tail not set");
	thread_list *const us = sem->head;
	sem->head = us->next;
	if(sem->head) sem->head->prev = NULL;
	if(!sem->head) sem->tail = NULL;
	async_wakeup(us->thread);
}
static void timeout_cb(uv_timer_t *const timer) {
	thread_list *const us = timer->data;
	async_sem_t *const sem = us->sem;
	if(us->prev) us->prev->next = us->next;
	if(us->next) us->next->prev = us->prev;
	if(us == sem->head) sem->head = us->next;
	if(us == sem->tail) sem->tail = NULL;
	us->res = UV_ETIMEDOUT;
	co_switch(us->thread);
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
	assert(co_active() != yield);
	if(sem->value) {
		--sem->value;
		return 0;
	}
	uint64_t now = 0;
	if(future < UINT64_MAX) {
		now = uv_now(loop);
		if(now >= future) return UV_ETIMEDOUT;
	}
	thread_list us[1];
	us->sem = sem;
	us->thread = co_active();
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

