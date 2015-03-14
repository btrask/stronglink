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

#define DB_VAL_STORAGE(val, len) \
	uint8_t __buf_##val[(len)]; \
	*(val) = (DB_val){ 0, __buf_##val };
#define DB_RANGE_STORAGE(range, len) \
	uint8_t __buf_min_##range[(len)]; \
	uint8_t __buf_max_##range[(len)]; \
	*(range)->min = (DB_val){ 0, __buf_min_##range }; \
	*(range)->max = (DB_val){ 0, __buf_max_##range };

typedef uint64_t dbid_t;
enum {
	/* 0-19 are reserved. */
	DBSchema = 0, // TODO
	DBBigString = 1,
};

int db_schema_verify(DB_txn *const txn);

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

