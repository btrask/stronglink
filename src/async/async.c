#include <assert.h>
#include <stdio.h> /* For debugging */
#include <string.h>
#include <openssl/rand.h>
#include "async.h"

static thread_local uv_loop_t _loop;
thread_local uv_loop_t *loop = NULL;
thread_local cothread_t yield = NULL;

void async_init(void) {
	if(!getenv("UV_THREADPOOL_SIZE")) putenv("UV_THREADPOOL_SIZE=1");
	uv_loop_init(&_loop);
	loop = &_loop;
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

int async_random(unsigned char *const buf, size_t const len) {
	// TODO: Come up with a thread-safe and lock-free RNG. Maybe just read from /dev/urandom ourselves on appropriate platforms.
//	async_pool_enter(NULL);
	int const rc = RAND_bytes(buf, len);
//	async_pool_leave(NULL);
	if(rc <= 0) return -1; // Zero is an error too.
	return 0;
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
	getaddrinfo_state state;
	uv_getaddrinfo_t req;
	req.data = &state;
	uv_getaddrinfo_cb cb = NULL;
	if(yield) {
		state.thread = co_active();
		cb = getaddrinfo_cb;
	}
	int const err = uv_getaddrinfo(loop, &req, cb, node, service, hints);
	if(err < 0) return err;
	if(cb) async_yield();
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

