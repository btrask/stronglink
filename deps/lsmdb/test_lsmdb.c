#include "lsmdb.h"
#include "test.h"

//#define MDB_RDWR 0

static void test_write(LSMDB_env *const env) {
	uint8_t k[KEY_SIZE];
	uint8_t d[DATA_SIZE] = {};

	for(int i = 0; i < WRITES / TXN_SIZE; ++i) {
		LSMDB_txn *txn;
		chk( lsmdb_txn_begin(env, NULL, MDB_RDWR, &txn) );

		for(int j = 0; j < TXN_SIZE; ++j) {
			GENKEY(k);

			MDB_val key = { sizeof(k), &k };
			MDB_val data = { sizeof(d), &d };
			chk( lsmdb_put(txn, &key, &data, PUT_FLAGS) );
		}

		chk( lsmdb_autocompact(txn) );
		lsmdb_txn_commit(txn);
	}
}
static void test_read(LSMDB_env *const env) {
	LSMDB_txn *txn;
	chk( lsmdb_txn_begin(env, NULL, MDB_RDONLY, &txn) );
	LSMDB_cursor *cursor;
	chk( lsmdb_cursor_open(txn, &cursor) );

	for(int i = 0; i < WRITES; ++i) {
		MDB_val key, data;
		chk( lsmdb_cursor_next(cursor, &key, &data, +1) );

		assert(KEY_SIZE == key.mv_size);
		chkkey(key.mv_data);
		assert(DATA_SIZE == data.mv_size);
	}

	lsmdb_txn_abort(txn);
}

int main(void) {
	fprintf(stderr, "%s\n", __FILE__);

	unlink("./data.lsmdb");
	unlink("./data.lsmdb-lock");

	LSMDB_env *env;
	chk( lsmdb_env_create(&env) );
	chk( lsmdb_env_set_mapsize(env, MAP_SIZE) );
	chk( lsmdb_env_open(env, "./data.lsmdb", MDB_NOSUBDIR | (!SYNC * MDB_NOSYNC), 0600) );

/*	MDB_dbi dbi;
	{
		MDB_txn *txn;
		chk( mdb_txn_begin(env, NULL, MDB_RDWR, &txn) );
		chk( mdb_dbi_open(txn, NULL, 0, &dbi) );
		chk( mdb_txn_commit(txn) );
	}*/

	test_write(env);
	if(READ) test_read(env);

	lsmdb_env_close(env);
	return 0;
}

