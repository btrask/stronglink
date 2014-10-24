#include <assert.h>
#include <stdio.h> /* DEBUG */
#include <openssl/sha.h> // TODO: Switch to LibreSSL.
#include <stdlib.h>
#include <string.h>
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
	for(off_t i = 1; i < len; ++i) x = x << 8 | data[i];
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


uint64_t db_column(DB_val const *const val, unsigned const col) {
	size_t const pos = varint_seek(val->data, val->size, col);
	return varint_decode(val->data+pos, val->size-pos);
}
char const *db_column_text(DB_txn *const txn, DB_val const *const val, unsigned const col) {
	uint64_t const stringID = db_column(val, col);
	if(0 == stringID) return NULL;
	if(1 == stringID) return "";
	DB_VAL(stringID_key, 2);
	db_bind(stringID_key, DBStringByID);
	db_bind(stringID_key, stringID);
	DB_val string_val[1];
	int rc = db_get(txn, stringID_key, string_val);
	if(DB_SUCCESS != rc) return NULL;
	return string_val->data;
}

void db_bind(DB_val *const val, uint64_t const item) {
	size_t const len = varint_encode(val->data+val->size, SIZE_MAX, item);
	val->size += len;
}

uint64_t db_next_id(DB_txn *const txn, dbid_t const table) {
	DB_cursor *cur = NULL;
	if(DB_SUCCESS != db_txn_cursor(txn, &cur)) return 0;
	DB_RANGE(range, 1);
	db_bind(range->min, table+0);
	db_bind(range->max, table+1);
	DB_val prev[1];
	int rc = db_cursor_firstr(cur, range, prev, NULL, -1);
	if(DB_NOTFOUND == rc) return 1;
	if(DB_SUCCESS != rc) return 0;
	return db_column(prev, 1)+1;
}

uint64_t db_string_id(DB_txn *const txn, char const *const str) {
	if(!str) return 0;
	size_t const len = strlen(str);
	return db_string_id_len(txn, str, len, 1);
}
uint64_t db_string_id_len(DB_txn *const txn, char const *const str, size_t const len, int const nulterm) {
	assert(txn);
	if(!str) return 0;
	if(!len) return 1;

	uint8_t buf[DB_VARINT_MAX + SHA256_DIGEST_LENGTH*2 + DB_VARINT_MAX];
	DB_val lookup_key[1] = {{ 0, buf }};
	int rc;

	if(len+1 < SHA256_DIGEST_LENGTH*2) {
		db_bind(lookup_key, DBStringIDByValue);
		memcpy(buf+lookup_key->size, str, len+1);
		lookup_key->size += len+1;
	} else {
		db_bind(lookup_key, DBStringIDByHash);
		SHA256_CTX algo[1];
		if(SHA256_Init(algo) < 0) return 0;
		if(SHA256_Update(algo, str, len) < 0) return 0;
		if(SHA256_Final(buf+lookup_key->size, algo) < 0) return 0;
		lookup_key->size += SHA256_DIGEST_LENGTH;
	}

	DB_val existingStringID_val[1];
	rc = db_get(txn, lookup_key, existingStringID_val);
	if(DB_SUCCESS == rc) return db_column(existingStringID_val, 0);
	if(DB_NOTFOUND != rc) fprintf(stderr, "rc = %s\n", db_strerror(rc));
	assert(DB_NOTFOUND == rc);

	unsigned flags;
	rc = db_txn_get_flags(txn, &flags);
	if(DB_SUCCESS != rc || DB_RDONLY & flags) return 0;

	uint64_t const nextID = db_next_id(txn, DBStringByID);
	if(!nextID) return 0;

	DB_VAL(nextID_key, 2);
	db_bind(nextID_key, DBStringByID);
	db_bind(nextID_key, nextID);
	char *str2 = nulterm ? (char *)str : strndup(str, len);
	DB_val str_val = { len+1, str2 };
	rc = db_put(txn, nextID_key, &str_val, DB_NOOVERWRITE);
	if(!nulterm) free(str2);
	str2 = NULL;
	assert(DB_SUCCESS == rc);

	DB_VAL(nextID_val, 1);
	db_bind(nextID_val, nextID);
	rc = db_put(txn, lookup_key, nextID_val, DB_NOOVERWRITE);
	assert(DB_SUCCESS == rc);
	return nextID;
}


