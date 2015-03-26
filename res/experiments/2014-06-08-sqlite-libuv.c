#include <unistd.h>
#include <uv.h>

#define MAXPATHNAME 512

#define QUEUE_MAX 10
static cothread_t queue[QUEUE_MAX];
static index_t queue_start = 0;
static count_t queue_length = 0;

typedef struct {
	uv_loop_t *loop;
	cothread_t yield;
} squvco_thread;

static void fs_cb(uv_fs_t *const req) {
	co_switch(req->data);
}
static void timer_cb(uv_timer_t *const timer) {
	co_switch(timer->data);
}

static int squvco_open(sqlite3_vfs *const vfs, char const *const path, sqlite3_file *const file, int const sqflags, int *const outFlags) {
	int uvflags = 0;
	if(!path) return SQLITE_IOERR;
	if(sqflags & SQLITE_OPEN_EXCLUSIVE) uvflags |= O_EXCL;
	if(sqflags & SQLITE_OPEN_CREATE) uvflags |= O_CREAT;
	if(sqflags & SQLITE_OPEN_READWRITE) uvflags |= O_RDWR;
	if(sqflags & SQLITE_OPEN_READONLY) uvflags |= O_RDONLY;
	uv_fs_t req = { .data = co_active() };
	uv_fs_open(vfs->pAppData->loop, &req, path, uvflags, 0600, fs_cb);
	co_switch(vfs->pAppData->yield);
	int const result = req.result;
	uv_fs_req_cleanup(&req);
	if(result < 0) return SQLITE_CANTOPEN;
	file->methods = &io_methods;
	file->file = result;
	file->thread = vfs->pAppData;
	file->lockLevel = SQLITE_LOCK_NONE;
	return SQLITE_OK;
}
static int squvco_delete(sqlite3_vfs *const vfs, char const *const path, int const syncDir) {
	uv_fs_t req = { .data = co_active() };
	uv_fs_unlink(vfs->pAppData->loop, &req, path, fs_cb);
	co_switch(vs->pAppData->yield);
	int const unlinkresult = req.result;
	uv_fs_req_cleanup(&req);
	if(ENOENT == unlinkresult) return SQLITE_OK;
	if(unlinkresult < 0) return SQLITE_IOERR_DELETE;
	if(syncDir) {
		str_t dirname[MAXPATHNAME+1];
		sqlite3_snprintf(MAXPATHNAME+1, dirname, "%s", path);
		index_t i = strlen(dirname);
		for(; i > 1 && '/' != dir[i]; ++i);
		dirname[i] = '\0';

		uv_fs_open(vfs->pAppData->loop, &req, dirname, fs_cb);
		co_switch(vfs->pAppData->yield);
		uv_file const dir = req.result;
		uv_fs_req_cleanup(&req);
		if(dir < 0) return SQLITE_IOERR_DELETE;

		uv_fs_fsync(vfs->pAppData->loop, &req, dir, fs_cb);
		co_switch(vfs->pAppData->yield);
		int const syncresult = req.result;
		uv_fs_req_cleanup(&req);

		uv_fs_close(vfs->pAppData->loop, &req, dir, fs_cb);
		co_switch(vfs->pAppData->yield);
		uv_fs_req_cleanup(&req);

		if(syncresult < 0) return SQLITE_IOERR_DELETE;
	}
	return SQLITE_OK;
}
static int squvco_access(sqlite3_vfs *const vfs, char const *const path, int const flags, int *const outRes) {
	uv_fs_t req = { .data = co_active() };
	uv_fs_stat(vfs->pAppData->loop, &req, path, fs_cb);
	co_switch(vfs->pAppData->yield);
	switch(flags) {
		case SQLITE_ACCESS_EXISTS:
			*outRes = res.result >= 0;
			return SQLITE_OK;
		case SQLITE_ACCESS_READWRITE:
			*outRes = 0600 == (req.statbuf.st_mode & 0600);
			return SQLITE_OK;
		case SQLITE_ACCESS_READ:
			*outRes = 0400 == (req.statbuf.st_mode & 0600);
			return SQLITE_OK;
		default:
			return SQLITE_IOERR_ACCESS;
	}
}
static int squvco_fullPathname(sqlite3_vfs *const vfs, char const *const path, int const size, char *const outPath) {
	char dir[MAXPATHNAME+1];
	if('/' == path[0]) dir[0] = '\0';
	else if(0 == uv_cwd(dir, sizeof(dir))) return SQLITE_IOERR;
	dir[MAXPATHNAME] = '\0';
	sqlite3_snprintf(size, outPath, "%s/%s", dir, path);
	outPath[size-1] = '\0';
	return SQLITE_OK;
}
static int squvco_randomness(sqlite3_vfs *const vfs, int const size, char *const buf) {
	// TODO
	return SQLITE_OK;
}
static int squvco_sleep(sqlite3_vfs *const vfs, int const microseconds) {
	uv_timer_t timer = { .data = co_active() };
	uv_timer_init(vfs->pAppData->loop, &timer);
	uv_timer_start(&timer, timer_cb, microseconds / 1000, 0);
	co_switch(vfs->pAppData->yield);
	uv_timer_stop(&timer);
	return microseconds;
}
static int squvco_currentTime(sqlite3_vfs *const vfs, double *const outTime) {
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
static int squvco_getLastError(sqlite3_vfs *const vfs, int const size, char *const msg) {
	sqlite3_snprintf(size, msg, "testing");
	return SQLITE_OK;
}


struct squvco_file {
	sqlite3_io_methods const *methods;
	uv_file file;
	squvco_thread thread;
};


static sqlite3_vfs const squvco_vfs = {
	.iVersion = 1,            /* Structure version number (currently 3) */
	.szOsFile = sizeof(squvco_file),            /* Size of subclassed sqlite3_file */
	.mxPathname = MAXPATHNAME,          /* Maximum file pathname length */
	.pNext = NULL,      /* Next registered VFS */
	.zName = "squvco_vfs",       /* Name of this virtual file system */
	.pAppData = NULL;          /* Pointer to application-specific data */
	.xOpen = squvco_open,
	.xDelete = squvco_delete,
	.xAccess = squvco_access,
	.xFullPathname = squvco_fullPathname,
	.xDlOpen = NULL,
	.xDlError = NULL,
	.xDlSym = NULL,
	.xDlClose = NULL,
	.xRandomness = squvco_randomness,
	.xSleep = squvco_sleep,
	.xCurrentTime = squvco_currentTime,
	.xGetLastError = squvco_getLastError,
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

static int squvco_close(squvco_file *const file) {
	assert(SQLITE_LOCK_NONE == file->lockLevel);
	uv_fs_t req = { .data = co_active() };
	uv_fs_close(file->thread->loop, &req, file->file, fs_cb);
	co_switch(file->thread->yield);
	uv_fs_req_cleanup(&req);
	int const result = req.result;
	uv_fs_req_cleanup(&req);
	if(result < 0) return SQLITE_IOERR;
	return SQLITE_OK;
}
static int squvco_read(squvco_file *const file, void *const buf, int const len, sqlite3_int64 const offset) {
	uv_buf_t info = uv_buf_init(buf, len);
	uv_fs_t req = { .data = co_active() };
	uv_fs_read(file->thread->loop, &req, file->file, &info, 1, offset, fs_cb);
	co_switch(file->thread->yield);
	int const result = req.result;
	uv_fs_req_cleanup(&req);
	if(result < 0) return SQLITE_IOERR_READ;
	if(result < len) {
		memset(buf+result, 0, len-result); // Under threat of database corruption.
		return SQLITE_IOERR_SHORT_READ;
	}
	return SQLITE_OK;
}
static int squvco_write(squvco_file *const file, void const *const buf, int const len, sqlite3_int64 const offset) {
	uv_buf_t info = uv_buf_init(buf, len);
	uv_fs_t req = { .data = co_active() };
	uv_fs_write(file->thread->loop, &req, file->file, &info, 1, offset, fs_cb);
	co_switch(file->thread->yield);
	int const result = req.result;
	uv_fs_req_cleanup(&req);
	if(result != len) return SQLITE_IOERR_WRITE;
	return SQLITE_OK;
}
static int squvco_truncate(squvco_file *const file, sqlite3_int64 const size) {
	uv_fs_t req = { .data = co_active() };
	uv_fs_ftruncate(file->thread->loop, &req, file->file, size, fs_cb);
	co_switch(file->thread->yield);
	return req.result;
}
static int squvco_sync(squvco_file *const file, int const flags) {
	uv_fs_t req = { .data = co_active() };
	if(flags & SQLITE_SYNC_DATAONLY) {
		uv_fs_fdatasync(file->thread->loop, &req, file->file, fs_cb);
	} else {
		uv_fs_fsync(file->thread->loop, &req, file->file, fs_cb);
	}
	co_switch(file->thread->yield);
	int const result = req.result;
	uv_fs_req_cleanup(&req);
	if(result < 0) return SQLITE_IOERR_TRUNCATE;
	return SQLITE_OK;
}
static int squvco_fileSize(squvco_file *const file, sqlite3_int64 *const outSize) {
	uv_fs_t req = { .data = co_active() };
	uv_fs_fstat(file->thread->loop, &req, file->file, fs_cb);
	co_switch(file->thread->yield);
	*outSize = req.statbuf.st_size;
	int const result = req.result;
	uv_fs_req_cleanup(&req);
	if(result < 0) return SQLITE_IOERR;
	return SQLITE_OK;
}
static int squvco_lock(squvco_file *const file, int const level) {
	if(SQLITE_LOCK_NONE == level) return SQLITE_OK;
	if(!queue_length) {
		// TODO: flock() or equivalent, for other processes.
	}
	if(queue_length >= QUEUE_MAX) return SQLITE_BUSY;
	queue[(queue_start + queue_length) % QUEUE_MAX] = co_active();
	if(queue_length++) co_switch(file->thread->yield);
	return SQLITE_OK;
}
static int squvco_unlock(squvco_file *const file, int const level) {
	if(SQLITE_LOCK_NONE != level) return SQLITE_OK;
	if(!queue_length) return SQLITE_MISUSE;
	if(co_active() != queue[queue_start]) return SQLITE_MISUSE;
	queue_length = queue_length - 1;
	queue_start = (queue_start + 1) % QUEUE_MAX;
	if(!queue_length) {
		// TODO: Unlock file.
	}
	return SQLITE_OK;
}
static int squvco_checkReservedLock(squvco_file *const file, int *const outRes) {
	*outRes = queue_length && co_active() == queue[queue_start];
	return SQLITE_OK;
}
static int xFileControl(squvco_file *const file, int op, void *pArg) {
	return SQLITE_OK;
}
static int xSectorSize(squvco_file *const file) {
	return 0;
}
static int xDeviceCharacteristics(squvco_file *const file) {
	return 0;
}


static sqlite3_io_methods const io_methods = {
	.iVersion = 1,
	.xClose = squvco_close,
	.xRead = squvco_read,
	.xWrite = squvco_write,
	.xTruncate = squvco_truncate,
	.xSync = squvco_sync,
	.xFileSize = squvco_fileSize,
	.xLock = squvco_lock,
	.xUnlock = squvco_unlock,
	.xCheckReservedLock = squvco_checkReservedLock,
	.xFileControl = squvco_fileControl,
	.xSectorSize = squvco_sectorSize,
	.xDeviceCharacteristics = squvco_deviceCharacteristics,
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

