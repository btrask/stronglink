#include <stdint.h>
#include "db_ext.h"

/* TODO: These assertions shouldn't be disabled by NDEBUG because they check data integrity at runtime. */
#define db_assert(x) assert(x)
#define db_assertf(x, y, z...) assertf(x, y, ##z)

#if 0
#define DB_NOOVERWRITE_FAST DB_NOOVERWRITE
#else
#define DB_NOOVERWRITE_FAST 0
#endif

typedef uint64_t dbid_t;
enum {
	/* 0-19 are reserved. */
	DBSchema = 0, // TODO
	DBBigString = 1,
};

#define DB_VARINT_MAX 9
uint64_t db_read_uint64(DB_val *const val);
void db_bind_uint64(DB_val *const val, uint64_t const x);

uint64_t db_next_id(dbid_t const table, DB_txn *const txn);

#define DB_INLINE_MAX 96
char const *db_read_string(DB_val *const val, DB_txn *const txn);
void db_bind_string(DB_val *const val, char const *const str, DB_txn *const txn);
void db_bind_string_len(DB_val *const val, char const *const str, size_t const len, int const nulterm, DB_txn *const txn);

/* Increments range->min and fills in range->max. Assumes lexicographic ordering. */
void db_range_genmax(DB_range *const range);

