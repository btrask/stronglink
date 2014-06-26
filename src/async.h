#ifndef ASYNC_H
#define ASYNC_H

#include <uv.h>
#include "../deps/libco/libco.h"

extern uv_loop_t *loop;
extern cothread_t yield;

static void async_fs_cb(uv_fs_t *const req) {
	co_switch(req->data);
}
static void async_timer_cb(uv_timer_t *const timer) {
	co_switch(timer->data);
}
static void async_close_cb(uv_handle_t *const handle) {
	co_switch(handle->data);
}

typedef struct {
	cothread_t thread;
	int status;
} async_state;

static void async_write_cb(uv_write_t *const req, int const status) {
	async_state *const state = req->data;
	state->status = status;
	co_switch(state->thread);
}
static void async_exit_cb(uv_process_t *const proc, int64_t const status, int const signal) {
	async_state *const state = proc->data;
	state->status = status;
	co_switch(state->thread);
}

void async_init(void);
void async_wakeup(cothread_t const thread);

void co_terminate(void);

#endif
