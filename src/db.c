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


uint64_t db_column(MDB_val const *const val, index_t const col) {
	size_t const pos = varint_seek(val->mv_data, val->mv_size, col);
	return varint_decode(val->mv_data+pos, val->mv_size-pos);
}
strarg_t db_column_text(MDB_txn *const txn, DB_schema const *const schema, MDB_val const *const val, index_t const col) {
	uint64_t const stringID = db_column(val, col);
	if(0 == stringID) return NULL;
	if(1 == stringID) return "";
	DB_VAL(stringID_key, 2);
	db_bind(stringID_key, DBStringByID);
	db_bind(stringID_key, stringID);
	MDB_val string_val[1];
	int rc = mdb_get(txn, schema->main, stringID_key, string_val);
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
uint64_t db_next_id(MDB_txn *const txn, DB_schema const *const schema, dbid_t const table) {
	MDB_cursor *cur = NULL;
	if(MDB_SUCCESS != mdb_cursor_open(txn, schema->main, &cur)) return 0;
	DB_VAL(min, 1);
	DB_VAL(max, 1);
	db_bind(min, table+0);
	db_bind(max, table+1);
	DB_range range = { min, max };
	MDB_val prev[1];
	int rc = db_cursor_firstr(cur, &range, prev, NULL, -1);
	mdb_cursor_close(cur); cur = NULL;
	if(MDB_SUCCESS != rc) return 0;
	return db_column(prev, 0)+1;
}

uint64_t db_string_id(MDB_txn *const txn, DB_schema const *const schema, strarg_t const str) {
	if(!str) return 0;
	size_t const len = strlen(str);
	return db_string_id_len(txn, schema, str, len, true);
}
uint64_t db_string_id_len(MDB_txn *const txn, DB_schema const *const schema, strarg_t const str, size_t const len, bool_t const nulterm) {
	if(!str) return 0;
	if(!len) return 1;

	byte_t buf[DB_VARINT_MAX + SHA256_DIGEST_LENGTH*2 + DB_VARINT_MAX];
	MDB_val lookup_key[1] = {{ 0, buf }};
	int rc;

	if(len+1 < SHA256_DIGEST_LENGTH*2) {
		db_bind(lookup_key, DBStringIDByValue);
		memcpy(buf+lookup_key->mv_size, str, len+1);
		lookup_key->mv_size += len+1;
	} else {
		db_bind(lookup_key, DBStringIDByHash);
		SHA256_CTX algo[1];
		if(SHA256_Init(algo) < 0) return 0;
		if(SHA256_Update(algo, str, len) < 0) return 0;
		if(SHA256_Final(buf+lookup_key->mv_size, algo) < 0) return 0;
		lookup_key->mv_size += SHA256_DIGEST_LENGTH;
	}

	MDB_val existingStringID_val[1];
	rc = mdb_get(txn, schema->main, lookup_key, existingStringID_val);
	if(MDB_SUCCESS == rc) return db_column(existingStringID_val, 0);
	assertf(MDB_NOTFOUND == rc, "Database err %s", mdb_strerror(rc));

	MDB_cursor *cur = NULL;
	rc = mdb_cursor_open(txn, schema->main, &cur);
	assert(MDB_SUCCESS == rc);

	DB_VAL(strings_min, 1);
	DB_VAL(strings_max, 1);
	db_bind(strings_min, DBStringByID+0);
	db_bind(strings_max, DBStringByID+1);
	DB_range strings = { strings_min, strings_max };
	MDB_val lastID_key[1];
	rc = db_cursor_firstr(cur, &strings, lastID_key, NULL, -1);
	uint64_t lastID;
	if(MDB_SUCCESS == rc) {
		assert(DBStringByID == db_column(lastID_key, 0));
		lastID = db_column(lastID_key, 1);
	} else if(MDB_NOTFOUND == rc) {
		lastID = 1;
	} else {
		assertf(0, "Database err %s", mdb_strerror(rc));
	}

	uint64_t const nextID = lastID+1;
	DB_VAL(nextID_key, 2);
	db_bind(nextID_key, DBStringByID);
	db_bind(nextID_key, nextID);
	str_t *str2 = nulterm ? (str_t *)str : strndup(str, len);
	MDB_val str_val = { len+1, str2 };
	rc = mdb_put(txn, schema->main, nextID_key, &str_val, MDB_NOOVERWRITE);
	if(nulterm) str2 = NULL;
	else FREE(&str2);
	mdb_cursor_close(cur); cur = NULL;
	if(EACCES == rc) return 0; // Read-only transaction.
	assertf(MDB_SUCCESS == rc, "Database err %s", mdb_strerror(rc));

	DB_VAL(nextID_val, 1);
	db_bind(nextID_val, nextID);
	rc = mdb_put(txn, schema->main, lookup_key, nextID_val, MDB_NOOVERWRITE);
	assertf(MDB_SUCCESS == rc, "Database err %s", mdb_strerror(rc));
	return nextID;
}

int db_cursor(MDB_txn *const txn, MDB_dbi const dbi, MDB_cursor **const cur) {
	if(*cur) return mdb_cursor_renew(txn, *cur);
	return mdb_cursor_open(txn, dbi, cur);
}
int db_cursor_get(MDB_cursor *const cur, MDB_val *const key, MDB_val *const val, MDB_cursor_op const op) {
	assert(cur);
	switch(op) {
		case MDB_FIRST_DUP:
		case MDB_LAST_DUP:
		case MDB_PREV_DUP:
		case MDB_NEXT_DUP: {
			size_t ignore;
			int rc = mdb_cursor_count(cur, &ignore);
			if(MDB_SUCCESS == rc) break;

			// Key should be optional even if cursor isn't initialized
			if(!key) return rc;

			// Cursor isn't initialized but we have a key
			MDB_val ignore_val;
			MDB_val *const v = val ? val : &ignore_val;
			rc = mdb_cursor_get(cur, key, v, MDB_SET);
			if(MDB_SUCCESS != rc) return rc;
			switch(op) {
				case MDB_FIRST_DUP:
				case MDB_NEXT_DUP:
					return rc;
				case MDB_LAST_DUP:
				case MDB_PREV_DUP:
					return mdb_cursor_get(cur, key, v, MDB_LAST_DUP);
				default: assert(0);
			}
			assert(0);
		}
		default: break;
	}
	switch(op) {
		case MDB_GET_CURRENT:
		case MDB_FIRST_DUP:
		case MDB_LAST_DUP:
		case MDB_PREV_DUP:
		case MDB_NEXT_DUP:
		case MDB_NEXT:
		case MDB_PREV: {
			// Keys and values should be optional for these ops
			MDB_val ignore1;
			MDB_val ignore2;
			MDB_val *const k = key ? key : &ignore1;
			MDB_val *const v = val ? val : &ignore2;
			int rc = mdb_cursor_get(cur, k, v, op);
			if(MDB_NOTFOUND != rc) return rc;
			// Fall off when we reach the end
			mdb_cursor_renew(mdb_cursor_txn(cur), cur);
			return rc;
		}
		case MDB_SET:
		case MDB_SET_KEY:
		case MDB_SET_RANGE: {
			// NULL values have weird side effects (DUPSORT only?)
			MDB_val ignore;
			MDB_val *const v = val ? val : &ignore;
			int rc = mdb_cursor_get(cur, key, v, op);
			return rc;
		}
		case MDB_GET_BOTH_RANGE: {
			// Don't leave the cursor half-initialized
			int rc = mdb_cursor_get(cur, key, val, op);
			if(MDB_NOTFOUND != rc) return rc;
			mdb_cursor_renew(mdb_cursor_txn(cur), cur);
			return rc;
		}
		default:
			return mdb_cursor_get(cur, key, val, op);
	}
}









int db_cursor_seek(MDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	if(!key) return EINVAL;
	MDB_val const orig = *key;
	MDB_cursor_op const op = 0 == dir ? MDB_SET : MDB_SET_RANGE;
	int rc = mdb_cursor_get(cursor, key, data, op);
	if(dir >= 0) return rc;
	if(MDB_SUCCESS == rc) {
		MDB_txn *const txn = mdb_cursor_txn(cursor);
		MDB_dbi const dbi = mdb_cursor_dbi(cursor);
		if(0 == mdb_cmp(txn, dbi, &orig, key)) return rc;
		return mdb_cursor_get(cursor, key, data, MDB_PREV);
	} else if(MDB_NOTFOUND == rc) {
		return mdb_cursor_get(cursor, key, data, MDB_LAST);
	} else return rc;
}
int db_cursor_first(MDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	if(0 == dir) return EINVAL;
	MDB_cursor_op const op = dir < 0 ? MDB_FIRST : MDB_LAST;
	return mdb_cursor_get(cursor, key, data, op);
}
int db_cursor_next(MDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	if(0 == dir) return EINVAL;
	MDB_cursor_op const op = dir < 0 ? MDB_PREV : MDB_NEXT;
	return mdb_cursor_get(cursor, key, data, op);
}


int db_cursor_seekr(MDB_cursor *const cursor, DB_range const *const range, MDB_val *const key, MDB_val *const data, int const dir) {
	int rc = db_cursor_seek(cursor, key, data, dir);
	if(MDB_SUCCESS != rc) return rc;
	MDB_val const *const limit = dir < 0 ? range->min : range->max;
	MDB_txn *const txn = mdb_cursor_txn(cursor);
	MDB_dbi const dbi = mdb_cursor_dbi(cursor);
	int x = mdb_cmp(txn, dbi, key, limit);
	if(x * dir < 0) return MDB_SUCCESS;
	mdb_cursor_renew(txn, cursor);
	return MDB_NOTFOUND;
}
int db_cursor_firstr(MDB_cursor *const cursor, DB_range const *const range, MDB_val *const key, MDB_val *const data, int const dir) {
	if(0 == dir) return EINVAL;
	MDB_val const *const first = dir > 0 ? range->min : range->max;
	MDB_val k = *first;
	int rc = db_cursor_seek(cursor, &k, data, dir);
	if(MDB_SUCCESS != rc) return rc;
	MDB_txn *const txn = mdb_cursor_txn(cursor);
	MDB_dbi const dbi = mdb_cursor_dbi(cursor);
	int x = mdb_cmp(txn, dbi, first, &k);
	if(0 == x) {
		rc = db_cursor_next(cursor, &k, data, dir);
		if(MDB_SUCCESS != rc) return rc;
	}
	MDB_val const *const last = dir < 0 ? range->min : range->max;
	x = mdb_cmp(txn, dbi, &k, last);
	if(x * dir < 0) {
		if(key) *key = k;
		return MDB_SUCCESS;
	} else {
		mdb_cursor_renew(txn, cursor);
		return MDB_NOTFOUND;
	}
}
int db_cursor_nextr(MDB_cursor *const cursor, DB_range const *const range, MDB_val *const key, MDB_val *const data, int const dir) {
	int rc = db_cursor_next(cursor, key, data, dir);
	if(MDB_SUCCESS != rc) return rc;
	MDB_val const *const limit = dir < 0 ? range->min : range->max;
	MDB_txn *const txn = mdb_cursor_txn(cursor);
	MDB_dbi const dbi = mdb_cursor_dbi(cursor);
	int x = mdb_cmp(txn, dbi, key, limit);
	if(x * dir < 0) return MDB_SUCCESS;
	mdb_cursor_renew(txn, cursor);
	return MDB_NOTFOUND;
}

