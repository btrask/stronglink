


int db_pfx(int const rc, MDB_val const *const pfx, MDB_val const *const key) {
	if(MDB_SUCCESS != rc) return rc;
	if(key->mv_size < pfx->mv_size) return MDB_NOTFOUND;
	int x = memcmp(pfx->mv_data, key->mv_data, pfx->mv_size);
	if(0 == x) return MDB_SUCCESS;
	return MDB_NOTFOUND;
}

int db_cursor_first_pfx(MDB_cursor *const cursor, MDB_val const *const pfx, MDB_val *const key, MDB_val *const data) {
	*key = *pfx;
	int rc = mdb_cursor_seek(cursor, key, data, +1);
	rc = db_pfx(rc, pfx, key);
	if(MDB_SUCCESS == rc) return MDB_SUCCESS;
	mdb_cursor_renew(mdb_cursor_txn(cursor), cursor);
	return MDB_NOTFOUND;
}
int db_cursor_next_pfx(



typedef struct DB_rcursor DB_rcursor;

struct DB_rcursor {
	MDB_cursor *cursor;
	MDB_val min[1];
	MDB_val max[1];
	byte_t buf[DB_VARINT_MAX*3][2];
};

int db_rcursor_open(MDB_txn *const txn, DB_rcursor **const out);



typedef struct {
	MDB_val min[1];
	MDB_val max[1];
} DB_range;


int mdb_cursor_seek(MDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir);
int db_cursor_first(MDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir);
int db_cursor_next(MDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir);


int db_cursor_seekr(MDB_cursor *const cursor, DB_range const *const range, MDB_val *const key, MDB_val *const data, int const dir);
int db_cursor_firstr(MDB_cursor *const cursor, DB_range const *const range, MDB_val *const key, MDB_val *const data, int const dir);
int db_cursor_nextr(MDB_cursor *const cursor, DB_range const *const range, MDB_val *const key, MDB_val *const data, int const dir);




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
	} else if(MDB_NOTFOUND != rc) return rc;
	return mdb_cursor_get(cursor, key, data, MDB_PREV);
}
int db_cursor_first(MDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	
}
int db_cursor_next(MDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	
}




int db_cursor_seekr(MDB_cursor *const cursor, DB_range const *const range, MDB_val *const key, MDB_val *const data, int const dir) {
	int rc = mdb_cursor_seek(cursor, key, data, dir);
	if(MDB_SUCCESS != rc) return rc;
	MDB_val const *const limit = dir < 0 ? range->min : range->max;
	int x = mdb_cmp(mdb_cursor_txn(cursor), key, limit);
	if(x * dir < 0) return MDB_SUCCESS;
	mdb_cursor_renew(mdb_cursor_txn(cursor), cursor);
	return MDB_NOTFOUND;
}
int db_cursor_firstr(MDB_cursor *const cursor, DB_range const *const range, MDB_val *const key, MDB_val *const data, int const dir) {
	if(!cursor) return EINVAL;
	if(0 == dir) return EINVAL;
	MDB_val const *const first = dir < 0 ? range->min : range->max;
	*key = first;
	int rc = mdb_cursor_seek(cursor, key, data, dir);
	if(MDB_SUCCESS != rc) return rc;
	int x = mdb_cmp(mdb_cursor_txn(cursor), first, key);
	if(0 == x) rc = mdb_cursor_next(cursor, key, data, dir);
	if(MDB_SUCCESS != rc) return rc;
	MDB_val const *const last = dir < 0 ? range->max : range->min;
	x = mdb_cmp(mdb_cursor_txn(cursor), key, last);
	if(x * dir < 0) return MDB_SUCCESS;
	mdb_cursor_renew(mdb_cursor_txn(cursor), cursor);
	return MDB_NOTFOUND;
}
int db_cursor_nextr(MDB_cursor *const cursor, DB_range const *const range, MDB_val *const key, MDB_val *const data, int const dir) {

}




