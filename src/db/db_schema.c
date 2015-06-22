// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdio.h> // DEBUG
#include <openssl/sha.h> // TODO: Switch to LibreSSL.
#include <stdlib.h>
#include <string.h>
#include "../common.h"
#include "db_schema.h"

static size_t varint_size(uint8_t const *const data) {
	return (data[0] >> 4) + 1;
}
static uint64_t varint_decode(uint8_t const *const data, size_t const size) {
	assert(size >= 1);
	size_t const len = varint_size(data);
	assert(len);
	assert(size >= len);
	uint64_t x = data[0] & 0x0f;
	for(size_t i = 1; i < len; ++i) x = x << 8 | data[i];
	return x;
}
static size_t varint_encode(uint8_t *const data, size_t const size, uint64_t const x) {
	assert(size >= DB_VARINT_MAX);
	size_t rem = 8;
	size_t out = 0;
	while(rem--) {
		uint8_t const y = 0xff & (x >> (8 * rem));
		if(out) {
			data[out++] = y;
		} else if(y && y <= 0x0f) {
			data[out++] = ((rem+0) << 4) | (y & 0x0f);
		} else if(y) {
			data[out++] = ((rem+1) << 4) | 0;
			data[out++] = y;
		}
	}
	if(!out) data[out++] = 0;
	assert(varint_decode(data, size) == x);
	return out;
}
static size_t varint_seek(uint8_t const *const data, size_t const size, unsigned const col) {
	size_t pos = 0;
	for(unsigned i = 0; i < col; ++i) {
		assert(pos+1 <= size);
		pos += varint_size(data+pos);
	}
	assert(pos+1 <= size);
	return pos;
}


int db_schema_verify(DB_txn *const txn) {
	char const magic[] = "DBDB schema layer v1";
	size_t const len = sizeof(magic)-1;

	DB_val key[1];
	DB_VAL_STORAGE(key, DB_VARINT_MAX*2);
	db_bind_uint64(key, DBSchema);
	db_bind_uint64(key, 0);
	DB_val val[1];

	DB_cursor *cur;
	int rc = db_txn_cursor(txn, &cur);
	if(rc < 0) return rc;
	rc = db_cursor_first(cur, NULL, NULL, +1);
	if(rc < 0 && DB_NOTFOUND != rc) return rc;

	// If the database is completely empty
	// we can assume it's ours to play with
	if(DB_NOTFOUND == rc) {
		*val = (DB_val){ len, (char *)magic };
		rc = db_put(txn, key, val, 0);
		if(rc < 0) return rc;
		return 0;
	}

	rc = db_get(txn, key, val);
	if(DB_NOTFOUND == rc) return DB_VERSION_MISMATCH;
	if(rc < 0) return rc;
	if(len != val->size) return DB_VERSION_MISMATCH;
	if(0 != memcmp(val->data, magic, len)) return DB_VERSION_MISMATCH;
	return 0;
}


// TODO: db_bind_* functions should accept buffer size for bounds checking

uint64_t db_read_uint64(DB_val *const val) {
	db_assert(val->size >= 1);
	size_t const len = varint_size(val->data);
	db_assert(val->size >= len);
	uint64_t const x = varint_decode(val->data, val->size);
	val->data += len;
	val->size -= len;
	return x;
}
void db_bind_uint64(DB_val *const val, uint64_t const x) {
	unsigned char *const out = val->data;
	size_t const len = varint_encode(out+val->size, SIZE_MAX, x);
	val->size += len;
}

uint64_t db_next_id(dbid_t const table, DB_txn *const txn) {
	DB_cursor *cur = NULL;
	if(db_txn_cursor(txn, &cur) < 0) return 0;
	DB_range range[1];
	DB_RANGE_STORAGE(range, DB_VARINT_MAX);
	db_bind_uint64(range->min, table+0);
	db_bind_uint64(range->max, table+1);
	DB_val prev[1];
	int rc = db_cursor_firstr(cur, range, prev, NULL, -1);
	if(DB_NOTFOUND == rc) return 1;
	if(rc < 0) return 0;
	uint64_t const t = db_read_uint64(prev);
	assert(table == t);
	return db_read_uint64(prev)+1;
}


