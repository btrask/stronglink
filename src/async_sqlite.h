#include "../deps/sqlite/sqlite3.h"
#include "async.h"

void async_sqlite_register(void);

// These versions just use sqlite3_unlock_notify to automatically handle SQLITE_LOCKED. They can still be used even when async_sqlite isn't the active VFS (although in that case they won't know how to handle SQLITE_LOCKED errors).
int async_sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nSql, sqlite3_stmt **ppStmt, const char **pz);
int async_sqlite3_step(sqlite3_stmt *const stmt);

