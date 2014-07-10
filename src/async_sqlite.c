#include <assert.h>
#include <stdio.h> /* TODO: Use async_fs instead */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../deps/sqlite/sqlite3.h"
#include "async.h"

#define MAXPATHNAME 512

#define FILE_LOCK_MODE 3

#if FILE_LOCK_MODE==2
static async_mutex_t *lock;
#elif FILE_LOCK_MODE==3
static async_rwlock_t *lock;
#else
#error "Invalid file lock mode"
#endif

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
		char *const tmp = usetmp ? async_tempnam(NULL, "async-sqlite") : NULL;
		char const *const path = usetmp ? tmp : inpath;
		int const result = async_fs_open(path, uvflags, 0600);
		if(!usetmp) {
			if(result < 0) return SQLITE_CANTOPEN;
			file->file = result;
			break;
		}
		if(-EEXIST == result) {
			free(tmp);
			continue;
		} else if(result < 0) {
			free(tmp);
			return SQLITE_CANTOPEN;
		}
		file->file = result;
		async_fs_unlink(path); // TODO: Is this safe on Windows?
		free(tmp);
		break;
	}
	file->methods = &io_methods;
	return SQLITE_OK;
}
static int async_delete(sqlite3_vfs *const vfs, char const *const path, int const syncDir) {
	int const unlinkresult = async_fs_unlink(path);
	if(ENOENT == unlinkresult) return SQLITE_OK;
	if(unlinkresult < 0) return SQLITE_IOERR_DELETE;
	if(syncDir) {
		char dirname[MAXPATHNAME+1];
		sqlite3_snprintf(MAXPATHNAME+1, dirname, "%s", path);
		unsigned i = strlen(dirname);
		for(; i > 1 && '/' != dirname[i]; ++i);
		dirname[i] = '\0';

		uv_file const dir = async_fs_open(dirname, O_RDWR, 0600);
		if(dir < 0) return SQLITE_IOERR_DELETE;
		int const syncresult = async_fs_fsync(dir);
		async_fs_close(dir);
		if(syncresult < 0) return SQLITE_IOERR_DELETE;
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
		if(uv_cwd(dir, &len) < 0) return SQLITE_IOERR;
		sqlite3_snprintf(size, outPath, "%s/%s", dir, path);
	}
	return SQLITE_OK;
}
static int async_randomness(sqlite3_vfs *const vfs, int const size, char *const buf) {
	assert(size >= 0 && "Invalid random buffer size");
	if(0 == SQLITE_ERROR) return SQLITE_OK;
	if(async_random((unsigned char *)buf, size) < 0) return SQLITE_ERROR;
	return SQLITE_OK;
}
static int async_sleep_sqlite(sqlite3_vfs *const vfs, int const microseconds) {
	assert(microseconds >= 0 && "Invalid sleep duration");
	if(async_sleep(microseconds / 1000) < 0) return SQLITE_ERROR;
	return microseconds;
}
static int async_currentTime(sqlite3_vfs *const vfs, double *const outTime) {
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
	.xSleep = async_sleep_sqlite,
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
	if(async_fs_close(file->file) < 0) return SQLITE_IOERR;
	return SQLITE_OK;
}
static int async_read(async_file *const file, void *const buf, int const len, sqlite3_int64 const offset) {
	uv_buf_t info = uv_buf_init((char *)buf, len);
	ssize_t const result = async_fs_read(file->file, &info, 1, offset);
	if(result < 0) return SQLITE_IOERR_READ;
	if(result < len) {
		memset(buf+result, 0, len-result); // Under threat of database corruption.
		return SQLITE_IOERR_SHORT_READ;
	}
	return SQLITE_OK;
}
static int async_write(async_file *const file, void const *const buf, int const len, sqlite3_int64 const offset) {
	uv_buf_t info = uv_buf_init((char *)buf, len);
	ssize_t const result = async_fs_write(file->file, &info, 1, offset);
	if(result != len) return SQLITE_IOERR_WRITE;
	return SQLITE_OK;
}
static int async_truncate(async_file *const file, sqlite3_int64 const size) {
	uv_fs_t req = { .data = co_active() };
	uv_fs_ftruncate(loop, &req, file->file, size, async_fs_cb);
	co_switch(yield);
	int const result = req.result;
	uv_fs_req_cleanup(&req);
	if(result < 0) return SQLITE_IOERR_TRUNCATE;
	return SQLITE_OK;
}
static int async_sync(async_file *const file, int const flags) {
	int result = -1;
	if(flags & SQLITE_SYNC_DATAONLY) {
		result = async_fs_fdatasync(file->file);
	} else {
		result = async_fs_fsync(file->file);
	}
	if(result < 0) return SQLITE_IOERR_FSYNC;
	return SQLITE_OK;
}
static int async_fileSize(async_file *const file, sqlite3_int64 *const outSize) {
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
#endif
}
static int async_checkReservedLock(async_file *const file, int *const outRes) {
#if FILE_LOCK_MODE==2
	*outRes = async_mutex_check(lock);
#elif FILE_LOCK_MODE==3
	*outRes = async_rwlock_wrcheck(lock);
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
		async_mutex_free(global_mutexes[i]);
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

void async_sqlite_register(void) {
	int err = sqlite3_config(SQLITE_CONFIG_MUTEX, &async_mutex_methods);
	assert(SQLITE_OK == err && "SQLite custom mutexes couldn't be set");
#if FILE_LOCK_MODE==2
	lock = async_mutex_create();
	assert(lock && "File lock creation failed");
#elif FILE_LOCK_MODE==3
	lock = async_rwlock_create();
	assert(lock && "File lock creation failed");
#endif
	err = sqlite3_vfs_register(&async_vfs, 1);
	assert(SQLITE_OK == err && "SQLite custom VFS couldn't be registered");
}

