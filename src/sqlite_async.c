#include <unistd.h>
#include "../deps/sqlite/sqlite3.h"
#include "common.h"
#include "async.h"

#define DEBUG_LOG() //fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define MAXPATHNAME 512

#define FILE_LOCK_MODE 2

#if FILE_LOCK_MODE==0
#elif FILE_LOCK_MODE==1
#define QUEUE_MAX 10
static cothread_t queue[QUEUE_MAX];
static index_t queue_start = 0;
static count_t queue_length = 0;
#elif FILE_LOCK_MODE==2
static sqlite3_mutex *lock;
#else
#error "Invalid file lock mode"
#endif

static sqlite3_io_methods const io_methods;

typedef struct {
	sqlite3_io_methods const *methods;
	uv_file file;
} async_file;

static int async_open(sqlite3_vfs *const vfs, char const *const inpath, async_file *const file, int const sqflags, int *const outFlags) {
	DEBUG_LOG();
	int uvflags = 0;
	bool_t const usetmp = !inpath;
	if(sqflags & SQLITE_OPEN_EXCLUSIVE || usetmp) uvflags |= O_EXCL;
	if(sqflags & SQLITE_OPEN_CREATE || usetmp) uvflags |= O_CREAT;
	if(sqflags & SQLITE_OPEN_READWRITE) uvflags |= O_RDWR;
	if(sqflags & SQLITE_OPEN_READONLY) uvflags |= O_RDONLY;
	if(usetmp) uvflags |= O_TRUNC;

	uv_fs_t req = { .data = co_active() };
	for(;;) {
		char *const tmp = usetmp ? tempnam(NULL, "async") : NULL; // TODO: Blocking, unportable, etc.
		char const *const path = usetmp ? tmp : inpath;
		uv_fs_open(loop, &req, path, uvflags, 0600, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);
		if(!usetmp) {
			if(req.result < 0) return SQLITE_CANTOPEN;
			file->file = req.result;
			break;
		}
		if(-EEXIST == req.result) {
			free(tmp);
			continue;
		} else if(req.result < 0) {
			free(tmp);
			return SQLITE_CANTOPEN;
		}
		file->file = req.result;
		uv_fs_unlink(loop, &req, path, async_fs_cb); // TODO: Is this safe on Windows?
		co_switch(yield);
		uv_fs_req_cleanup(&req);
		free(tmp);
		break;
	}
	file->methods = &io_methods;
	return SQLITE_OK;
}
static int async_delete(sqlite3_vfs *const vfs, char const *const path, int const syncDir) {
	DEBUG_LOG();
	uv_fs_t req = { .data = co_active() };
	uv_fs_unlink(loop, &req, path, async_fs_cb);
	co_switch(yield);
	int const unlinkresult = req.result;
	uv_fs_req_cleanup(&req);
	if(ENOENT == unlinkresult) return SQLITE_OK;
	if(unlinkresult < 0) return SQLITE_IOERR_DELETE;
	if(syncDir) {
		str_t dirname[MAXPATHNAME+1];
		sqlite3_snprintf(MAXPATHNAME+1, dirname, "%s", path);
		index_t i = strlen(dirname);
		for(; i > 1 && '/' != dirname[i]; ++i);
		dirname[i] = '\0';

		uv_fs_open(loop, &req, dirname, O_RDWR, 0600, async_fs_cb);
		co_switch(yield);
		uv_file const dir = req.result;
		uv_fs_req_cleanup(&req);
		if(dir < 0) return SQLITE_IOERR_DELETE;

		uv_fs_fsync(loop, &req, dir, async_fs_cb);
		co_switch(yield);
		int const syncresult = req.result;
		uv_fs_req_cleanup(&req);

		uv_fs_close(loop, &req, dir, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);

		if(syncresult < 0) return SQLITE_IOERR_DELETE;
	}
	return SQLITE_OK;
}
static int async_access(sqlite3_vfs *const vfs, char const *const path, int const flags, int *const outRes) {
	DEBUG_LOG();
	uv_fs_t req = { .data = co_active() };
	uv_fs_stat(loop, &req, path, async_fs_cb);
	co_switch(yield);
	switch(flags) {
		case SQLITE_ACCESS_EXISTS:
			*outRes = req.result >= 0;
			break;
		case SQLITE_ACCESS_READWRITE:
			*outRes = 0600 == (req.statbuf.st_mode & 0600);
			break;
		case SQLITE_ACCESS_READ:
			*outRes = 0400 == (req.statbuf.st_mode & 0400);
			break;
	}
	uv_fs_req_cleanup(&req);
	return SQLITE_OK;
}
static int async_fullPathname(sqlite3_vfs *const vfs, char const *const path, int const size, char *const outPath) {
	DEBUG_LOG();
	char dir[MAXPATHNAME+1];
	size_t len = sizeof(dir);
	if('/' == path[0]) dir[0] = '\0';
	else if(0 == uv_cwd(dir, &len)) return SQLITE_IOERR;
	dir[MAXPATHNAME] = '\0';
	sqlite3_snprintf(size, outPath, "%s/%s", dir, path);
	outPath[size-1] = '\0';
	return SQLITE_OK;
}
static int async_randomness(sqlite3_vfs *const vfs, int const size, char *const buf) {
	DEBUG_LOG();
	// TODO
	return SQLITE_OK;
}
static int async_sleep(sqlite3_vfs *const vfs, int const microseconds) {
	DEBUG_LOG();
	uv_timer_t timer = { .data = co_active() };
	uv_timer_init(loop, &timer);
	uv_timer_start(&timer, async_timer_cb, microseconds / 1000, 0);
	co_switch(yield);
	uv_close((uv_handle_t *)&timer, async_close_cb);
	co_switch(yield);
	return microseconds;
}
static int async_currentTime(sqlite3_vfs *const vfs, double *const outTime) {
	DEBUG_LOG();
/* TODO
** This implementation is not very good. The current time is rounded to
** an integer number of seconds. Also, assuming time_t is a signed 32-bit 
** value, it will stop working some time in the year 2038 AD (the so-called
** "year 2038" problem that afflicts systems that store time this way). 
*/
	time_t t = time(NULL);
	*outTime = t/86400.0 + 2440587.5; 
	return SQLITE_OK;
}
static int async_getLastError(sqlite3_vfs *const vfs, int const size, char *const msg) {
	DEBUG_LOG();
	sqlite3_snprintf(size, msg, "testing");
	return SQLITE_OK;
}


