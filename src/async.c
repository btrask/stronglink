#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "stdio.h"
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

#define ASYNC_FS_WRAP(name, args...) \
	cothread_t const thread = co_active(); \
	uv_fs_t req = { .data = thread }; \
	int const err = uv_fs_##name(loop, &req, ##args, async_fs_cb); \
	if(err < 0) return err; \
	co_switch(yield); \
	uv_fs_req_cleanup(&req); \
	return req.result;

uv_file async_fs_open(const char* path, int flags, int mode) {
	ASYNC_FS_WRAP(open, path, flags, mode)
}
ssize_t async_fs_close(uv_file file) {
	ASYNC_FS_WRAP(close, file)
}
ssize_t async_fs_read(uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset) {
	ASYNC_FS_WRAP(read, file, bufs, nbufs, offset)
}
ssize_t async_fs_write(uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset) {
	ASYNC_FS_WRAP(write, file, bufs, nbufs, offset)
}
ssize_t async_fs_unlink(const char* path) {
	ASYNC_FS_WRAP(unlink, path)
}
ssize_t async_fs_link(const char* path, const char* new_path) {
	ASYNC_FS_WRAP(link, path, new_path)
}

ssize_t async_fs_fstat(uv_file file, uv_stat_t *stats) {
	uv_fs_t req = { .data = co_active() };
	int const err = uv_fs_fstat(loop, &req, file, async_fs_cb);
	if(err < 0) return err;
	co_switch(yield);
	memcpy(stats, &req.statbuf, sizeof(uv_stat_t));
	uv_fs_req_cleanup(&req);
	return req.result;
}

