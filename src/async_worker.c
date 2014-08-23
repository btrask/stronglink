#include <assert.h>
#include <stdlib.h>
#include "async.h"

struct async_worker_s {
	uv_thread_t thread;
	uv_sem_t sem;
	cothread_t work;
	cothread_t main;
	uv_async_t async;
};

static thread_local async_worker_t *current = NULL;

static void work(void *const arg) {
	async_worker_t *const worker = arg;
	async_init();
	current = worker;
	worker->main = yield;
	yield = NULL;
	for(;;) {
		uv_sem_wait(&worker->sem);
		if(!worker->work) break;
		co_switch(worker->work);
		uv_async_send(&worker->async);
	}
}
static void enter(void *const arg) {
	async_worker_t *const worker = arg;
	uv_sem_post(&worker->sem);
}
static void leave(uv_async_t *const async) {
	async_worker_t *const worker = async->data;
	cothread_t const work = worker->work;
	worker->work = NULL;
	co_switch(work);
}

async_worker_t *async_worker_create(void) {
	async_worker_t *worker = calloc(1, sizeof(struct async_worker_s));
	if(!worker) goto bail;
	if(uv_sem_init(&worker->sem, 0) < 0) goto bail;
	worker->async.data = worker;
	if(uv_async_init(loop, &worker->async, leave) < 0) goto bail;
	if(uv_thread_create(&worker->thread, work, worker) < 0) goto bail;
	return worker;

bail:
	async_worker_free(worker);
	return NULL;
}
void async_worker_free(async_worker_t *const worker) {
	if(!worker) return;
	assert(!worker->work);
	uv_sem_post(&worker->sem);
	uv_thread_join(&worker->thread);
	uv_sem_destroy(&worker->sem);
	worker->async.data = co_active();
	uv_close((uv_handle_t *)&worker->async, async_close_cb);
	async_yield();
	free(worker);
}
void async_worker_enter(async_worker_t *const worker) {
	assert(worker);
	assert(!current);
	assert(!worker->work);
	worker->work = co_active();
	async_call(enter, worker);
	// Now on worker thread
}
void async_worker_leave(async_worker_t *const worker) {
	assert(worker);
	assert(current);
	assert(worker == current);
	assert(co_active() == worker->work);
	co_switch(worker->main);
	// Now on original thread
}
async_worker_t *async_worker_get_current(void) {
	return current;
}

