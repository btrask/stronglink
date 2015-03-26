

typedef struct LDB_cursor LDB_cursor;



struct LDB_cursor {
	leveldb_iter_t *iter;
	MDB_cmp_func cmp;
	char const *key;
	char const *val;
	unsigned char valid;
};


int ldb_cursor_open(leveldb_t *const db, leveldb_readoptions_t *const ropts, MDB_cmp_func const cmp, LDB_cursor **const out) {
	if(!db) return DB_EINVAL;
	if(!ropts) return DB_EINVAL;
	if(!cmp) return DB_EINVAL;
	if(!out) return DB_EINVAL;
	LDB_cursor *cursor = calloc(1, sizeof(struct LDB_cursor));
	if(!cursor) return DB_ENOMEM;
	cursor->iter = leveldb_create_iterator(db, ropts);
	if(!cursor->iter) {
		ldb_cursor_close(cursor);
		return DB_ENOMEM;
	}
	*out = cursor;
	return DB_SUCCESS;
}
void ldb_cursor_close(LDB_cursor *const cursor) {
	if(!cursor) return;
	leveldb_iter_destroy(cursor->iter);
	cursor->cmp = NULL;
	free(cursor->key); cursor->key = NULL;
	free(cursor->val); cursor->val = NULL;
	cursor->valid = 0;
	free(cursor);
}


int ldb_cursor_current(LDB_cursor *const cursor, DB_val *const key, DB_val *const val) {
	if(!cursor) return DB_EINVAL;
	if(!cursor->valid) return DB_NOTFOUND;
	free(cursor->key); cursor->key = NULL;
	free(cursor->val); cursor->val = NULL;
	if(key) cursor->key = key->data = leveldb_iter_key(cursor->iter, &key->size);
	if(val) cursor->val = val->data = leveldb_iter_key(cursor->iter, &val->size);
	return DB_SUCCESS;
}
int ldb_cursor_seek(LDB_cursor *const cursor, DB_val *const key, DB_val *const val, int const dir) {
	if(!cursor) return DB_EINVAL;
	if(!key) return DB_EINVAL;
	DB_val const orig = *key;
	leveldb_iter_seek(cursor->iter, key->data, key->size);
	cursor->valid = !!leveldb_iter_valid(cursor->iter);
	int rc = ldb_cursor_current(cursor, key, val);
	if(dir > 0) return rc;
	if(dir < 0) {
		if(DB_SUCCESS != rc) {
			leveldb_iter_seek_to_last(cursor->iter);
		} else if(0 != cursor->cmp(key, &orig)) {
			leveldb_iter_prev(cursor->iter);
		} else return rc;
		cursor->valid = !!leveldb_iter_valid(cursor->iter);
		return ldb_cursor_current(cursor, key, val);
	}
	if(DB_SUCCESS != rc) return rc;
	if(0 == cursor->cmp(key, &orig)) return rc;
	cursor->valid = 0;
	return DB_NOTFOUND;
}
int ldb_cursor_first(LDB_cursor *const cursor, DB_val *const key, DB_val *const val, int const dir) {
	if(!cursor) return DB_EINVAL;
	if(0 == dir) return DB_EINVAL;
	if(dir > 0) leveldb_iter_seek_to_first(cursor->iter);
	if(dir < 0) leveldb_iter_seek_to_last(cursor->iter);
	cursor->valid = !!leveldb_iter_valid(cursor->iter);
	return ldb_cursor_current(cursor, key, val);
}
int ldb_cursor_next(LDB_cursor *const cursor, DB_val *const key, DB_val *const val, int const dir) {
	if(!cursor) return DB_EINVAL;
	if(0 == dir) return DB_EINVAL;
	if(dir > 0) leveldb_iter_next(cursor->iter);
	if(dir < 0) leveldb_iter_prev(cursor->iter);
	cursor->valid = !!leveldb_iter_valid(cursor->iter);
	return ldb_cursor_current(cursor, key, val);
}









