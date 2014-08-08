#include "../deps/sqlite/sqlite3.h"
#include "async.h"

int async_sqlite3_open_v2(async_worker_t *worker, const char *filename, sqlite3 **ppDb, int flags, const char *zVfs) {
	async_worker_enter(worker);
	int const rc = sqlite3_open_v2(filename, ppDb, flags, zVfs);
	async_worker_leave(worker);
	return rc;
}
int async_sqlite3_close(async_worker_t *worker, sqlite3 *const db) {
	async_worker_enter(worker);
	int const rc = sqlite3_close(db);
	async_worker_leave(worker);
	return rc;
}

int async_sqlite3_prepare_v2(async_worker_t *worker, sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail) {
	async_worker_enter(worker);
	int const rc = sqlite3_prepare_v2(db, zSql, nByte, ppStmt, pzTail);
	async_worker_leave(worker);
	return rc;
}
int async_sqlite3_step(async_worker_t *worker, sqlite3_stmt *pStmt) {
	async_worker_enter(worker);
	int const rc = sqlite3_step(pStmt);
	async_worker_leave(worker);
	return rc;
}
int async_sqlite3_reset(async_worker_t *worker, sqlite3_stmt *pStmt) {
//	async_worker_enter(worker);
	int const rc = sqlite3_reset(pStmt);
//	async_worker_leave(worker);
	return rc;
}
int async_sqlite3_clear_bindings(async_worker_t *worker, sqlite3_stmt *pStmt) {
//	async_worker_enter(worker);
	int const rc = sqlite3_clear_bindings(pStmt);
//	async_worker_leave(worker);
	return rc;
}
int async_sqlite3_finalize(async_worker_t *worker, sqlite3_stmt *pStmt) {
//	async_worker_enter(worker);
	int const rc = sqlite3_finalize(pStmt);
//	async_worker_leave(worker);
	return rc;
}

int async_sqlite3_bind_int64(async_worker_t *worker, sqlite3_stmt *pStmt, int iOffset, sqlite3_int64 iValue) {
//	async_worker_enter(worker);
	int const rc = sqlite3_bind_int64(pStmt, iOffset, iValue);
//	async_worker_leave(worker);
	return rc;
}
int async_sqlite3_bind_text(async_worker_t *worker, sqlite3_stmt *pStmt, int iOffset, const char *zValue, int nValue, void(*cb)(void*)) {
//	async_worker_enter(worker);
	int const rc = sqlite3_bind_text(pStmt, iOffset, zValue, nValue, cb);
//	async_worker_leave(worker);
	return rc;
}

const unsigned char *async_sqlite3_column_text(async_worker_t *worker, sqlite3_stmt *pStmt, int iCol) {
//	async_worker_enter(worker);
	const unsigned char *const val = sqlite3_column_text(pStmt, iCol);
//	async_worker_leave(worker);
	return val;
}
sqlite3_int64 async_sqlite3_column_int64(async_worker_t *worker, sqlite3_stmt *pStmt, int iCol) {
//	async_worker_enter(worker);
	sqlite3_int64 const val = sqlite3_column_int64(pStmt, iCol);
//	async_worker_leave(worker);
	return val;
}
int async_sqlite3_column_type(async_worker_t *worker, sqlite3_stmt *pStmt, int iCol) {
//	async_worker_enter(worker);
	int const val = sqlite3_column_type(pStmt, iCol);
//	async_worker_leave(worker);
	return val;
}

