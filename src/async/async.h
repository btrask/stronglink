#ifndef ASYNC_H
#define ASYNC_H

#include <assert.h>
#include <stdio.h> /* Debugging */
#include "../../deps/uv/include/uv.h"
#include "../../deps/libco/libco.h"

// TODO: Get page size at runtime?
#define STACK_SIZE(kb) (1024 * (kb) * sizeof(void *) / 4)
#define STACK_DEFAULT STACK_SIZE(48)
#define STACK_MINIMUM STACK_SIZE(16)
// 4K on sjlj/32
// 16K on sjlj/64?

#ifndef ASYNC_DEBUG
#define async_yield() (co_switch(yield))
#else
#define async_yield() ({ \
	assert(yield); \
	double const x = uv_now(loop) / 1000.0; \
	co_switch(yield); \
	double const y = uv_now(loop) / 1000.0; \
	if(y - x > 1.0) fprintf(stderr, "%s blocked for %f s\n", __PRETTY_FUNCTION__, y-x); \
})
#endif

#ifdef LIBCO_MP
#define thread_local __thread
#else
#define thread_local
#endif

extern thread_local uv_loop_t *loop;
extern thread_local cothread_t yield;

static void async_fs_cb(uv_fs_t *const req) {
	co_switch(req->data);
}
static void async_timer_cb(uv_timer_t *const timer) {
	co_switch(timer->data);
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
static void async_connect_cb(uv_connect_t *const req, int const status) {
	async_state *const state = req->data;
	state->status = status;
	co_switch(state->thread);
}

void async_init(void);
int async_thread(size_t const stack, void (*const func)(void *), void *const arg);
void async_call(void (*const func)(void *), void *const arg); // Conceptually, yields and then calls `func` from the main thread. Similar to `nextTick`.
void async_wakeup(cothread_t const thread);

int async_random(unsigned char *const buf, size_t const len);
int async_getaddrinfo(char const *const node, char const *const service, struct addrinfo const *const hints, struct addrinfo **const res);
int async_sleep(uint64_t const milliseconds);

void async_close(uv_handle_t *const handle);

// async_read.c
typedef struct {
	cothread_t thread;
	uv_buf_t buf[1];
	ssize_t nread;
} async_read_t;
ssize_t async_read(async_read_t *const req, uv_stream_t *const stream);
void async_read_cleanup(async_read_t *const req);
void async_read_cancel(async_read_t *const req);

// async_fs.c
uv_file async_fs_open(const char* path, int flags, int mode);
int async_fs_close(uv_file file);
ssize_t async_fs_read(uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset);
ssize_t async_fs_write(uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset);
int async_fs_unlink(const char* path);
int async_fs_link(const char* path, const char* new_path);
int async_fs_fsync(uv_file file);
int async_fs_fdatasync(uv_file file);
int async_fs_mkdir(const char* path, int mode);
int async_fs_ftruncate(uv_file file, int64_t offset);

int async_fs_fstat(uv_file file, uv_fs_t *const req);
int async_fs_stat(const char* path, uv_fs_t *const req);

int async_fs_mkdirp_fast(char *const path, size_t const len, int const mode);
int async_fs_mkdirp(char const *const path, int const mode);
int async_fs_mkdirp_dirname(char const *const path, int const mode);

char *async_fs_tempnam(char const *dir, char const *prefix);

// async_sem.c
typedef struct async_sem_s async_sem_t;
async_sem_t *async_sem_create(unsigned int const value);
void async_sem_free(async_sem_t *const sem);
void async_sem_post(async_sem_t *const sem);
void async_sem_wait(async_sem_t *const sem);
int async_sem_trywait(async_sem_t *const sem);
int async_sem_timedwait(async_sem_t *const sem, uint64_t const future);

// async_mutex.c
typedef struct async_mutex_s async_mutex_t;
async_mutex_t *async_mutex_create(void);
void async_mutex_free(async_mutex_t *const mutex);
void async_mutex_lock(async_mutex_t *const mutex);
int async_mutex_trylock(async_mutex_t *const mutex);
void async_mutex_unlock(async_mutex_t *const mutex);
int async_mutex_check(async_mutex_t *const mutex);

// async_rwlock.c
typedef struct async_rwlock_s async_rwlock_t;
async_rwlock_t *async_rwlock_create(void);
void async_rwlock_free(async_rwlock_t *const lock);
void async_rwlock_rdlock(async_rwlock_t *const lock);
int async_rwlock_tryrdlock(async_rwlock_t *const lock);
void async_rwlock_rdunlock(async_rwlock_t *const lock);
void async_rwlock_wrlock(async_rwlock_t *const lock);
int async_rwlock_trywrlock(async_rwlock_t *const lock);
void async_rwlock_wrunlock(async_rwlock_t *const lock);
int async_rwlock_upgrade(async_rwlock_t *const lock);
void async_rwlock_downgrade(async_rwlock_t *const lock);

// async_cond.c
typedef struct async_cond_s async_cond_t;
async_cond_t *async_cond_create(void);
void async_cond_free(async_cond_t *const cond);
void async_cond_signal(async_cond_t *const cond);
void async_cond_broadcast(async_cond_t *const cond);
void async_cond_wait(async_cond_t *const cond, async_mutex_t *const mutex);
int async_cond_timedwait(async_cond_t *const cond, async_mutex_t *const mutex, uint64_t const future);

// async_worker.c
typedef struct async_worker_s async_worker_t;
async_worker_t *async_worker_create(void);
void async_worker_free(async_worker_t *const worker);
void async_worker_enter(async_worker_t *const worker);
void async_worker_leave(async_worker_t *const worker);

// async_pool.c
typedef struct async_pool_s async_pool_t;
async_pool_t *async_pool_get_shared(void);
async_pool_t *async_pool_create(void);
void async_pool_free(async_pool_t *const pool);
void async_pool_enter(async_pool_t *const pool);
void async_pool_leave(async_pool_t *const pool);
async_worker_t *async_pool_get_worker(void);

#endif
