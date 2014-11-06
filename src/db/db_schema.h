#include <stdint.h>
#include "db_ext.h"

#define DB_VARINT_MAX 9

#define DB_VAL(name, cols) \
	uint8_t __buf_##name[DB_VARINT_MAX * cols]; \
	DB_val name[1] = {{ 0, __buf_##name }}

#define DB_RANGE(name, cols) \
	DB_VAL(__min_##name, cols); \
	DB_VAL(__max_##name, cols); \
	DB_range name[1] = {{ __min_##name, __max_##name }}

#if 0
#define DB_NOOVERWRITE_FAST DB_NOOVERWRITE
#else
#define DB_NOOVERWRITE_FAST 0
#endif

typedef uint64_t dbid_t;
enum {
	/* 0-19 are reserved. */
	DBSchema = 0,
	DBStringByID = 1,
	DBStringIDByValue = 2,
	DBStringIDByHash = 3,
};

uint64_t db_column(DB_val const *const val, unsigned const col);
char const *db_column_text(DB_txn *const txn, DB_val const *const val, unsigned const col);

void db_bind(DB_val *const val, uint64_t const item);

uint64_t db_next_id(DB_txn *const txn, dbid_t const table);

uint64_t db_string_id(DB_txn *const txn, char const *const str);
uint64_t db_string_id_len(DB_txn *const txn, char const *const str, size_t const len, int const nulterm);


