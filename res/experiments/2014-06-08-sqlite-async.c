
struct async_file {
	sqlite3_io_methods const *methods;
	sqlite3_file *file;
};

typedef struct {
	sqlite3_vfs *vfs;
	uv_loop_t *loop;
	cothread_t yield;
} async_global;
static async_global gdata = {};

static sqlite3_vfs gvfs = {
	.iVersion = 1,
	.szOsFile = sizeof(async_file),
	.mxPathname = 0,
	.pNext = NULL,
	.zName = "sqlite-async",
	.pAppData = &gdata,
	int (*xOpen)(sqlite3_vfs*, const char *zName, sqlite3_file*,
		       int flags, int *pOutFlags);
	int (*xDelete)(sqlite3_vfs*, const char *zName, int syncDir);
	int (*xAccess)(sqlite3_vfs*, const char *zName, int flags, int *pResOut);
	int (*xFullPathname)(sqlite3_vfs*, const char *zName, int nOut, char *zOut);
	void *(*xDlOpen)(sqlite3_vfs*, const char *zFilename);
	void (*xDlError)(sqlite3_vfs*, int nByte, char *zErrMsg);
	void (*(*xDlSym)(sqlite3_vfs*,void*, const char *zSymbol))(void);
	void (*xDlClose)(sqlite3_vfs*, void*);
	int (*xRandomness)(sqlite3_vfs*, int nByte, char *zOut);
	int (*xSleep)(sqlite3_vfs*, int microseconds);
	int (*xCurrentTime)(sqlite3_vfs*, double*);
	int (*xGetLastError)(sqlite3_vfs*, int, char *);
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

static sqlite3_io_methods const io_methods = {
	.iVersion = 1,
	int (*xClose)(sqlite3_file*);
	int (*xRead)(sqlite3_file*, void*, int iAmt, sqlite3_int64 iOfst);
	int (*xWrite)(sqlite3_file*, const void*, int iAmt, sqlite3_int64 iOfst);
	int (*xTruncate)(sqlite3_file*, sqlite3_int64 size);
	int (*xSync)(sqlite3_file*, int flags);
	int (*xFileSize)(sqlite3_file*, sqlite3_int64 *pSize);
	int (*xLock)(sqlite3_file*, int);
	int (*xUnlock)(sqlite3_file*, int);
	int (*xCheckReservedLock)(sqlite3_file*, int *pResOut);
	int (*xFileControl)(sqlite3_file*, int op, void *pArg);
	int (*xSectorSize)(sqlite3_file*);
	int (*xDeviceCharacteristics)(sqlite3_file*);
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
}

int async_initialize(char const *const vfsName, int const makeDefault) {
	sqlite3_vfs *const vfs = sqlite3_vfs_find(vfsName);
	if(!vfs) return SQLITE_ERROR;
	gdata.vfs = vfs;
	gdata.loop = uv_loop_default();
	gdata.yield = co_active();
	async_vfs.mxPathname = vfs->mxPathname;
	sqlite3_vfs_register(&async_vfs, makeDefault);
	return SQLITE_OK;
}

typedef struct {

} argsOpen;
static int workerOpen(uv_worker_t *const req) {
	
}
static int asyncOpen(sqlite3_vfs*, const char *zName, sqlite3_file*, int flags, int *pOutFlags) {
	
}
static int asyncDelete(sqlite3_vfs*, const char *zName, int syncDir) {
	
}

