#include "../deps/sqlite/sqlite3.h"

#define STATEMENT_CACHE_SIZE 32

typedef struct sqlite3f_stmt sqlite3f_stmt;

struct sqlite3f_stmt {
	char const *sql;
	sqlite3_stmt *stmt;
	sqlite3f_stmt *next;
	unsigned flags; // TODO: Statement detaching
};

typedef struct {
	sqlite3 *conn;
#if STATEMENT_CACHE_SIZE > 0
	sqlite3f_stmt *head;
	sqlite3f_stmt *tail;
	sqlite3f_stmt cache[STATEMENT_CACHE_SIZE];
#endif
} sqlite3f;

sqlite3f *sqlite3f_create(sqlite3 *const conn);
void sqlite3f_close(sqlite3f *const db);
int sqlite3f_prepare_v2(sqlite3f *const db, char const *const sql, int const len, sqlite3_stmt **const stmt);
int sqlite3f_finalize(sqlite3_stmt *const stmt);

