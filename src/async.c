#include <assert.h>
#include <stdio.h> /* For debugging */
#include <string.h>
#include <openssl/rand.h>
#include "async.h"

uv_loop_t *loop = NULL;
cothread_t yield = NULL;

static cothread_t reap = NULL;
static cothread_t zombie = NULL;

static void reaper(void) {
	for(;;) {
		co_delete(zombie); zombie = NULL;
		co_switch(yield);
	}
}
void async_init(void) {
	loop = uv_default_loop();
	yield = co_active();
	reap = co_create(1024 * 4 * sizeof(void *) / 4, reaper);
	assert(reap && "async_init() thread creation failed");
}
void co_terminate(void) {
	zombie = co_active();
	co_switch(reap);
}

static void wakeup_cb(uv_handle_t *const handle) {
	cothread_t const thread = handle->data;
	free(handle);
	co_switch(thread);
}
void async_wakeup(cothread_t const thread) {
	// TODO: Use one global timer with a queue of threads to wake.
	uv_timer_t *const timer = malloc(sizeof(uv_timer_t));
	timer->data = thread;
	uv_timer_init(loop, timer);
	uv_close((uv_handle_t *)timer, wakeup_cb);
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
	uv_work_t req = { .data = &state };
	int const err = uv_queue_work(loop, &req, random_cb, after_random_cb);
	if(err < 0) return err;
	co_switch(yield);
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
	uv_getaddrinfo_t req = { .data = &state };
	int const err = uv_getaddrinfo(loop, &req, getaddrinfo_cb, node, service, hints);
	if(err < 0) return err;
	co_switch(yield);
	if(res) *res = state.res;
	return state.status;
}