// Inline strings can be up to 96 bytes including nul. Longer strings are
// truncated at 64 bytes (including nul), followed by the 32-byte SHA-256 hash.
// The first byte of the hash may not be 0x00 (if it's 0x00, it's replaced with
// 0x01). If a string is exactly 64 bytes (including nul), it's followed by an
// extra 0x00 to indicate it wasn't truncated. A null pointer is 0x00 00, and
// an empty string is 0x00 01.
#define DB_INLINE_TRUNC (DB_INLINE_MAX-SHA256_DIGEST_LENGTH)

char const *db_read_string(DB_val *const val, DB_txn *const txn) {
	assert(txn);
	assert(val);
	db_assert(val->size >= 1);
	char const *const str = val->data;
	size_t const len = strnlen(str, MIN(val->size, DB_INLINE_MAX));
	db_assert('\0' == str[len]);
	if(0 == len) {
		db_assert(val->size >= 2);
		val->data += 2;
		val->size -= 2;
		if(0x00 == str[1]) return NULL;
		if(0x01 == str[1]) return "";
		db_assertf(0, "Invalid string type %u\n", str[1]);
		return NULL;
	}
	if(DB_INLINE_TRUNC != len+1) {
		val->data += len+1;
		val->size -= len+1;
		return str;
	}
	db_assert(val->size >= len+2);
	if(0x00 == str[len+1]) {
		val->data += len+2;
		val->size -= len+2;
		return str;
	}

	DB_val key = { DB_INLINE_MAX, (char *)str };
	DB_val full[1];
	int rc = db_get(txn, &key, full);
	db_assertf(rc >= 0, "Database error %s", db_strerror(rc));
	char const *const fstr = full->data;
	db_assert('\0' == fstr[full->size-1]);
	return fstr;
}
void db_bind_string(DB_val *const val, char const *const str, DB_txn *const txn) {
	size_t const len = str ? strlen(str) : 0;
	db_bind_string_len(val, str, len, true, txn);
}
void db_bind_string_len(DB_val *const val, char const *const str, size_t const len, int const nulterm, DB_txn *const txn) {
	assert(val);
	assert(len == strnlen(str, len) && "Embedded nuls");
	unsigned char *const out = val->data;
	if(0 == len) {
		out[val->size++] = '\0';
		out[val->size++] = str ? 0x01 : 0x00;
		return;
	}
	if(len < DB_INLINE_MAX) {
		memcpy(out+val->size, str, len);
		val->size += len;
		out[val->size++] = '\0';
		if(DB_INLINE_TRUNC != len+1) return;
		out[val->size++] = '\0';
		return;
	}

	memcpy(out+val->size, str, DB_INLINE_TRUNC-1);
	val->size += DB_INLINE_TRUNC-1;
	out[val->size++] = '\0';

	SHA256_CTX algo[1];
	int rc;
	rc = SHA256_Init(algo);
	db_assert(rc >= 0);
	rc = SHA256_Update(algo, str, len);
	db_assert(rc >= 0);
	rc = SHA256_Final(out+val->size, algo);
	db_assert(rc >= 0);
	if(0x00 == out[val->size]) out[val->size] = 0x01;
	val->size += SHA256_DIGEST_LENGTH;

	if(!txn) return;
	unsigned flags = 0;
	rc = db_txn_get_flags(txn, &flags);
	db_assertf(rc >= 0, "Database error %s", db_strerror(rc));
	if(flags & DB_RDONLY) return;

	DB_val key = { DB_INLINE_MAX, out+val->size-DB_INLINE_MAX };
	char *str2 = nulterm ? (char *)str : strndup(str, len);
	DB_val full = { len+1, str2 };
	assert('\0' == str2[full.size-1]);
	rc = db_put(txn, &key, &full, 0);
	if(!nulterm) free(str2);
	str2 = NULL;
	db_assertf(rc >= 0, "Database error %s", db_strerror(rc));
}


void db_range_genmax(DB_range *const range) {
	assert(range);
	assert(range->min);
	assert(range->max);
	unsigned char *const out = range->max->data;
	memcpy(out, range->min->data, range->min->size);
	range->max->size = range->min->size;
	size_t i = range->max->size;
	while(i--) {
		if(out[i] < 0xff) {
			out[i]++;
			return;
		} else {
			out[i] = 0;
		}
	}
	assert(0);
}

