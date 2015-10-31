// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <stdint.h>
#include "db_ext.h"

#ifndef NDEBUG
#define db_assert(x) assert(x)
#define db_assertf(x, y, z...) assertf(x, y, ##z)
#else
#error "Database assertions not configured for NDEBUG" // TODO
#endif

// "Blind write" support. There shouldn't be a collision
// but in some cases we don't want to pay for the lookup.
#if 0
#define DB_NOOVERWRITE_FAST DB_NOOVERWRITE
#else
#define DB_NOOVERWRITE_FAST 0
#endif

// FASTHIT expects a collision and pays the read to avoid the write.
// FASTMISS expects a miss and always writes to avoid the read.
#define DB_NOOVERWRITE_FASTHIT DB_NOOVERWRITE
#define DB_NOOVERWRITE_FASTMISS DB_NOOVERWRITE_FAST

#define DB_VAL_STORAGE(val, len) \
	uint8_t __buf_##val[(len)]; \
	*(val) = (DB_val){ 0, __buf_##val };
#define DB_RANGE_STORAGE(range, len) \
	uint8_t __buf_min_##range[(len)]; \
	uint8_t __buf_max_##range[(len)]; \
	*(range)->min = (DB_val){ 0, __buf_min_##range }; \
	*(range)->max = (DB_val){ 0, __buf_max_##range };

// TODO: These checks are better than nothing, but they're far from ideal.
// We can calculate the storage needed at compile-time, so we shouldn't need
// to hardcode+verify at all. Just generate the right answer and use that.
// We could also count the theoretical storage needed at runtime and verify
// that. Probably by adding an in/out counter to db_bind_*.
#define DB_VAL_STORAGE_VERIFY(val) \
	assert((val)->size <= sizeof(__buf_##val))
#define DB_RANGE_STORAGE_VERIFY(range) do { \
	assert((range)->min->size <= sizeof(__buf_min_##range)); \
	assert((range)->max->size <= sizeof(__buf_max_##range)); \
} while(0)

typedef uint64_t dbid_t;
enum {
	// 0-19 are reserved.
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

// Increments range->min to fill in range->max.
// Assumes lexicographic ordering. Don't use it if you changed cmp functions.
void db_range_genmax(DB_range *const range);

static void db_nullval(DB_val *const val) {
	val->size = 0;
	val->data = NULL;
}

