#include <assert.h>
#include <stdio.h> /* For debugging */
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include "../deps/sqlite/sqlite3.h"
#include "async_sqlite.h"

#define MAXPATHNAME 512

#define FILE_LOCK_MODE 4

#if FILE_LOCK_MODE==2
static async_mutex_t *lock;
#elif FILE_LOCK_MODE==3
static async_rwlock_t *lock;
#elif FILE_LOCK_MODE==4
// nothing
#else
#error "Invalid file lock mode"
#endif

#define DBG(status) ({ assert(0); status; })
//#define DBG(status) ({ fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); status; })

static sqlite3_io_methods const io_methods;

typedef struct {
	sqlite3_io_methods const *methods;
	uv_file file;
} async_file;

static int async_open(sqlite3_vfs *const vfs, char const *const inpath, async_file *const file, int const sqflags, int *const outFlags) {
	int uvflags = 0;
	int const usetmp = !inpath;
	if(sqflags & SQLITE_OPEN_EXCLUSIVE || usetmp) uvflags |= O_EXCL;
	if(sqflags & SQLITE_OPEN_CREATE || usetmp) uvflags |= O_CREAT;
	if(sqflags & SQLITE_OPEN_READWRITE) uvflags |= O_RDWR;
	if(sqflags & SQLITE_OPEN_READONLY) uvflags |= O_RDONLY;
	if(usetmp) uvflags |= O_TRUNC;

	for(;;) {
		char *tmp = usetmp ? async_fs_tempnam(NULL, "async-sqlite") : NULL;
		char const *const path = usetmp ? tmp : inpath;
		uv_file const result = async_fs_open(path, uvflags, 0600);
		if(!usetmp) {
			if(result < 0) return DBG(SQLITE_CANTOPEN);
			file->file = result;
			break;
		}
		if(-EEXIST == result) {
			free(tmp); tmp = NULL;
			continue;
		} else if(result < 0) {
			free(tmp); tmp = NULL;
			return DBG(SQLITE_CANTOPEN);
		}
		file->file = result;
		async_fs_unlink(path); // TODO: Is this safe on Windows?
		free(tmp); tmp = NULL;
		break;
	}
	file->methods = &io_methods;
	return SQLITE_OK;
}
static int async_delete(sqlite3_vfs *const vfs, char const *const path, int const syncDir) {
	int const unlinkresult = async_fs_unlink(path);
	if(-ENOENT == unlinkresult) return SQLITE_OK;
	if(unlinkresult < 0) return DBG(SQLITE_IOERR_DELETE);
	if(syncDir) {
		char dirname[MAXPATHNAME+1];
		sqlite3_snprintf(MAXPATHNAME+1, dirname, "%s", path);
		unsigned i = strlen(dirname);
		for(; i > 1 && '/' != dirname[i]; ++i);
		dirname[i] = '\0';

		uv_file const dir = async_fs_open(dirname, O_RDWR, 0600);
		if(dir < 0) return DBG(SQLITE_IOERR_DELETE);
		int const syncresult = async_fs_fsync(dir);
		async_fs_close(dir);
		if(syncresult < 0) return DBG(SQLITE_IOERR_DELETE);
	}
	return SQLITE_OK;
}
static int async_access(sqlite3_vfs *const vfs, char const *const path, int const flags, int *const outRes) {
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
	if('/' == path[0]) {
		sqlite3_snprintf(size, outPath, "%s", path);
	} else {
		char dir[MAXPATHNAME+1] = "";
		size_t len = MAXPATHNAME;
		if(uv_cwd(dir, &len) < 0) return DBG(SQLITE_IOERR);
		sqlite3_snprintf(size, outPath, "%s/%s", dir, path);
	}
	return SQLITE_OK;
}
static int async_randomness(sqlite3_vfs *const vfs, int const size, char *const buf) {
	assert(size >= 0 && "Invalid random buffer size");
	if(0 == SQLITE_ERROR) return SQLITE_OK;
	if(async_random((unsigned char *)buf, size) < 0) return DBG(SQLITE_ERROR);
	return SQLITE_OK;
}
static int async_sleep_sqlite(sqlite3_vfs *const vfs, int const microseconds) {
	assert(microseconds >= 0 && "Invalid sleep duration");
	if(async_sleep(microseconds / 1000) < 0) return DBG(SQLITE_ERROR);
	return microseconds;
}
static int async_currentTimeInt64(sqlite3_vfs *const vfs, sqlite3_int64 *const piNow) {
	// From sqlite/os_unix.c
	static const sqlite3_int64 unixEpoch = 24405875*(sqlite3_int64)8640000;
	int rc = SQLITE_OK;
	struct timeval sNow;
	if( gettimeofday(&sNow, 0)==0 ){
		*piNow = unixEpoch + 1000*(sqlite3_int64)sNow.tv_sec + sNow.tv_usec/1000;
	}else{
		rc = DBG(SQLITE_ERROR);
	}
	return rc;
}
static int async_currentTime(sqlite3_vfs *const vfs, double *const prNow) {
	// From sqlite/os_unix.c
	sqlite3_int64 i = 0;
	int rc;
	rc = async_currentTimeInt64(NULL, &i);
	*prNow = i/86400000.0;
	return rc;
}
static int async_getLastError(sqlite3_vfs *const vfs, int const size, char *const msg) {
	sqlite3_snprintf(size, msg, "testing");
	return SQLITE_OK;
}


