#include <assert.h>
#include <stdlib.h>
#include "async.h"

struct async_worker_s {
	uv_thread_t thread;
	uv_sem_t sem;
	async_t *work;
	async_t *main;
	uv_async_t async;
};

static void work(void *const arg) {
	async_worker_t *const worker = arg;
	async_init();
	worker->main = yield;
	yield = NULL;
	for(;;) {
		uv_sem_wait(&worker->sem);
		if(!worker->work) break;
		async_switch(worker->work);
		uv_async_send(&worker->async);
	}
}
static void enter(void *const arg) {
	async_worker_t *const worker = arg;
	uv_sem_post(&worker->sem);
}
static void leave(uv_async_t *const async) {
	async_worker_t *const worker = async->data;
	async_t *const work = worker->work;
	worker->work = NULL;
	async_switch(work);
}

async_worker_t *async_worker_create(void) {
	async_worker_t *worker = calloc(1, sizeof(struct async_worker_s));
	if(!worker) {
		return NULL;
	}
	if(uv_sem_init(&worker->sem, 0) < 0) {
		free(worker);
		return NULL;
	}
	worker->async.data = worker;
	if(uv_async_init(loop, &worker->async, leave) < 0) {
		uv_sem_destroy(&worker->sem);
		free(worker);
		return NULL;
	}
	if(uv_thread_create(&worker->thread, work, worker) < 0) {
		uv_sem_destroy(&worker->sem);
		async_close((uv_handle_t *)&worker->async);
		free(worker);
		return NULL;
	}
	uv_unref((uv_handle_t *)&worker->async);
	return worker;
}
void async_worker_free(async_worker_t *const worker) {
	if(!worker) return;
	assert(!worker->work);
	uv_sem_post(&worker->sem);
	uv_thread_join(&worker->thread);
	uv_sem_destroy(&worker->sem);
	uv_ref((uv_handle_t *)&worker->async);
	async_close((uv_handle_t *)&worker->async);
	free(worker);
}

void async_worker_enter(async_worker_t *const worker) {
	assert(worker);
	assert(!worker->work);
	worker->work = async_active();
	uv_ref((uv_handle_t *)&worker->async);
	async_call(enter, worker);
	// Now on worker thread
}
void async_worker_leave(async_worker_t *const worker) {
	assert(worker);
	assert(async_active() == worker->work);
	async_switch(worker->main);
	// Now on original thread
	uv_unref((uv_handle_t *)&worker->async);
}

