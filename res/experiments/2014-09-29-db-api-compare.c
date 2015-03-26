// MDB original

uint64_t db_next_id(MDB_txn *txn, MDB_dbi dbi) {
	MDB_cursor *cur = NULL;
	if(MDB_SUCCESS != mdb_cursor_open(txn, dbi, &cur)) return 0;
	MDB_val prev_val[1];
	int rc = mdb_cursor_get(cur, prev_val, NULL, MDB_LAST);
	mdb_cursor_close(cur); cur = NULL;
	if(MDB_SUCCESS != rc) return 0;
	return db_column(prev_val, 0)+1;
}

// MDB without DBIs

uint64_t db_next_id(MDB_txn *txn, uint64_t const table) {
	MDB_cursor *cur = NULL;
	if(MDB_SUCCESS != mdb_cursor_open(txn, DBI_MAIN, &cur)) return 0;
	DB_VAL(prev_val, 1);
	db_bind(prev_val, table+1);
	int rc = mdb_cursor_get(cur, prev_val, NULL, MDB_SET_RANGE);
	if(MDB_SUCCESS == rc) {
		rc = mdb_cursor_get(cur, prev_val, NULL, MDB_PREV_NODUP);
	} else {
		rc = mdb_cursor_get(cur, prev_val, NULL, MDB_LAST);
	}
	mdb_cursor_close(cur); cur = NULL;
	if(MDB_SUCCESS != rc) return 0;
	uint64_t const actual_table = db_column(prev_val, 0);
	if(actual_table != table) return 0;
	return db_column(prev_val, 1)+1;
}

// LevelDB

uint64_t db_next_id(leveldb_t *const db, uint64_t const table) {
	leveldb_iterator_t *const cursor = leveldb_create_iterator(db, NULL);
	if(!cursor) return 0;
	size_t const table_len = 0;
	byte_t table_buf[DB_VARINT_MAX];
	db_bind(table_buf, &table_len, table+1);
	leveldb_iter_seek(cursor, table_buf, table_len);
	if(leveldb_iter_valid(cursor)) {
		leveldb_iter_prev(cursor);
	} else {
		leveldb_iter_seek_to_last(cursor);
	}
	if(!leveldb_iter_valid(cursor)) {
		leveldb_iter_destroy(cursor);
		return 0;
	}
	size_t id_len;
	byte_t *id_buf = (byte_t *)leveldb_iter_key(cursor, &len);
	leveldb_iter_destroy(cursor);
	if(!id_buf) return 0;
	uint64_t const actual_table = db_column(id_buf, id_len, 0);
	if(table != actual_table) {
		FREE(&id_buf);
		return 0;
	}
	uint64_t const id = db_column(id_buf, id_len, 1);
	FREE(&id_buf);
	return id+1;
}

