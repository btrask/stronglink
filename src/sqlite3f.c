#include <assert.h>
#include <stdio.h> /* Debugging */
#include "sqlite3f.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

sqlite3f *sqlite3f_create(sqlite3 *const conn) {
	sqlite3f *db = sqlite3_malloc(sizeof(sqlite3f));
	if(!db) return NULL;
	db->conn = conn;
	db->head = &db->cache[0];
	for(int i = 0; i < STATEMENT_CACHE_SIZE; ++i) {
		db->cache[i].sql = NULL;
		db->cache[i].stmt = NULL;
		db->cache[i].next = &db->cache[i+1];
		db->cache[i].flags = 0;
	}
	db->tail = &db->cache[STATEMENT_CACHE_SIZE-1];
	db->tail->next = NULL;
	return db;
}
void sqlite3f_close(sqlite3f *const db) {
	if(!db) return;
	sqlite3f_stmt *cur = db->head;
	while(cur) {
		cur->sql = NULL;
		sqlite3_finalize(cur->stmt); cur->stmt = NULL;
		cur = cur->next;
	}
	db->head = NULL;
	db->tail = NULL;
	sqlite3_close(db->conn); db->conn = NULL;
	sqlite3_free(db);
}

int sqlite3f_prepare_v2(sqlite3f *const db, char const *const sql, int const len, sqlite3_stmt **const stmt) {
	sqlite3f_stmt *last = NULL;
	sqlite3f_stmt *cur = db->head;
	for(;;) {
		if(sql == cur->sql) {
			if(last) {
				last->next = cur->next;
				cur->next = db->head;
				db->head = cur;
				if(db->tail == cur) {
					db->tail = last;
				}
			}
			*stmt = cur->stmt;
			return SQLITE_OK;
		}
		if(!cur->next) break;
		last = cur;
		cur	= cur->next;
	}
	int const result = sqlite3_prepare_v2(db->conn, sql, len, stmt, NULL);
	if(SQLITE_OK != result) return result;
	assert(db->tail != last && "Didn't retain list item");
	assert(last->next == db->tail && "Didn't reach last item");
	last->next = NULL;
	sqlite3_finalize(cur->stmt);
	cur->sql = sql;
	cur->stmt = *stmt;
	cur->next = db->head;
	cur->flags = 0;
	db->head = cur;
	db->tail = last;
	return SQLITE_OK;
}
int sqlite3f_finalize(sqlite3_stmt *const stmt) {
	// TODO: Track which statements are in use. If more than the cached number of statements are needed at once, create "detached" statements.
	sqlite3_clear_bindings(stmt);
	return sqlite3_reset(stmt);
}

