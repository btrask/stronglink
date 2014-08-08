#include <assert.h>
#include <stdio.h> /* For debugging */
#include <string.h>
#include <openssl/rand.h>
#include "async.h"

thread_local uv_loop_t *loop = NULL;
thread_local cothread_t yield = NULL;

void async_init(void) {
	loop = uv_default_loop();
	yield = co_active();
}

static thread_local cothread_t trampoline = NULL;
static thread_local void (*arg_func)(void *) = NULL;
static thread_local void *arg_arg = NULL;

static void trampoline_fn(void) {
	for(;;) {
		void (*const func)(void *) = arg_func;
		void *const arg = arg_arg;
		func(arg);
		co_switch(yield);
	}
}
static void async_start(void) {
	void (*const func)(void *) = arg_func;
	void *const arg = arg_arg;
	func(arg);
	async_call(co_delete, co_active());
}
int async_thread(size_t const stack, void (*const func)(void *), void *const arg) {
	cothread_t const thread = co_create(stack, async_start);
	if(!thread) return -1;
	arg_func = func;
	arg_arg = arg;
	async_wakeup(thread);
	return 0;
}
void async_call(void (*const func)(void *), void *const arg) {
	if(!trampoline) {
		trampoline = co_create(STACK_DEFAULT, trampoline_fn);
		assert(trampoline);
	}
	arg_func = func;
	arg_arg = arg;
	co_switch(trampoline);
}
void async_wakeup(cothread_t const thread) {
	if(thread == yield) return; // The main thread will get woken up when we yield to it. It would never yield back to us.
	cothread_t const original = yield;
	yield = co_active();
	co_switch(thread);
	yield = original;
}

struct random_state {
	cothread_t thread;
	unsigned char *buf;
	size_t len;
	int status;
};
static void random_cb(uv_work_t *const req) {
	struct random_state *const state = req->data;
	if(RAND_bytes(state->buf, state->len) <= 0) state->status = -1;
	// Apparently zero is an error too.
}
static void after_random_cb(uv_work_t *const req, int const status) {
	struct random_state *const state = req->data;
	if(status < 0) state->status = status;
	co_switch(state->thread);
}
int async_random(unsigned char *const buf, size_t const len) {
	struct random_state state = {
		.thread = co_active(),
		.buf = buf,
		.len = len,
		.status = 0,
	};
	uv_work_t req;
	req.data = &state;
	int const err = uv_queue_work(loop, &req, random_cb, after_random_cb);
	if(err < 0) return err;
	async_yield();
	return state.status;
}

typedef struct {
	cothread_t thread;
	int status;
	struct addrinfo *res;
} getaddrinfo_state;
static void getaddrinfo_cb(uv_getaddrinfo_t *const req, int const status, struct addrinfo *const res) {
	getaddrinfo_state *const state = req->data;
	state->status = status;
	state->res = res;
	co_switch(state->thread);
}
int async_getaddrinfo(char const *const node, char const *const service, struct addrinfo const *const hints, struct addrinfo **const res) {
	getaddrinfo_state state = { .thread = co_active() };
	uv_getaddrinfo_t req;
	req.data = &state;
	int const err = uv_getaddrinfo(loop, &req, getaddrinfo_cb, node, service, hints);
	if(err < 0) return err;
	async_yield();
	if(res) *res = state.res;
	return state.status;
}

int async_sleep(uint64_t const milliseconds) {
	// TODO: Pool timers together.
	uv_timer_t timer;
	timer.data = co_active();
	int err;
	err = uv_timer_init(loop, &timer);
	if(err < 0) return err;
	if(milliseconds > 0) {
		err = uv_timer_start(&timer, async_timer_cb, milliseconds, 0);
		if(err < 0) return err;
		async_yield();
	}
	uv_close((uv_handle_t *)&timer, async_close_cb);
	async_yield();
	return 0;
}