static sqlite3_vfs async_vfs = {
	.iVersion = 1,            /* Structure version number (currently 3) */
	.szOsFile = sizeof(async_file),            /* Size of subclassed sqlite3_file */
	.mxPathname = MAXPATHNAME,          /* Maximum file pathname length */
	.pNext = NULL,      /* Next registered VFS */
	.zName = "async_vfs",       /* Name of this virtual file system */
	.pAppData = NULL,          /* Pointer to application-specific data */
	.xOpen = (int (*)())async_open,
	.xDelete = async_delete,
	.xAccess = async_access,
	.xFullPathname = async_fullPathname,
	.xDlOpen = NULL,
	.xDlError = NULL,
	.xDlSym = NULL,
	.xDlClose = NULL,
	.xRandomness = async_randomness,
	.xSleep = async_sleep,
	.xCurrentTime = async_currentTime,
	.xGetLastError = async_getLastError,
	/*
	** The methods above are in version 1 of the sqlite_vfs object
	** definition.  Those that follow are added in version 2 or later
	*/
//	int (*xCurrentTimeInt64)(sqlite3_vfs*, sqlite3_int64*);
	/*
	** The methods above are in versions 1 and 2 of the sqlite_vfs object.
	** Those below are for version 3 and greater.
	*/
//	int (*xSetSystemCall)(sqlite3_vfs*, const char *zName, sqlite3_syscall_ptr);
//	sqlite3_syscall_ptr (*xGetSystemCall)(sqlite3_vfs*, const char *zName);
//	const char *(*xNextSystemCall)(sqlite3_vfs*, const char *zName);
	/*
	** The methods above are in versions 1 through 3 of the sqlite_vfs object.
	** New fields may be appended in figure versions.  The iVersion
	** value will increment whenever this happens. 
	*/
};

