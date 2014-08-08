#ifndef ASYNC_H
#define ASYNC_H

#include <stdio.h> /* Debugging */
#include "../deps/uv/include/uv.h"
#include "../deps/libco/libco.h"

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

int async_fs_fstat(uv_file file, uv_stat_t *stats);
int async_fs_fstat_size(uv_file file, uint64_t *size); // Avoids copying the whole stat buffer.
int async_fs_stat_mode(const char* path, uint64_t *mode);

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

// async_worker.c
typedef struct async_worker_s async_worker_t;
async_worker_t *async_worker_create(void);
void async_worker_free(async_worker_t *const worker);
void async_worker_enter(async_worker_t *const worker);
void async_worker_leave(async_worker_t *const worker);

// async_sqlite.c
void async_sqlite_register(void);

#include "../deps/sqlite/sqlite3.h" /* TODO: Don't include this here */

// async_sqlite2.c
int async_sqlite3_open_v2(async_worker_t *worker, const char *filename, sqlite3 **ppDb, int flags, const char *zVfs);
int async_sqlite3_close(async_worker_t *worker, sqlite3 *const db);

int async_sqlite3_prepare_v2(async_worker_t *worker, sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail);
int async_sqlite3_step(async_worker_t *worker, sqlite3_stmt *pStmt);
int async_sqlite3_reset(async_worker_t *worker, sqlite3_stmt *pStmt);
int async_sqlite3_clear_bindings(async_worker_t *worker, sqlite3_stmt *pStmt);
int async_sqlite3_finalize(async_worker_t *worker, sqlite3_stmt *pStmt);

int async_sqlite3_bind_int64(async_worker_t *worker, sqlite3_stmt *pStmt, int iOffset, sqlite3_int64 iValue);
int async_sqlite3_bind_text(async_worker_t *worker, sqlite3_stmt *pStmt, int iOffset, const char *zValue, int nValue, void(*cb)(void*));

const unsigned char *async_sqlite3_column_text(async_worker_t *worker, sqlite3_stmt *pStmt, int iCol);
sqlite3_int64 async_sqlite3_column_int64(async_worker_t *worker, sqlite3_stmt *pStmt, int iCol);
int async_sqlite3_column_type(async_worker_t *worker, sqlite3_stmt *pStmt, int iCol);

#endif
