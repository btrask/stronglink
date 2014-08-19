#include <assert.h>
#include "db.h"

// http://commandcenter.blogspot.co.uk/2012/04/byte-order-fallacy.html
static int64_t get_int64(byte_t const *const data, size_t const size) {
	assert(size >= DB_SIZE_INT64);
	return (
		(int64_t)(data[0]) << (8 * 7) |
		(int64_t)(data[1]) << (8 * 6) |
		(int64_t)(data[2]) << (8 * 5) |
		(int64_t)(data[3]) << (8 * 4) |
		(int64_t)(data[4]) << (8 * 3) |
		(int64_t)(data[5]) << (8 * 2) |
		(int64_t)(data[6]) << (8 * 1) |
		(int64_t)(data[7]) << (8 * 0)
	);
}
static void put_int64(byte_t *const data, size_t const size, int64_t const item) {
	assert(size >= DB_SIZE_INT64);
	data[0] = 0xff & (item >> (8 * 7));
	data[1] = 0xff & (item >> (8 * 6));
	data[2] = 0xff & (item >> (8 * 5));
	data[3] = 0xff & (item >> (8 * 4));
	data[4] = 0xff & (item >> (8 * 3));
	data[5] = 0xff & (item >> (8 * 2));
	data[6] = 0xff & (item >> (8 * 1));
	data[7] = 0xff & (item >> (8 * 0));
}

static int16_t get_text(void *const data, size_t const size, strarg_t *const outstr) {
	assert(size >= DB_SIZE_TEXT(0));
	byte_t const *const x = data;
	int16_t const len = (
		x[0] << (8 * 1) |
		x[1] << (8 * 0)
	);
	assert(size >= DB_SIZE_TEXT(len));
	strarg_t const str = (strarg_t)&x[2];
	assert('\0' == str[len]);
	*outstr = str;
	return len;
}
static void put_text(void *const data, size_t const size, strarg_t const str, size_t const len) {
	assert(len <= UINT16_MAX);
	assert(size >= DB_SIZE_TEXT(len));
	byte_t *const x = data;
	x[0] = 0xff & (len >> (8 * 1));
	x[1] = 0xff & (len >> (8 * 0));
	memcpy(&x[2], str, len);
	x[2+len] = '\0';
}

/*int64_t db_peek_int64(MDB_val const *const val) {
	int64_t const r = get_int64(val->mv_data, val->mv_size);
	return r;
}
strarg_t db_peek_text(MDB_val const *const val) {
	strarg_t str = NULL;
	get_text(val->mv_data, val->mv_size, &str);
	return str;
}*/

int64_t db_read_int64(MDB_val *const val) {
	int64_t const r = get_int64(val->mv_data, val->mv_size);
	val->mv_size -= DB_SIZE_INT64;
	val->mv_data += DB_SIZE_INT64;
	return r;
}
strarg_t db_read_text(MDB_val *const val) {
	strarg_t str = NULL;
	int16_t const len = get_text(val->mv_data, val->mv_size, &str);
	val->mv_size -= DB_SIZE_TEXT(len);
	val->mv_data += DB_SIZE_TEXT(len);
	return str;
}

void db_bind_int64(MDB_val *const val, size_t const max, int64_t const item) {
	put_int64(val->mv_data+val->mv_size, max-val->mv_size, item);
	val->mv_size += DB_SIZE_INT64;
}
void db_bind_text(MDB_val *const val, size_t const max, strarg_t const item) {
	size_t const len = strlen(item);
	put_text(val->mv_data+val->mv_size, max-val->mv_size, item, len);
	val->mv_size += DB_SIZE_TEXT(len);
}
void db_bind_text_len(MDB_val *const val, size_t const max, strarg_t const item, size_t const len) {
	put_text(val->mv_data+val->mv_size, max-val->mv_size, item, len);
	val->mv_size += DB_SIZE_TEXT(len);
}

void db_fill_int64(MDB_val *const val, int64_t const item) {
	put_int64(val->mv_data, val->mv_size, item);
	val->mv_size -= DB_SIZE_INT64;
	val->mv_data += DB_SIZE_INT64;
}
void db_fill_text(MDB_val *const val, strarg_t const item, size_t const len) {
	put_text(val->mv_data, val->mv_size, item, len);
	val->mv_size -= DB_SIZE_TEXT(len);
	val->mv_data += DB_SIZE_TEXT(len);
}

int64_t db_autoincrement(MDB_txn *txn, MDB_dbi dbi) {
	MDB_cursor *cur = NULL;
	if(MDB_SUCCESS != mdb_cursor_open(txn, dbi, &cur)) return 1;
	MDB_val prev_val;
	MDB_val ignored_val;
	int rc = mdb_cursor_get(cur, &prev_val, &ignored_val, MDB_LAST);
	mdb_cursor_close(cur); cur = NULL;
	if(MDB_SUCCESS != rc) return 1;
	return db_read_int64(&prev_val)+1;
}


