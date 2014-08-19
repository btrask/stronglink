#include "../deps/liblmdb/lmdb.h"
#include "common.h"

// Makes beginning a transaction slightly clearer.
#define MDB_RDWR 0

#define DB_SIZE_INT64 8
#define DB_SIZE_TEXT(len) (2+(len)+1)

//int64_t db_peek_int64(MDB_val const *const val);
//strarg_t db_peek_text(MDB_val const *const val);

int64_t db_read_int64(MDB_val *const val);
strarg_t db_read_text(MDB_val *const val);

void db_bind_int64(MDB_val *const val, size_t const max, int64_t const item);
void db_bind_text(MDB_val *const val, size_t const max, strarg_t const item);
void db_bind_text_len(MDB_val *const val, size_t const max, strarg_t const item, size_t const len);

void db_fill_int64(MDB_val *const val, int64_t const item);
void db_fill_text(MDB_val *const val, strarg_t const item, size_t const len);

int64_t db_autoincrement(MDB_txn *txn, MDB_dbi dbi);

