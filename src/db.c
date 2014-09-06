#include <assert.h>
#include <openssl/sha.h> // TODO: Switch to LibreSSL.
#include "db.h"

static size_t varint_size(byte_t const *const data) {
	return (data[0] >> 4) + 1;
}
static uint64_t varint_decode(byte_t const *const data, size_t const size) {
	assert(size >= 1);
	size_t const len = varint_size(data);
	assert(len);
	assert(size >= len);
	uint64_t x = data[0] & 0x0f;
	for(off_t i = 1; i < len; ++i) x = x << 8 | data[i];
	return x;
}
static size_t varint_encode(byte_t *const data, size_t const size, uint64_t const x) {
	assert(size >= DB_VARINT_MAX);
	size_t rem = 8;
	size_t out = 0;
	while(rem--) {
		byte_t const y = 0xff & (x >> (8 * rem));
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
static size_t varint_seek(byte_t const *const data, size_t const size, index_t const col) {
	size_t pos = 0;
	for(index_t i = 0; i < col; ++i) {
		assert(pos+1 <= size);
		pos += varint_size(data+pos);
	}
	assert(pos+1 <= size);
	return pos;
}


// http://commandcenter.blogspot.co.uk/2012/04/byte-order-fallacy.html
uint64_t db_column(MDB_val const *const val, index_t const col) {
	size_t const pos = varint_seek(val->mv_data, val->mv_size, col);
	return varint_decode(val->mv_data+pos, val->mv_size-pos);
}
strarg_t db_column_text(MDB_txn *const txn, DB_schema const *const schema, MDB_val const *const val, index_t const col) {
	uint64_t const stringID = db_column(val, col);
	if(0 == stringID) return NULL;
	if(1 == stringID) return "";
	DB_VAL(stringID_val, 1);
	db_bind(stringID_val, stringID);
	MDB_val string_val[1];
	int rc = mdb_get(txn, schema->stringByID, stringID_val, string_val);
	if(MDB_SUCCESS != rc) return NULL;
	return string_val->mv_data;
}

void db_bind(MDB_val *const val, uint64_t const item) {
	size_t const len = varint_encode(val->mv_data+val->mv_size, SIZE_MAX, item);
	val->mv_size += len;
}

uint64_t db_last_id(MDB_txn *txn, MDB_dbi dbi) {
	MDB_cursor *cur = NULL;
	if(MDB_SUCCESS != mdb_cursor_open(txn, dbi, &cur)) return 0;
	MDB_val prev_val[1];
	MDB_val ignored_val[1];
	int rc = mdb_cursor_get(cur, prev_val, ignored_val, MDB_LAST);
	mdb_cursor_close(cur); cur = NULL;
	if(MDB_SUCCESS != rc) return 0;
	return db_column(prev_val, 0);
}

uint64_t db_string_id(MDB_txn *const txn, DB_schema const *const schema, strarg_t const str) {
	if(!str) return 0;
	size_t const len = strlen(str);
	return db_string_id_len(txn, schema, str, len, true);
}
uint64_t db_string_id_len(MDB_txn *const txn, DB_schema const *const schema, strarg_t const str, size_t const len, bool_t const nulterm) {
	if(!str) return 0;
	if(!len) return 1;

	MDB_dbi lookup_table;
	MDB_val lookup_val[1];
	byte_t hash[SHA256_DIGEST_LENGTH];
	int rc;

	if(len < sizeof(hash) * 2) {
		lookup_table = schema->stringIDByValue;
		lookup_val->mv_size = len;
		lookup_val->mv_data = (void *)str;
	} else {
		SHA256_CTX algo[1];
		if(SHA256_Init(algo) < 0) return 0;
		if(SHA256_Update(algo, str, len) < 0) return 0;
		if(SHA256_Final(hash, algo) < 0) return 0;
		lookup_table = schema->stringIDByHash;
		lookup_val->mv_size = sizeof(hash);
		lookup_val->mv_data = hash;
	}

	MDB_val existingStringID_val[1];
	rc = mdb_get(txn, lookup_table, lookup_val, existingStringID_val);
	if(MDB_SUCCESS == rc) return db_column(existingStringID_val, 0);
	assertf(MDB_NOTFOUND == rc, "mdb err %s", mdb_strerror(rc));

	MDB_cursor *cur = NULL;
	rc = mdb_cursor_open(txn, schema->stringByID, &cur);
	assert(MDB_SUCCESS == rc);

	uint64_t lastID;
	MDB_val lastID_val[1];
	MDB_val ignored_val[1];
	rc = mdb_cursor_get(cur, lastID_val, ignored_val, MDB_LAST);
	if(MDB_SUCCESS == rc) {
		lastID = db_column(lastID_val, 0);
	} else if(MDB_NOTFOUND == rc) {
		lastID = 1;
	} else {
		assertf(0, "mdb err %s", mdb_strerror(rc));
	}

	uint64_t const nextID = lastID+1;
	DB_VAL(nextID_val, 1);
	db_bind(nextID_val, nextID);
	str_t *str2 = nulterm ? (str_t *)str : strndup(str, len);
	MDB_val str_val = { len+1, str2 };
	rc = mdb_put(txn, schema->stringByID, nextID_val, &str_val, MDB_NOOVERWRITE | MDB_APPEND);
	if(nulterm) str2 = NULL;
	else FREE(&str2);
	mdb_cursor_close(cur); cur = NULL;
	if(EACCES == rc) return 0; // Read-only transaction.
	assertf(MDB_SUCCESS == rc, "mdb err %s", mdb_strerror(rc));
	rc = mdb_put(txn, lookup_table, lookup_val, nextID_val, MDB_NOOVERWRITE);
	assertf(MDB_SUCCESS == rc, "mdb err %s", mdb_strerror(rc));
	return nextID;
}

int db_cursor(MDB_txn *const txn, MDB_dbi const dbi, MDB_cursor **const cur) {
	if(*cur) return mdb_cursor_renew(txn, *cur);
	return mdb_cursor_open(txn, dbi, cur);
}
int db_cursor_get(MDB_cursor *const cur, MDB_val *const key, MDB_val *const val, MDB_cursor_op const op) {
	assert(cur);
	// MDB workarounds
	// - Inconsistent dupsort cursor initialization rules (prev/next init, first/last don't)
	switch(op) {
		case MDB_PREV_DUP:
		case MDB_NEXT_DUP: {
			if(key) break;
			size_t ignore;
			int rc = mdb_cursor_count(cur, &ignore);
			if(MDB_SUCCESS != rc) return rc;
			break;
		}
		default: break;
	}
	// - Keys required when they shouldn't be
	// - NULL values having unexpected side effects (mainly/exclusively for dupsort tables?)
	switch(op) {
		case MDB_GET_CURRENT:
		case MDB_FIRST_DUP:
		case MDB_LAST_DUP:
		case MDB_PREV_DUP:
		case MDB_NEXT_DUP: {
			MDB_val ignore1;
			MDB_val ignore2;
			MDB_val *const k = key ? key : &ignore1;
			MDB_val *const v = val ? val : &ignore2;
			int rc = mdb_cursor_get(cur, k, v, op);
			return rc;
		}
		case MDB_SET:
		case MDB_SET_KEY:
		case MDB_SET_RANGE: {
			MDB_val ignore;
			MDB_val *const v = val ? val : &ignore;
			int rc = mdb_cursor_get(cur, key, v, op);
			return rc;
		}
		default:
			return mdb_cursor_get(cur, key, val, op);
	}
}


