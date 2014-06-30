#ifndef ASYNC_H
#define ASYNC_H

#include <uv.h>
#include "../deps/libco/libco.h"

#define STACK_SIZE (1024 * 50 * sizeof(void *) / 4)

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

uv_file async_fs_open(const char* path, int flags, int mode);
ssize_t async_fs_close(uv_file file);
ssize_t async_fs_read(uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset);
ssize_t async_fs_write(uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset);
ssize_t async_fs_unlink(const char* path);
ssize_t async_fs_link(const char* path, const char* new_path);

ssize_t async_fs_fstat(uv_file file, uv_stat_t *stats);

int async_random(unsigned char *const buf, size_t const len);

typedef struct async_mutex_s async_mutex_t;

async_mutex_t *async_mutex_create(void);
void async_mutex_free(async_mutex_t *const mutex);
void async_mutex_lock(async_mutex_t *const mutex);
int async_mutex_trylock(async_mutex_t *const mutex);
void async_mutex_unlock(async_mutex_t *const mutex);
int async_mutex_check(async_mutex_t *const mutex);

typedef struct async_rwlock_s async_rwlock_t;

async_rwlock_t *async_rwlock_create(void);
void async_rwlock_free(async_rwlock_t *const lock);
void async_rwlock_rdlock(async_rwlock_t *const lock);
int async_rwlock_tryrdlock(async_rwlock_t *const lock);
void async_rwlock_rdunlock(async_rwlock_t *const lock);
void async_rwlock_wrlock(async_rwlock_t *const lock);
int async_rwlock_trywrlock(async_rwlock_t *const lock);
void async_rwlock_wrunlock(async_rwlock_t *const lock);
int async_rwlock_rdcheck(async_rwlock_t *const lock);
int async_rwlock_wrcheck(async_rwlock_t *const lock);

#endif
