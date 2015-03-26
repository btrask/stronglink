

int db_cursor_seek(MDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir);
int db_cursor_first(MDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir);
int db_cursor_next(MDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir);

typedef struct {
	MDB_val min[1];
	MDB_val max[1];
} DB_range;

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
	MDB_val const *const first = dir < 0 ? range->min : range->max;
	*key = first;
	int rc = db_cursor_seek(cursor, key, data, dir);
	if(MDB_SUCCESS != rc) return rc;
	MDB_txn *const txn = mdb_cursor_txn(cursor);
	MDB_dbi const dbi = mdb_cursor_dbi(cursor);
	int x = mdb_cmp(txn, dbi, first, key);
	if(0 == x) rc = db_cursor_next(cursor, key, data, dir);
	if(MDB_SUCCESS != rc) return rc;
	MDB_val const *const last = dir < 0 ? range->max : range->min;
	x = mdb_cmp(txn, dbi, key, last);
	if(x * dir < 0) return MDB_SUCCESS;
	mdb_cursor_renew(txn, cursor);
	return MDB_NOTFOUND;
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