static int async_close(async_file *const file) {
	DEBUG_LOG();
	uv_fs_t req = { .data = co_active() };
	uv_fs_close(loop, &req, file->file, async_fs_cb);
	co_switch(yield);
	int const result = req.result;
	uv_fs_req_cleanup(&req);
	if(result < 0) return SQLITE_IOERR;
	return SQLITE_OK;
}
static int async_read(async_file *const file, void *const buf, int const len, sqlite3_int64 const offset) {
	DEBUG_LOG();
	uv_buf_t info = uv_buf_init((char *)buf, len);
	uv_fs_t req = { .data = co_active() };
	uv_fs_read(loop, &req, file->file, &info, 1, offset, async_fs_cb);
	co_switch(yield);
	int const result = req.result;
	uv_fs_req_cleanup(&req);
	if(result < 0) return SQLITE_IOERR_READ;
	if(result < len) {
		memset(buf+result, 0, len-result); // Under threat of database corruption.
		return SQLITE_IOERR_SHORT_READ;
	}
	return SQLITE_OK;
}
static int async_write(async_file *const file, void const *const buf, int const len, sqlite3_int64 const offset) {
	DEBUG_LOG();
	uv_buf_t info = uv_buf_init((char *)buf, len);
	uv_fs_t req = { .data = co_active() };
	uv_fs_write(loop, &req, file->file, &info, 1, offset, async_fs_cb);
	co_switch(yield);
	int const result = req.result;
	uv_fs_req_cleanup(&req);
	if(result != len) return SQLITE_IOERR_WRITE;
	return SQLITE_OK;
}
static int async_truncate(async_file *const file, sqlite3_int64 const size) {
	DEBUG_LOG();
	uv_fs_t req = { .data = co_active() };
	uv_fs_ftruncate(loop, &req, file->file, size, async_fs_cb);
	co_switch(yield);
	int const result = req.result;
	uv_fs_req_cleanup(&req);
	if(result < 0) return SQLITE_IOERR_TRUNCATE;
	return SQLITE_OK;
}
static int async_sync(async_file *const file, int const flags) {
	DEBUG_LOG();
	uv_fs_t req = { .data = co_active() };
	if(flags & SQLITE_SYNC_DATAONLY) {
		uv_fs_fdatasync(loop, &req, file->file, async_fs_cb);
	} else {
		uv_fs_fsync(loop, &req, file->file, async_fs_cb);
	}
	co_switch(yield);
	int const result = req.result;
	uv_fs_req_cleanup(&req);
	if(result < 0) return SQLITE_IOERR_FSYNC;
	return SQLITE_OK;
}
static int async_fileSize(async_file *const file, sqlite3_int64 *const outSize) {
	DEBUG_LOG();
	uv_fs_t req = { .data = co_active() };
	uv_fs_fstat(loop, &req, file->file, async_fs_cb);
	co_switch(yield);
	*outSize = req.statbuf.st_size;
	int const result = req.result;
	uv_fs_req_cleanup(&req);
	if(result < 0) return SQLITE_IOERR;
	return SQLITE_OK;
}
static int async_lock(async_file *const file, int const level) {
	DEBUG_LOG();
#if FILE_LOCK_MODE==0
	return SQLITE_OK;
#elif FILE_LOCK_MODE==1
	if(level <= SQLITE_LOCK_NONE) return SQLITE_OK;
	if(queue_length && co_active() == queue[queue_start]) return SQLITE_OK;
	if(queue_length >= QUEUE_MAX) return SQLITE_BUSY;
	if(!queue_length) {
		// TODO: flock() or equivalent, for other processes.
	}
	queue[(queue_start + queue_length) % QUEUE_MAX] = co_active();
	if(queue_length++) co_switch(yield);
	return SQLITE_OK;
#elif FILE_LOCK_MODE==2
	if(level <= SQLITE_LOCK_NONE) return SQLITE_OK;
	if(sqlite3_mutex_held(lock)) return SQLITE_OK;
	sqlite3_mutex_enter(lock);
	return SQLITE_OK;
//	return sqlite3_mutex_try(lock);
#endif
}
static int async_unlock(async_file *const file, int const level) {
	DEBUG_LOG();
#if FILE_LOCK_MODE==0
	return SQLITE_OK;
#elif FILE_LOCK_MODE==1
	if(level > SQLITE_LOCK_NONE) return SQLITE_OK;
	if(!queue_length) return SQLITE_OK;
	if(co_active() != queue[queue_start]) return SQLITE_OK;
	queue_length = queue_length - 1;
	queue_start = (queue_start + 1) % QUEUE_MAX;
	if(!queue_length) {
		// TODO: Unlock file.
	} else {
		async_wakeup(queue[queue_start]);
	}
	return SQLITE_OK;
#elif FILE_LOCK_MODE==2
	if(level > SQLITE_LOCK_NONE) return SQLITE_OK;
	if(sqlite3_mutex_notheld(lock)) return SQLITE_OK;
	sqlite3_mutex_leave(lock);
	return SQLITE_OK;
#endif
}
static int async_checkReservedLock(async_file *const file, int *const outRes) {
	DEBUG_LOG();
#if FILE_LOCK_MODE==0
	*outRes = 0;
#elif FILE_LOCK_MODE==1
	*outRes = queue_length && co_active() == queue[queue_start];
#elif FILE_LOCK_MODE==2
	*outRes = sqlite3_mutex_held(lock);
#endif
	return SQLITE_OK;
}
static int async_fileControl(async_file *const file, int op, void *pArg) {
	DEBUG_LOG();
	return SQLITE_OK;
}
static int async_sectorSize(async_file *const file) {
	DEBUG_LOG();
	return 0;
}
static int async_deviceCharacteristics(async_file *const file) {
	DEBUG_LOG();
	return 0;
}


