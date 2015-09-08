// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdio.h> /* For debugging */
#include <string.h>
#include <openssl/rand.h>
#include "async.h"

thread_local uv_loop_t async_loop[1] = {};
thread_local async_t *async_main = NULL;

static thread_local async_t master[1] = {};
static thread_local async_t *active = NULL;

static thread_local cothread_t trampoline = NULL;
static thread_local void (*arg_func)(void *) = NULL;
static thread_local void *arg_arg = NULL;

static void trampoline_fn(void);

int async_init(void) {
	int rc = uv_loop_init(async_loop);
	if(rc < 0) return rc;
	master->fiber = co_active();
	master->flags = 0;
	active = master;
	async_main = master;
	trampoline = co_create(STACK_DEFAULT, trampoline_fn);
	if(!trampoline) return UV_ENOMEM;
	return 0;
}
void async_destroy(void) {
	assert(async_loop);
	co_delete(trampoline); trampoline = NULL;
	uv_loop_close(async_loop);
	memset(async_loop, 0, sizeof(async_loop));

// TODO: Not safe for various libco backends?
#if defined(CORO_USE_VALGRIND)
	co_delete(master->fiber);
	memset(master, 0, sizeof(master));
#endif
}

async_t *async_active(void) {
	return active;
}
static void async_start(void) {
	async_t thread[1];
	thread->fiber = co_active();
	thread->flags = 0;
	active = thread;
	void (*const func)(void *) = arg_func;
	void *arg = arg_arg;
	func(arg);
	async_call(co_delete, thread->fiber);
}
int async_spawn(size_t const stack, void (*const func)(void *), void *const arg) {
	cothread_t const fiber = co_create(stack, async_start);
	if(!fiber) return UV_ENOMEM;
	arg_func = func;
	arg_arg = arg;

	// Similar to async_wakeup but the new thread is not created yet
	async_t *const original = async_main;
	async_main = async_active();
	co_switch(fiber);
	async_main = original;

	return 0;
}
void async_switch(async_t *const thread) {
	active = thread;
	co_switch(thread->fiber);
}
void async_wakeup(async_t *const thread) {
	assert(thread != async_main);
	async_t *const original = async_main;
	async_main = async_active();
	async_switch(thread);
	async_main = original;
}
static void trampoline_fn(void) {
	for(;;) {
		void (*const func)(void *) = arg_func;
		void *const arg = arg_arg;
		func(arg);
		async_switch(async_main);
	}
}
void async_call(void (*const func)(void *), void *const arg) {
	arg_func = func;
	arg_arg = arg;
	active = async_main;
	co_switch(trampoline);
}

void async_yield(void) {
	async_switch(async_main);
}
int async_yield_cancelable(void) {
	async_t *const thread = async_active();
	if(ASYNC_CANCELED & thread->flags) {
		thread->flags &= ~ASYNC_CANCELED;
		return UV_ECANCELED;
	}
	assert(!(ASYNC_CANCELABLE & thread->flags));
	thread->flags |= ASYNC_CANCELABLE;
	async_yield();
	thread->flags &= ~ASYNC_CANCELABLE;
	if(ASYNC_CANCELED & thread->flags) {
		thread->flags &= ~ASYNC_CANCELED;
		return UV_ECANCELED;
	}
	return 0;
}
int async_yield_flags(unsigned const flags) {
	if(ASYNC_CANCELABLE & flags) return async_yield_cancelable();
	async_yield();
	return 0;
}
int async_canceled(void) {
	async_t *const thread = async_active();
	if(ASYNC_CANCELED & thread->flags) {
		thread->flags &= ~ASYNC_CANCELED;
		return UV_ECANCELED;
	}
	return 0;
}
void async_cancel(async_t *const thread) {
	if(!thread) return;
	thread->flags |= ASYNC_CANCELED;
	if(ASYNC_CANCELABLE & thread->flags) async_wakeup(thread);
}


int async_random(unsigned char *const buf, size_t const len) {
	// TODO: Come up with a thread-safe and lock-free RNG. Maybe just read from /dev/urandom ourselves on appropriate platforms.
//	async_pool_enter(NULL);
	int const rc = RAND_bytes(buf, len);
//	async_pool_leave(NULL);
	if(rc <= 0) return -1; // Zero is an error too.
	return 0;
}

typedef struct {
	async_t *thread;
	int status;
	struct addrinfo *res;
} getaddrinfo_state;
static void getaddrinfo_cb(uv_getaddrinfo_t *const req, int const status, struct addrinfo *const res) {
	getaddrinfo_state *const state = req->data;
	state->status = status;
	state->res = res;
	async_switch(state->thread);
}
int async_getaddrinfo(char const *const node, char const *const service, struct addrinfo const *const hints, struct addrinfo **const res) {
// uv_getaddrinfo kind of sucks so we try to avoid it.
// TODO: We don't ever define __POSIX__ currently.
#if defined(__POSIX__) || defined(CORO_USE_VALGRIND)
	async_pool_enter(NULL);
	int rc = getaddrinfo(node, service, hints, res);
	async_pool_leave(NULL);
	return rc;
#else
	getaddrinfo_state state[1];
	uv_getaddrinfo_t req[1];
	req->data = state;
	uv_getaddrinfo_cb cb = NULL;
	if(async_main) {
		state->thread = async_active();
		cb = getaddrinfo_cb;
	}
	int rc = uv_getaddrinfo(async_loop, req, cb, node, service, hints);
	if(rc < 0) return rc;
	if(cb) async_yield();
	if(res) *res = state->res;
	return state->status;
#endif
}

static void async_close_cb(uv_handle_t *const handle) {
	async_switch(handle->data);
}
static void timer_cb(uv_timer_t *const timer) {
	async_switch(timer->data);
}
int async_sleep(uint64_t const milliseconds) {
	// TODO: Pool timers together.
	uv_timer_t timer[1];
	timer->data = async_active();
	int rc = uv_timer_init(async_loop, timer);
	if(rc < 0) return rc;
	if(milliseconds > 0) {
		rc = uv_timer_start(timer, timer_cb, milliseconds, 0);
		if(rc < 0) return rc;
		async_yield();
	}
	async_close((uv_handle_t *)timer);
	return 0;
}
void async_close(uv_handle_t *const handle) {
	if(UV_UNKNOWN_HANDLE == handle->type) return;
	handle->data = async_active();
	uv_close(handle, async_close_cb);
	async_yield();
	memset(handle, 0, uv_handle_size(handle->type));
	// Luckily UV_UNKNOWN_HANDLE is 0
}

