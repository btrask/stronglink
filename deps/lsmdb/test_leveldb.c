#include <leveldb/c.h>
#include "test.h"

static void test_write(leveldb_t *const db) {
	char *err = NULL;

	leveldb_writeoptions_t *const wopts = leveldb_writeoptions_create();
	assert(wopts);
	leveldb_writeoptions_set_sync(wopts, SYNC);

	uint8_t k[KEY_SIZE];
	uint8_t d[DATA_SIZE] = {};

	for(int i = 0; i < WRITES / TXN_SIZE; ++i) {
		leveldb_writebatch_t *const batch = leveldb_writebatch_create();
		assert(batch);

		for(int j = 0; j < TXN_SIZE; ++j) {
			GENKEY(k);

			leveldb_writebatch_put(batch, (char *)k, sizeof(k), (char *)d, sizeof(d));

		}

		err = NULL;
		leveldb_write(db, wopts, batch, &err);
		if(err) fprintf(stderr, "%s\n", err);
		assert(!err);
	}

	leveldb_writeoptions_destroy(wopts);
}
static void test_read(leveldb_t *const db) {
	leveldb_readoptions_t *const ropts = leveldb_readoptions_create();

	assert(0); // TODO

	leveldb_readoptions_destroy(ropts);
}


int main(void) {
	fprintf(stderr, "%s\n", __FILE__);

	char *err;

	leveldb_options_t *const opts = leveldb_options_create();
	assert(opts);
	leveldb_options_set_create_if_missing(opts, 1);
	leveldb_options_set_error_if_exists(opts, 1);
	leveldb_options_set_compression(opts, leveldb_no_compression);

	err = NULL;
	leveldb_destroy_db(opts, "./data.leveldb/", &err);
	if(err) fprintf(stderr, "%s\n", err);
	assert(!err);

	err = NULL;
	leveldb_t *const db = leveldb_open(opts, "./data.leveldb/", &err);
	if(err) fprintf(stderr, "%s\n", err);
	assert(!err);
	assert(db);

	test_write(db);
	if(READ) test_read(db);

	leveldb_close(db);
	leveldb_options_destroy(opts);
	return 0;
}

