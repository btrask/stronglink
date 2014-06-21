#include <stdlib.h>
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
static void async_write_cb(uv_write_t *const req, int status) {
	co_switch(req->data);
}

static void async_terminate_cb(uv_timer_t *const timer) {
	co_delete(timer->data);
	uv_timer_stop(timer);
	free(timer);
}
static void co_terminate(void) {
	uv_timer_t *const timer = malloc(sizeof(uv_timer_t));
	timer->data = co_active();
	uv_timer_init(loop, timer);
	uv_timer_start(timer, async_terminate_cb, 0, 0);
	co_switch(yield);
}

