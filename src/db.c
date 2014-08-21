#include <assert.h>
#include <openssl/sha.h> // TODO: Switch to LibreSSL.
#include "db.h"

// http://commandcenter.blogspot.co.uk/2012/04/byte-order-fallacy.html
uint64_t db_column(MDB_val const *const val, index_t const col) {
	assert(val->mv_size >= (col+1) * sizeof(DB_uint64));
	byte_t const *const data = val->mv_data + col * sizeof(DB_uint64);
	return (
		(uint64_t)data[0] << (8 * 7) |
		(uint64_t)data[1] << (8 * 6) |
		(uint64_t)data[2] << (8 * 5) |
		(uint64_t)data[3] << (8 * 4) |
		(uint64_t)data[4] << (8 * 3) |
		(uint64_t)data[5] << (8 * 2) |
		(uint64_t)data[6] << (8 * 1) |
		(uint64_t)data[7] << (8 * 0)
	);
}
strarg_t db_column_text(MDB_txn *const txn, DB_schema const *const schema, MDB_val const *const val, index_t const col) {
	assert(val->mv_size >= (col+1) * sizeof(DB_uint64));
	byte_t const *const data = val->mv_data + col * sizeof(DB_uint64);

	if(!data[7]) {
		if(data[0]) return (strarg_t)data;
		assert(!data[2]);
		assert(!data[3]);
		assert(!data[4]);
		assert(!data[5]);
		assert(!data[6]);
		if(0 == data[1]) return NULL;
		if(1 == data[1]) return (strarg_t)data;
		assert(0);
	}

	MDB_val stringID_val[1] = {{ sizeof(DB_uint64), (void *)data }};
	MDB_val string_val[1];
	int rc = mdb_get(txn, schema->stringByID, stringID_val, string_val);
	if(MDB_SUCCESS != rc) return NULL;
	return string_val->mv_data;
}

void db_bind(MDB_val *const val, index_t const col, uint64_t const item) {
	assert(val->mv_size >= (col+1) * sizeof(DB_uint64));
	byte_t *const data = val->mv_data + col * sizeof(DB_uint64);
	data[0] = 0xff & (item >> (8 * 7));
	data[1] = 0xff & (item >> (8 * 6));
	data[2] = 0xff & (item >> (8 * 5));
	data[3] = 0xff & (item >> (8 * 4));
	data[4] = 0xff & (item >> (8 * 3));
	data[5] = 0xff & (item >> (8 * 2));
	data[6] = 0xff & (item >> (8 * 1));
	data[7] = 0xff & (item >> (8 * 0));
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
	return db_string_id_len(txn, schema, str, len);
}
uint64_t db_string_id_len(MDB_txn *const txn, DB_schema const *const schema, strarg_t const str, size_t const len) {
	if(!str) return 0;

	MDB_dbi lookup_table;
	MDB_val lookup_val[1];
	byte_t hash[SHA256_DIGEST_LENGTH];
	int rc;

	if(0 == len) {
		return (uint64_t)1 << (8 * 6);
	} else if(len < sizeof(DB_uint64)) {
		uint64_t x = 0;
		if(len >= 1) x |= (uint64_t)str[0] << (8 * 7);
		if(len >= 2) x |= (uint64_t)str[1] << (8 * 6);
		if(len >= 3) x |= (uint64_t)str[2] << (8 * 5);
		if(len >= 4) x |= (uint64_t)str[3] << (8 * 4);
		if(len >= 5) x |= (uint64_t)str[4] << (8 * 3);
		if(len >= 6) x |= (uint64_t)str[5] << (8 * 2);
		if(len >= 7) x |= (uint64_t)str[6] << (8 * 1);
		// Last byte must be nul.
		return x;
	} else if(len < sizeof(hash) * 2) {
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
	assert(MDB_NOTFOUND == rc);

	// Assume we have a write transaction. If not, it'll let us know.
	uint64_t const newStringID = (db_last_id(txn, schema->stringByID) + 2) | 1;
	assertf(!(newStringID >> 63), "Overflow (%llx)", newStringID);
	if(newStringID >> 63) return 0;
	DB_VAL(newStringID_val, 1);
	db_bind(newStringID_val, 0, newStringID);
	str_t *nulterm = strndup(str, len); // TODO: Avoid this if possible.
	MDB_val str_val = { len+1, nulterm };
	rc = mdb_put(txn, schema->stringByID, newStringID_val, &str_val, MDB_NOOVERWRITE);
	FREE(&nulterm);
	if(EACCES == rc) return 0; // Read-only transaction.
	assertf(MDB_SUCCESS == rc, "mdb err %s", mdb_strerror(rc));
	rc = mdb_put(txn, lookup_table, lookup_val, newStringID_val, MDB_NOOVERWRITE);
	assertf(MDB_SUCCESS == rc, "mdb err %s", mdb_strerror(rc));
	return newStringID;
}

