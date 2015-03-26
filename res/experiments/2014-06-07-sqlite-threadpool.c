#include <uv.h>

typedef struct SQLThreadPool* SQLThreadPoolRef;

struct SQLThreadPool {
	count_t count;
	uv_thread_t threads[0];
};

SQLThreadPoolRef SQLThreadPoolCreate(count_t const threads) {
	if(!threads) return NULL;
	SQLThreadPoolRef const pool = calloc(1, sizeof(struct SQLThreadPool));
	
	return pool;
}

#define POOL_SIZE 10

struct DBPool {
	uv_thread_t threads[POOL_SIZE];
}

void thread_cb(void *const arg) {
	sqlite3 *db = NULL;
	(void)BTSQLiteErr(sqlite3_open_v2(
		repo->DBPath,
		&db,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX,
		NULL
	));

}

DBPoolRef DBPoolCreate(strarg_t const path) {
	DBPoolRef const pool = calloc(1, sizeof(struct DBPool));
	for(index_t i = 0; i < POOL_SIZE; ++i) {
		uv_thread_create(&pool->threads[i], thread_cb, path);
	}
	return pool;
}











void DBExec...





typedef struct Query* QueryRef;

typedef enum {
	QueryArgText,
	QueryArgInteger,
} QueryArgType;
typedef struct {
	QueryArgType type;
	union {
		str_t *text;
		int64_t integer;
	} data;
} QueryArg;
typedef struct {
	count_t count;
	count_t size;
	QueryArg items[0];
} QueryArgList;
struct Query {
	str_t *statement;
	QueryArgList *args;
};


QueryRef QueryCreate(strarg_t const statement) {
	QueryRef const query = calloc(1, sizeof(struct Query));
	query->statement = strdup(statement);
}
void QueryFree(QueryRef const query) {
	if(!query) return;
	FREE(&query->statement);
	if(query->args) for(index_t i = 0; i < query->args->count; ++i) {
		QueryArgFree(&query->args->items[i]);
	}
	FREE(&query->args);
	free(query);
}
err_t QueryAddArgText(QueryRef const query, strarg_t const text) {
	if(!query) return 0;
	QueryArg *const arg = QueryGetNextArg(query);
	if(!arg) return -1;
	arg->type = QueryArgText;
	arg->data.text = strdup(text);
	return 0;
}
err_t QueryAddArgInteger(QueryRef const query, int64_t const integer) {
	if(!query) return 0;
	QueryArg *const arg = QueryGetNextArg(query);
	if(!arg) return -1;
	arg->type = QueryArgInteger;
	arg->data.integer = integer;
	return 0;
}

sqlite3_stmt *QueryPrepare(QueryRef const query, sqlite3 *const db) {
	if(!query) return NULL;
	sqlite3_stmt *stmt = NULL;
	if(BTSQLiteErr(sqlite3_prepare_v2(db, query->statement, -1, &stmt, NULL))) return -1;
	QueryArgsList *const args = query->args;
	if(args) for(index_t i = 0; i < args->count; ++i) {
		QueryArg *const arg = &args->items[i];
		switch(arg->type) {
			case QueryArgText:
				(void)BTSQLiteErr(sqlite3_bind_text(stmt, i+1, arg->data.text, -1, SQLITE3_TRANSIENT));
				break;
			case QueryArgInteger:
				(void)BTSQLiteErr(sqlite3_bind_int64(stmt, i+1, arg->data.integer));
				break;
		}
	}
	return stmt;
}


static QueryArg *QueryGetNextArg(QueryRef const query) {
	if(!query) return NULL;
	QueryArgList *list = query->args;
	count_t count = list ? list->count : 0;
	count_t size = list ? list->size : 0;
	if(count+1 > size) {
		size = MAX(4, MAX(count+1, size * 2));
		list = realloc(list, sizeof(QueryArgList) + sizeof(QueryArg) * size);
		if(!list) return NULL;
		list->size = size;
	}
	return &query->args->items[query->args->count++];
}
static void QueryArgFree(QueryArg *const arg) {
	switch(arg->type) {
		case QueryArgText:
			FREE(&arg->data.text);
			break;
	}
}



thread *EFSRepoGetThread(QueryRef const query) {
	for(threads in list) {
		if(thread inactive) {
			mark thread as active
			return thread;
		}
	}
}



void EFSRepoDBThread(EFSRepoRef const repo) {
	sqlite3 *db = NULL;
	(void)BTSQLiteErr(sqlite3_open_v2(
		repo->DBPath,
		&db,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX,
		NULL
	));

	for(;;) {



		QueryRef const query = ...;
		sqlite3_stmt *const stmt = QueryPrepare(query);
		if(!stmt) {
			continue;
		}



	}


	(void)BTSQLiteErr(sqlite3_close(db));
}








