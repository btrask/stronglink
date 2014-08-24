#include "../deps/liblmdb/lmdb.h"
#include "common.h"

// Makes beginning a transaction slightly clearer.
#define MDB_RDWR 0

#define DB_VARINT_MAX 9

#define DB_VAL(name, cols) \
	byte_t __buf_##name[DB_VARINT_MAX * cols]; \
	MDB_val name[1] = {{ 0, __buf_##name }};

typedef struct {
	MDB_dbi schema;
	MDB_dbi stringByID;
	MDB_dbi stringIDByValue;
	MDB_dbi stringIDByHash;
} DB_schema;

uint64_t db_column(MDB_val const *const val, index_t const col);
strarg_t db_column_text(MDB_txn *const txn, DB_schema const *const schema, MDB_val const *const val, index_t const col);

void db_bind(MDB_val *const val, uint64_t const item);

uint64_t db_last_id(MDB_txn *txn, MDB_dbi dbi);

uint64_t db_string_id(MDB_txn *const txn, DB_schema const *const schema, strarg_t const str);
uint64_t db_string_id_len(MDB_txn *const txn, DB_schema const *const schema, strarg_t const str, size_t const len);