static sqlite3_vfs async_vfs = {
	.iVersion = 3,            /* Structure version number (currently 3) */
	.szOsFile = sizeof(async_file),            /* Size of subclassed sqlite3_file */
	.mxPathname = MAXPATHNAME,          /* Maximum file pathname length */
	.pNext = NULL,      /* Next registered VFS */
	.zName = "async_vfs",       /* Name of this virtual file system */
	.pAppData = NULL,          /* Pointer to application-specific data */

	// V1
	.xOpen = (int (*)())async_open,
	.xDelete = async_delete,
	.xAccess = async_access,
	.xFullPathname = async_fullPathname,
	.xDlOpen = NULL,
	.xDlError = NULL,
	.xDlSym = NULL,
	.xDlClose = NULL,
	.xRandomness = async_randomness,
	.xSleep = async_sleep_sqlite,
	.xCurrentTime = async_currentTime,
	.xGetLastError = async_getLastError,

	// V2
	.xCurrentTimeInt64 = async_currentTimeInt64,

	// V3
	.xSetSystemCall = NULL,
	.xGetSystemCall = NULL,
	.xNextSystemCall = NULL,
};

static int async_close(async_file *const file) {
	if(async_fs_close(file->file) < 0) return DBG(SQLITE_IOERR);
	return SQLITE_OK;
}
static int async_read(async_file *const file, void *const buf, int const len, sqlite3_int64 const offset) {
	uv_buf_t info = uv_buf_init((char *)buf, len);
	ssize_t const result = async_fs_read(file->file, &info, 1, offset);
	if(result < 0) return DBG(SQLITE_IOERR_READ);
	if(result < len) {
		memset(buf+result, 0, len-result); // Under threat of database corruption.
		return SQLITE_IOERR_SHORT_READ;
	}
	return SQLITE_OK;
}
static int async_write(async_file *const file, void const *const buf, int const len, sqlite3_int64 const offset) {
	uv_buf_t info = uv_buf_init((char *)buf, len);
	ssize_t const result = async_fs_write(file->file, &info, 1, offset);
	if(result != len) return DBG(SQLITE_IOERR_WRITE);
	return SQLITE_OK;
}
static int async_truncate(async_file *const file, sqlite3_int64 const size) {
	uv_fs_t req = { .data = co_active() };
	uv_fs_ftruncate(loop, &req, file->file, size, async_fs_cb);
	co_switch(yield);
	int const result = req.result;
	uv_fs_req_cleanup(&req);
	if(result < 0) return DBG(SQLITE_IOERR_TRUNCATE);
	return SQLITE_OK;
}
static int async_sync(async_file *const file, int const flags) {
	int result = -1;
	if(flags & SQLITE_SYNC_DATAONLY) {
		result = async_fs_fdatasync(file->file);
	} else {
		result = async_fs_fsync(file->file);
	}
	if(result < 0) return DBG(SQLITE_IOERR_FSYNC);
	return SQLITE_OK;
}
static int async_fileSize(async_file *const file, sqlite3_int64 *const outSize) {
	uv_fs_t req = { .data = co_active() };
	uv_fs_fstat(loop, &req, file->file, async_fs_cb);
	co_switch(yield);
	*outSize = req.statbuf.st_size;
	int const result = req.result;
	uv_fs_req_cleanup(&req);
	if(result < 0) return DBG(SQLITE_IOERR);
	return SQLITE_OK;
}
static int async_lock(async_file *const file, int const level) {
#if FILE_LOCK_MODE==2
	if(level <= SQLITE_LOCK_NONE) return SQLITE_OK;
	if(async_mutex_check(lock)) return SQLITE_OK;
	async_mutex_lock(lock);
	return SQLITE_OK;
#elif FILE_LOCK_MODE==3
	switch(level) {
		case SQLITE_LOCK_NONE:
			return SQLITE_OK;
		case SQLITE_LOCK_SHARED:
			if(async_rwlock_wrcheck(lock)) return SQLITE_OK;
			if(async_rwlock_rdcheck(lock)) return SQLITE_OK;
			async_rwlock_rdlock(lock);
			return SQLITE_OK;
		case SQLITE_LOCK_RESERVED:
		case SQLITE_LOCK_PENDING:
		case SQLITE_LOCK_EXCLUSIVE:
			if(async_rwlock_wrcheck(lock)) return SQLITE_OK;
			if(async_rwlock_rdcheck(lock)) {
				if(async_rwlock_upgrade(lock) < 0) return SQLITE_BUSY;
				return SQLITE_OK;
			}
			async_rwlock_wrlock(lock);
			return SQLITE_OK;
		default:
			assert(0 && "Unknown lock mode");
			return 0;
	}
#elif FILE_LOCK_MODE==4
	return SQLITE_OK;
#endif
}
static int async_unlock(async_file *const file, int const level) {
#if FILE_LOCK_MODE==2
	if(level > SQLITE_LOCK_NONE) return SQLITE_OK;
	if(!async_mutex_check(lock)) return SQLITE_OK;
	async_mutex_unlock(lock);
	return SQLITE_OK;
#elif FILE_LOCK_MODE==3
	switch(level) {
		case SQLITE_LOCK_EXCLUSIVE:
		case SQLITE_LOCK_PENDING:
		case SQLITE_LOCK_RESERVED:
			return SQLITE_OK;
		case SQLITE_LOCK_SHARED:
			if(async_rwlock_wrcheck(lock)) {
				if(async_rwlock_downgrade(lock) < 0) {
					assert(0 && "Non-recursive lock downgrade should always succeed");
					return SQLITE_BUSY;
				}
				return SQLITE_OK;
			}
			return SQLITE_OK;
		case SQLITE_LOCK_NONE:
			if(async_rwlock_wrcheck(lock)) {
				async_rwlock_wrunlock(lock);
				return SQLITE_OK;
			}
			if(async_rwlock_rdcheck(lock)) {
				async_rwlock_rdunlock(lock);
				return SQLITE_OK;
			}
			return SQLITE_OK;
		default:
			assert(0 && "Unknown lock mode");
			return 0;
	}
#elif FILE_LOCK_MODE==4
	return SQLITE_OK;
#endif
}
static int async_checkReservedLock(async_file *const file, int *const outRes) {
#if FILE_LOCK_MODE==2
	*outRes = async_mutex_check(lock);
#elif FILE_LOCK_MODE==3
	*outRes = async_rwlock_wrcheck(lock);
#elif FILE_LOCK_MODE==4
	*outRes = 1;
#endif
	return SQLITE_OK;
}
static int async_fileControl(async_file *const file, int op, void *pArg) {
	return SQLITE_OK;
}
static int async_sectorSize(async_file *const file) {
	return 0;
}
static int async_deviceCharacteristics(async_file *const file) {
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

#define GLOBAL_MUTEX_COUNT 10
static async_mutex_t **global_mutexes;

static async_mutex_t *async_mutex_create_sqlite(int const type) {
	if(type > SQLITE_MUTEX_RECURSIVE) {
		assert(type < GLOBAL_MUTEX_COUNT && "Unknown static mutex");
		return global_mutexes[type - SQLITE_MUTEX_RECURSIVE - 1];
	}
	return async_mutex_create();
}
static int async_mutex_trylock_sqlite(async_mutex_t *const mutex) {
	if(async_mutex_trylock(mutex) < 0) return SQLITE_BUSY;
	return SQLITE_OK;
}
static int async_mutex_held(async_mutex_t *const mutex) {
	if(!mutex) return 1;
	return async_mutex_check(mutex);
}
static int async_mutex_notheld(async_mutex_t *const mutex) {
	if(!mutex) return 1;
	return !async_mutex_check(mutex);
}

static int async_mutex_init(void) {
	if(global_mutexes) return SQLITE_OK;
	global_mutexes = calloc(GLOBAL_MUTEX_COUNT, sizeof(async_mutex_t *));
	for(unsigned i = 0; i < GLOBAL_MUTEX_COUNT; ++i) {
		global_mutexes[i] = async_mutex_create_sqlite(SQLITE_MUTEX_RECURSIVE);
	}
	return SQLITE_OK;
}
static int async_mutex_end(void) {
	if(!global_mutexes) return SQLITE_OK;
	for(unsigned i = 0; i < GLOBAL_MUTEX_COUNT; ++i) {
		async_mutex_free(global_mutexes[i]); global_mutexes[i] = NULL;
	}
	free(global_mutexes); global_mutexes = NULL;
	return SQLITE_OK;
}

static sqlite3_mutex_methods const async_mutex_methods = {
	.xMutexInit = (int (*)(void))async_mutex_init,
	.xMutexEnd = (int (*)(void))async_mutex_end,
	.xMutexAlloc = (sqlite3_mutex *(*)(int))async_mutex_create_sqlite,
	.xMutexFree = (void (*)(sqlite3_mutex *))async_mutex_free,
	.xMutexEnter = (void (*)(sqlite3_mutex *))async_mutex_lock,
	.xMutexTry = (int (*)(sqlite3_mutex *))async_mutex_trylock_sqlite,
	.xMutexLeave = (void (*)(sqlite3_mutex *))async_mutex_unlock,
	.xMutexHeld = (int (*)(sqlite3_mutex *))async_mutex_held,
	.xMutexNotheld = (int (*)(sqlite3_mutex *))async_mutex_notheld,
};

static int use_unlock_notify = 0;
void async_sqlite_register(void) {
	int err = 0;
	err = sqlite3_config(SQLITE_CONFIG_MUTEX, &async_mutex_methods);
	assert(SQLITE_OK == err && "Couldn't enable async_sqlite mutex");
#if FILE_LOCK_MODE==2
	lock = async_mutex_create();
	assert(lock && "File lock creation failed");
#elif FILE_LOCK_MODE==3
	lock = async_rwlock_create();
	assert(lock && "File lock creation failed");
#elif FILE_LOCK_MODE==4
	err = sqlite3_enable_shared_cache(1);
	assert(SQLITE_OK == err && "Couldn't enable SQLite shared cache");
	use_unlock_notify = 1;
#endif
	err = sqlite3_vfs_register(&async_vfs, 1);
	assert(SQLITE_OK == err && "Couldn't register async_sqlite VFS");
}

static void async_unlock_notify_cb(cothread_t *threads, int const count) {
	for(int i = 0; i < count; ++i) {
		async_wakeup(threads[i]);
	}
}
int async_sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nSql, sqlite3_stmt **ppStmt, const char **pz) {
	for(;;) {
		int status = sqlite3_prepare_v2(db, zSql, nSql, ppStmt, pz);
		if(SQLITE_LOCKED != status) return status;
		if(!use_unlock_notify) return SQLITE_LOCKED;
		status = sqlite3_unlock_notify(db, async_unlock_notify_cb, co_active());
		if(SQLITE_OK != status) return status;
		co_switch(yield);
	}
}
int async_sqlite3_step(sqlite3_stmt *const stmt) {
	for(;;) {
		int status = sqlite3_step(stmt);
		if(SQLITE_LOCKED != status) return status;
		if(!use_unlock_notify) return SQLITE_LOCKED;
		sqlite3 *const db = sqlite3_db_handle(stmt);
		status = sqlite3_unlock_notify(db, async_unlock_notify_cb, co_active());
		if(SQLITE_OK != status) return status;
		co_switch(yield);
		sqlite3_reset(stmt);
	}
}

