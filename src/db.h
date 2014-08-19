#include "../deps/liblmdb/lmdb.h"
#include "common.h"

// Makes beginning a transaction slightly clearer.
#define MDB_RDWR 0

// Big endian for sorting.
typedef byte_t DB_uint64[8];

#define DB_VAL(name, cols) \
	DB_uint64 __buf_##name[cols]; \
	MDB_val name[1] = {{ sizeof(__buf_##name), __buf_##name }};

typedef struct {
	MDB_dbi schema;
	MDB_dbi stringByID;
	MDB_dbi stringIDByValue;
	MDB_dbi stringIDByHash;
} DB_schema;

uint64_t db_column(MDB_val const *const val, index_t const col);
void db_bind(MDB_val *const val, index_t const col, uint64_t const item);

uint64_t db_autoincrement(MDB_txn *txn, MDB_dbi dbi);

uint64_t db_string_id(MDB_txn *const txn, DB_schema const *const schema, strarg_t const str);
uint64_t db_string_id_len(MDB_txn *const txn, DB_schema const *const schema, strarg_t const str, size_t const len);
strarg_t db_string(MDB_txn *const txn, DB_schema const *const schema, uint64_t const stringID);