static sqlite3_io_methods const io_methods = {
	.iVersion = 1,
	.xClose = (int (*)())async_close,
	.xRead = (int (*)())async_read,
	.xWrite = (int (*)())async_write,
	.xTruncate = (int (*)())async_truncate,
	.xSync = (int (*)())async_sync,
	.xFileSize = (int (*)())async_fileSize,
	.xLock = (int (*)())async_lock,
	.xUnlock = (int (*)())async_unlock,
	.xCheckReservedLock = (int (*)())async_checkReservedLock,
	.xFileControl = (int (*)())async_fileControl,
	.xSectorSize = (int (*)())async_sectorSize,
	.xDeviceCharacteristics = (int (*)())async_deviceCharacteristics,
	/* Methods above are valid for version 1 */
//	int (*xShmMap)(sqlite3_file*, int iPg, int pgsz, int, void volatile**);
//	int (*xShmLock)(sqlite3_file*, int offset, int n, int flags);
//	void (*xShmBarrier)(sqlite3_file*);
//	int (*xShmUnmap)(sqlite3_file*, int deleteFlag);
	/* Methods above are valid for version 2 */
//	int (*xFetch)(sqlite3_file*, sqlite3_int64 iOfst, int iAmt, void **pp);
//	int (*xUnfetch)(sqlite3_file*, sqlite3_int64 iOfst, void *p);
	/* Methods above are valid for version 3 */
	/* Additional methods may be added in future releases */
};

typedef struct {
	int dbgtype;
	index_t current;
	count_t count;
	count_t size;
	count_t recursion;
	cothread_t *queue;
} async_mutex;

#define GLOBAL_MUTEX_COUNT 10
static async_mutex **global_mutexes;

