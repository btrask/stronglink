
#define STATEMENT_CACHE_SIZE 16

typedef struct sqlite3f_stmt sqlite3f_stmt;

typedef struct {
	char const *sql;
	sqlite3_stmt *stmt;
	sqlite3f_stmt *next;
} sqlite3f_stmt;

typedef struct {
	sqlite3 *conn;
	sqlite3f_stmt *head;
	sqlite3f_stmt *tail;
	sqlite3f_stmt cache[STATEMENT_CACHE_SIZE];
} sqlite3f;

sqlite3f sqlite3f_create(sqlite3 *const conn) {
	sqlite3f *db = calloc(1, sizeof(sqlite3f));
	if(!db) return NULL;
	db->conn = conn;
	db->head = &db->cache[0];
	db->tail = &db->cache[STATEMENT_CACHE_SIZE-1];
	for(int i = 0; i < STATEMENT_CACHE_SIZE-1; ++i) {
		db->cache[i].next = &db->cache[i+1];
	}
	return db;
}
void sqlite3f_free(sqlite3f **const dbptr) {
	sqlite3f *db = *dbptr;
	if(!db) return;
	sqlite3f_stmt *cur = db->head;
	while(cur) {
		cur->sql = NULL;
		sqlite3_finalize(cur->stmt); cur->stmt = NULL;
		cur = cur->next;
	}
	db->head = NULL;
	db->tail = NULL;
	FREE(&db->cache);
	sqlite3_close(conn); conn = NULL;
	FREE(dbptr); db = NULL;
}

int sqlite3f_prepare_v2(sqlite3f *const db, char const *const sql, int const len, sqlite3_stmt **const stmt) {
	sqlite3f_stmt *last = NULL;
	sqlite3f_stmt *cur = db->head;
	while(cur) {
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
		last = cur;
		cur	= cur->next;
	}
	int const result = sqlite3_prepare_v2(db->conn, sql, len, stmt, NULL);
	if(SQLITE_OK != result) return result;
	sqlite3_finalize(db->tail->stmt);
	db->tail->sql = sql;
	db->tail->stmt = *stmt;
	db->tail->next = db->head;
	db->head = db->tail;
	db->tail = last;
	last->next = NULL;
	return SQLITE_OK;
}
int sqlite3f_finalize(sqlite3_stmt *const stmt) {
	sqlite3_clear_bindings(stmt);
	return sqlite3_reset(stmt);
}

