
typedef struct async_sqlite3_pool async_sqlite3_pool;
typedef struct async_sqlite3

#include "async.h"

typedef enum {
	t_prepare,
	t_step,
} req_type;

typedef struct {
	req_type type;
	
} prepare_req;
typedef struct {
	req_type type;
	
} step_req;

typedef union {
	prepare_req prepare;
	step_req step;
} request;

typedef struct {
	uv_thread_t *thread;
	request *req;
	uv_cond_t worker;
	uv_async_t main;
} worker_state;

struct async_sqlite3_pool {
	unsigned count;
	async_sqlite3 **connections;
	uv_thread_t **threads;
};
struct async_sqlite3 {
	sqlite3 *connection;
};




int async_sqlite3_prepare_v2(async_sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail) {

}