static async_mutex *async_mutexAlloc(int const type) {
	if(type > SQLITE_MUTEX_RECURSIVE) {
		BTAssert(type < GLOBAL_MUTEX_COUNT, "Unknown static mutex %d", type);
		return global_mutexes[type - SQLITE_MUTEX_RECURSIVE - 1];
	}
	async_mutex *const m = calloc(1, sizeof(async_mutex));
	m->dbgtype = type;
	m->current = 0;
	m->count = 0;
	m->size = 10;
	m->recursion = 0;
	m->queue = calloc(m->size, sizeof(cothread_t));
	return m;
}
static void async_mutexFree(async_mutex *const m) {
	BTAssert(!m->count, "Mutex freed while held %p", m);
	free(m);
}
static void async_mutexEnter(async_mutex *const m) {
	cothread_t const active = co_active();
	if(m->count && active == m->queue[m->current]) {
		++m->recursion;
		return;
	}
	if(++m->count > m->size) {
		BTAssert(0, "Mutex queue growth not yet implemented");
		// TODO: Grow.
	}
	m->queue[(m->current + m->count - 1) % m->size] = active;
	if(1 == m->count) {
		m->recursion = 1;
		return;
	}
	co_switch(yield);
}
static int async_mutexTry(async_mutex *const m) {
	DEBUG_LOG();
//	return SQLITE_BUSY;
	if(m->count && co_active() != m->queue[m->current]) return SQLITE_BUSY;
	async_mutexEnter(m);
	return SQLITE_OK;
}
static void async_mutexLeave(async_mutex *const m) {
	cothread_t const active = co_active();
	BTAssert(m->count, "Leaving empty mutex %p", m);
	BTAssert(active == m->queue[m->current], "Leaving someone else's mutex %p", m);
	if(--m->recursion) return;
	m->current = (m->current + 1) % m->size;
	if(!--m->count) return;
	cothread_t const next = m->queue[m->current];
	async_wakeup(next);
}
static int async_mutexHeld(async_mutex *const m) {
//	DEBUG_LOG();
	if(!m) return 1;
	return m->count && co_active() == m->queue[m->current];
}
static int async_mutexNotheld(async_mutex *const m) {
//	DEBUG_LOG();
	if(!m) return 1;
	return !m->count || co_active() != m->queue[m->current];
}

static int async_mutexInit(void) {
	DEBUG_LOG();
	if(global_mutexes) return SQLITE_OK;
	global_mutexes = calloc(GLOBAL_MUTEX_COUNT, sizeof(async_mutex *));
	for(index_t i = 0; i < GLOBAL_MUTEX_COUNT; ++i) {
		global_mutexes[i] = async_mutexAlloc(SQLITE_MUTEX_RECURSIVE);
	}
	return SQLITE_OK;
}
static int async_mutexEnd(void) {
	DEBUG_LOG();
	if(!global_mutexes) return SQLITE_OK;
	for(index_t i = 0; i < GLOBAL_MUTEX_COUNT; ++i) {
		async_mutexFree(global_mutexes[i]);
	}
	FREE(&global_mutexes);
	return SQLITE_OK;
}

static sqlite3_mutex_methods const async_mutex_methods = {
	.xMutexInit = (int (*)(void))async_mutexInit,
	.xMutexEnd = (int (*)(void))async_mutexEnd,
	.xMutexAlloc = (sqlite3_mutex *(*)(int))async_mutexAlloc,
	.xMutexFree = (void (*)(sqlite3_mutex *))async_mutexFree,
	.xMutexEnter = (void (*)(sqlite3_mutex *))async_mutexEnter,
	.xMutexTry = (int (*)(sqlite3_mutex *))async_mutexTry,
	.xMutexLeave = (void (*)(sqlite3_mutex *))async_mutexLeave,
	.xMutexHeld = (int (*)(sqlite3_mutex *))async_mutexHeld,
	.xMutexNotheld = (int (*)(sqlite3_mutex *))async_mutexNotheld,
};

void sqlite_async_register(void) {
	BTSQLiteErr(sqlite3_config(SQLITE_CONFIG_MUTEX, &async_mutex_methods));
#if FILE_LOCK_MODE==0
#elif FILE_LOCK_MODE==1
#elif FILE_LOCK_MODE==2
	lock = sqlite3_mutex_alloc(SQLITE_MUTEX_RECURSIVE);
#endif
	BTSQLiteErr(sqlite3_vfs_register(&async_vfs, 1));
}

