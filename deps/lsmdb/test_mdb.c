#include "liblmdb/lmdb.h"
#include "test.h"

#define MDB_RDWR 0

static void test_write(MDB_env *const env, MDB_dbi const dbi) {
	uint8_t k[KEY_SIZE];
	uint8_t d[DATA_SIZE] = {};

	for(int i = 0; i < WRITES / TXN_SIZE; ++i) {
		MDB_txn *txn;
		chk( mdb_txn_begin(env, NULL, MDB_RDWR, &txn) );

		for(int j = 0; j < TXN_SIZE; ++j) {
			GENKEY(k);

			MDB_val key = { sizeof(k), &k };
			MDB_val data = { sizeof(d), &d };
			chk( mdb_put(txn, dbi, &key, &data, PUT_FLAGS) );
		}

//		chk( lsmdb_autocompact(txn) );
		mdb_txn_commit(txn);
	}
}
static void test_read(MDB_env *const env, MDB_dbi const dbi) {
	MDB_txn *txn;
	chk( mdb_txn_begin(env, NULL, MDB_RDONLY, &txn) );
	MDB_cursor *cursor;
	chk( mdb_cursor_open(txn, dbi, &cursor) );

	for(int i = 0; i < WRITES; ++i) {
		MDB_val key, data;
		chk( mdb_cursor_get(cursor, &key, &data, MDB_NEXT) );

		assert(KEY_SIZE == key.mv_size);
		chkkey(key.mv_data);
		assert(DATA_SIZE == data.mv_size);
	}

	mdb_txn_abort(txn);
}

int main(void) {
	fprintf(stderr, "%s\n", __FILE__);

	unlink("./data.mdb");
	unlink("./data.mdb-lock");

	MDB_env *env;
	chk( mdb_env_create(&env) );
	chk( mdb_env_set_mapsize(env, MAP_SIZE) );
	chk( mdb_env_open(env, "./data.mdb", MDB_NOSUBDIR | (!SYNC * MDB_NOSYNC), 0600) );

	MDB_dbi dbi;
	{
		MDB_txn *txn;
		chk( mdb_txn_begin(env, NULL, MDB_RDWR, &txn) );
		chk( mdb_dbi_open(txn, NULL, 0, &dbi) );
		chk( mdb_txn_commit(txn) );
	}

	test_write(env, dbi);
	if(READ) test_read(env, dbi);

	mdb_env_close(env);
	return 0;
}

