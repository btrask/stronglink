#include <assert.h>
#include <stdlib.h>
#include "async.h"

typedef struct thread_list thread_list;
struct thread_list {
	cothread_t thread;
	thread_list *next;
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
	thread_list *const next = sem->head;
	sem->head = next->next;
	if(!sem->head) sem->tail = NULL;
	next->next = NULL;
	async_wakeup(next->thread);
}
void async_sem_wait(async_sem_t *const sem) {
	assert(sem);
	assert(yield);
	assert(co_active() != yield);
	if(sem->value) {
		--sem->value;
		return;
	}
	thread_list us = {
		.thread = co_active(),
		.next = NULL,
	};
	if(!sem->head) sem->head = &us;
	if(sem->tail) sem->tail->next = &us;
	sem->tail = &us;
	async_yield();
	assert(!us.next && "Woke up in wrong order");
}
int async_sem_trywait(async_sem_t *const sem) {
	assert(sem);
	if(0 == sem->value) return -1;
	--sem->value;
	return 0;
}

