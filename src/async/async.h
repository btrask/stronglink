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

#ifdef LIBCO_MP
#define thread_local __thread
#else
#define thread_local
#endif

enum {
	ASYNC_CANCELED = 1 << 0,
	ASYNC_CANCELABLE = 1 << 1,
};
typedef struct {
	cothread_t fiber;
	unsigned flags;
} async_t;

extern thread_local uv_loop_t loop[1];
extern thread_local async_t *yield;

int async_init(void);

async_t *async_active(void);
int async_spawn(size_t const stack, void (*const func)(void *), void *const arg);
void async_switch(async_t *const thread);
void async_wakeup(async_t *const thread);
void async_call(void (*const func)(void *), void *const arg); // Conceptually, yields and then calls `func` from the main thread. Similar to `nextTick`.

void async_yield(void);
int async_yield_cancelable(void);
int async_yield_flags(unsigned const flags);
int async_canceled(void);
void async_cancel(async_t *const thread);


int async_random(unsigned char *const buf, size_t const len);
int async_getaddrinfo(char const *const node, char const *const service, struct addrinfo const *const hints, struct addrinfo **const res);
int async_sleep(uint64_t const milliseconds);

void async_close(uv_handle_t *const handle);

// async_stream.c
int async_read(uv_stream_t *const stream, uv_buf_t *const out);

int async_write(uv_stream_t *const stream, uv_buf_t const bufs[], unsigned const nbufs);
int async_tcp_connect(uv_tcp_t *const stream, struct sockaddr const *const addr);

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

int async_fs_writeall(uv_file const file, uv_buf_t bufs[], unsigned int const nbufs, int64_t const offset);

int async_fs_fstat(uv_file file, uv_fs_t *const req);
int async_fs_stat(const char* path, uv_fs_t *const req);

int async_fs_mkdirp_fast(char *const path, size_t const len, int const mode);
int async_fs_mkdirp(char const *const path, int const mode);
int async_fs_mkdirp_dirname(char const *const path, int const mode);
uv_file async_fs_open_mkdirp(const char* path, int flags, int mode);
int async_fs_link_mkdirp(const char* path, const char* new_path);

char *async_fs_tempnam(char const *dir, char const *prefix);

// async_sem.c
typedef struct async_thread_list async_thread_list;
typedef struct {
	async_thread_list *head;
	async_thread_list *tail;
	unsigned value;
	unsigned flags;
} async_sem_t;
void async_sem_init(async_sem_t *const sem, unsigned const value, unsigned const flags);
void async_sem_destroy(async_sem_t *const sem);
void async_sem_post(async_sem_t *const sem);
int async_sem_wait(async_sem_t *const sem);
int async_sem_trywait(async_sem_t *const sem);
int async_sem_timedwait(async_sem_t *const sem, uint64_t const future);

// async_mutex.c
typedef struct {
	async_sem_t sem[1];
	async_t *active;
	int depth;
} async_mutex_t;
void async_mutex_init(async_mutex_t *const mutex, unsigned const flags);
void async_mutex_destroy(async_mutex_t *const mutex);
int async_mutex_lock(async_mutex_t *const mutex);
int async_mutex_trylock(async_mutex_t *const mutex);
void async_mutex_unlock(async_mutex_t *const mutex);
int async_mutex_check(async_mutex_t *const mutex);

// async_rwlock.c
typedef struct {
	int state;
	async_thread_list *rdhead;
	async_thread_list *rdtail;
	async_thread_list *wrhead;
	async_thread_list *wrtail;
	async_t *upgrade;
	unsigned flags;
} async_rwlock_t;
void async_rwlock_init(async_rwlock_t *const lock, unsigned const flags);
void async_rwlock_destroy(async_rwlock_t *const lock);
void async_rwlock_rdlock(async_rwlock_t *const lock);
int async_rwlock_tryrdlock(async_rwlock_t *const lock);
void async_rwlock_rdunlock(async_rwlock_t *const lock);
void async_rwlock_wrlock(async_rwlock_t *const lock);
int async_rwlock_trywrlock(async_rwlock_t *const lock);
void async_rwlock_wrunlock(async_rwlock_t *const lock);
int async_rwlock_upgrade(async_rwlock_t *const lock);
void async_rwlock_downgrade(async_rwlock_t *const lock);

// async_cond.c
typedef struct {
	unsigned numWaiters;
	async_sem_t sem[1];
	async_mutex_t internalMutex[1];
} async_cond_t;
void async_cond_init(async_cond_t *const cond, unsigned const flags);
void async_cond_destroy(async_cond_t *const cond);
void async_cond_signal(async_cond_t *const cond);
void async_cond_broadcast(async_cond_t *const cond);
int async_cond_wait(async_cond_t *const cond, async_mutex_t *const mutex);
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
