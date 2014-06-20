#include <assert.h>
#include "../src/common.h"
#include "../src/async.h"
#include "../deps/sqlite/sqlite3.h"

cothread_t yield = NULL;
uv_loop_t *loop = NULL;

void sqlite_async_register(void);

void test_thread(void) {
	int err = 0;

	sqlite3 *db = NULL;
	err = sqlite3_open_v2(
		NULL, // TODO: Test real paths too.
		&db,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, // NOMUTEX / FULLMUTEX
		NULL
	);
	assert(SQLITE_OK == err);
	assert(NULL != db);

	EXEC(QUERY(db,
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
	}

	err = sqlite3_close(db);
	assert(SQLITE_OK == err);

	co_terminate();
}

int main() {
	yield = co_active();
	loop = uv_default_loop();
	sqlite_async_register();

	for(index_t i = 0; i < 100; ++i) {
		co_switch(co_create(1024 * 50 * sizeof(void *) / 4, test_thread));
	}

	uv_run(loop, UV_RUN_DEFAULT);

	return 0;
}

