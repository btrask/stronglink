#include <assert.h>
#include "../src/common.h"
#include "../src/async.h"
#include "../deps/sqlite/sqlite3.h"

cothread_t yield = NULL;
uv_loop_t *loop = NULL;

void sqlite_async_register(void);

void test_thread(void) {
//	int err = 0;

//for(index_t i = 0; i < 20; ++i) {
/*	sqlite3 *db = NULL;
	err = sqlite3_open_v2(
		NULL, // TODO: Test real paths too.
		&db,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, // NOMUTEX / FULLMUTEX
		NULL
	);
	assert(SQLITE_OK == err);
	assert(NULL != db);*/

/*	EXEC(QUERY(db,
		"CREATE TABLE IF NOT EXISTS test (\n"
		"\t" "a INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
		"\t" "b TEXT,\n"
		"\t" "c TEXT\n"
		")"));
	EXEC(QUERY(db, "CREATE INDEX IF NOT EXISTS testIndex ON test (b ASC)"));

	for(index_t i = 0; i < 100; ++i) {
		EXEC(QUERY(db, "BEGIN TRANSACTION"));
		EXEC(QUERY(db, "SELECT * FROM test"));
		EXEC(QUERY(db, "INSERT INTO test (b, c) VALUES ('asdf', 'jkl;')"));
		EXEC(QUERY(db, "COMMIT"));
	}*/

//	err = sqlite3_close(db);
//	assert(SQLITE_OK == err);
//}
	co_terminate();
//	co_switch(yield);
}

/*static sqlite3_mutex *m1;
static sqlite3_mutex *m2;
static index_t mcount = 0;
void test_mutex(void) {
	sqlite3_mutex_enter(m1);
	++mcount;
	int err = sqlite3_mutex_try(m2);
	assert(SQLITE_OK == err);
	assert(1 == mcount);
	sqlite3_mutex_leave(m2);
	--mcount;
	sqlite3_mutex_leave(m1);
//	co_terminate();
	co_switch(yield);
}*/

#define STACK_SIZE (1024 * 1000 * sizeof(void *) / 4)
int main() {
	yield = co_active();
	loop = uv_default_loop();
//	sqlite_async_register();

//	for(index_t i = 0; i < 20; ++i) {
		co_switch(co_create(STACK_SIZE, test_thread));
//	}
	uv_run(loop, UV_RUN_DEFAULT);

/*	m1 = sqlite3_mutex_alloc(SQLITE_MUTEX_RECURSIVE);
	m2 = sqlite3_mutex_alloc(SQLITE_MUTEX_RECURSIVE);
	for(index_t i = 0; i < 10; ++i) {
		co_switch(co_create(STACK_SIZE, test_mutex));
	}
	uv_run(loop, UV_RUN_DEFAULT);*/

	uv_timer_t timer = {};
	uv_timer_init(loop, &timer);


	return 0;
}

