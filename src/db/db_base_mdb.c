#include <assert.h>
#include <stdlib.h>
#include "db_base.h"
#include "../../deps/lsmdb/liblmdb/lmdb.h"

#define MDB_MAIN_DBI 1

struct DB_txn {
	MDB_txn *txn;
	unsigned flags;
	DB_cursor *cursor;
};

int db_env_create(DB_env **const out) {
	return mdb_env_create((MDB_env **)out);
}
int db_env_set_mapsize(DB_env *const env, size_t const size) {
	return mdb_env_set_mapsize((MDB_env *)env, size);
}
int db_env_open(DB_env *const env, char const *const name, unsigned const flags, unsigned const mode) {
	int rc = mdb_env_open((MDB_env *)env, name, flags | MDB_NOSUBDIR, mode);
	if(MDB_SUCCESS != rc) return rc;
	MDB_txn *txn;
	rc = mdb_txn_begin((MDB_env *)env, NULL, 0, &txn);
	if(MDB_SUCCESS != rc) return rc;
	MDB_dbi dbi;
	rc = mdb_dbi_open(txn, NULL, 0, &dbi);
	mdb_txn_abort(txn);
	if(MDB_SUCCESS != rc) return rc;
	if(MDB_MAIN_DBI != dbi) return -1; /* Private API but seems unlikely to change. */
	return DB_SUCCESS;
}
void db_env_close(DB_env *const env) {
	mdb_env_close((MDB_env *)env);
}

int db_txn_begin(DB_env *const env, DB_txn *const parent, unsigned const flags, DB_txn **const out) {
	if(!out) return DB_EINVAL;
	MDB_txn *const psub = parent ? parent->txn : NULL;
	MDB_txn *subtxn;
	int rc = mdb_txn_begin((MDB_env *)env, psub, flags, &subtxn);
	if(MDB_SUCCESS != rc) return rc;
	DB_txn *txn = malloc(sizeof(struct DB_txn));
	if(!txn) {
		mdb_txn_abort(subtxn);
		return DB_ENOMEM;
	}
	txn->txn = subtxn;
	txn->flags = flags;
	txn->cursor = NULL;
	*out = txn;
	return DB_SUCCESS;
}
int db_txn_commit(DB_txn *const txn) {
	if(!txn) return DB_EINVAL;
	db_cursor_close(txn->cursor);
	int rc = mdb_txn_commit(txn->txn);
	free(txn);
	return rc;
}
void db_txn_abort(DB_txn *const txn) {
	if(!txn) return;
	db_cursor_close(txn->cursor);
	mdb_txn_abort(txn->txn);
	free(txn);
}
void db_txn_reset(DB_txn *const txn) {
	if(!txn) return;
	mdb_txn_reset(txn->txn);
}
int db_txn_renew(DB_txn *const txn) {
	if(!txn) return DB_EINVAL;
	int rc = mdb_txn_renew(txn->txn);
	if(MDB_SUCCESS != rc) return rc;
	if(txn->cursor) {
		rc = db_cursor_renew(txn, &txn->cursor);
		if(DB_SUCCESS != rc) return rc;
	}
	return DB_SUCCESS;
}
int db_txn_get_flags(DB_txn *const txn, unsigned *const flags) {
	if(!txn) return DB_EINVAL;
	if(flags) *flags = txn->flags;
	return DB_SUCCESS;
}
int db_txn_cmp(DB_txn *const txn, DB_val const *const a, DB_val const *const b) {
	return mdb_cmp(txn->txn, MDB_MAIN_DBI, (MDB_val *)a, (MDB_val *)b);
}
int db_txn_cursor(DB_txn *const txn, DB_cursor **const out) {
	if(!txn) return DB_EINVAL;
	if(!txn->cursor) {
		int rc = db_cursor_renew(txn, &txn->cursor);
		if(MDB_SUCCESS != rc) return rc;
	}
	if(out) *out = txn->cursor;
	return DB_SUCCESS;
}

int db_cursor_open(DB_txn *const txn, DB_cursor **const out) {
	return mdb_cursor_open(txn->txn, MDB_MAIN_DBI, (MDB_cursor **)out);
}
void db_cursor_close(DB_cursor *const cursor) {
	mdb_cursor_close((MDB_cursor *)cursor);
}
void db_cursor_reset(DB_cursor *const cursor) {
	// Do nothing.
}
int db_cursor_renew(DB_txn *const txn, DB_cursor **const out) {
	if(!out) return DB_EINVAL;
	if(*out) return mdb_cursor_renew(txn->txn, (MDB_cursor *)*out);
	return mdb_cursor_open(txn->txn, MDB_MAIN_DBI, (MDB_cursor **)out);
}
int db_cursor_clear(DB_cursor *const cursor) {
	return mdb_cursor_renew(mdb_cursor_txn((MDB_cursor *)cursor), (MDB_cursor *)cursor);
}
int db_cursor_cmp(DB_cursor *const cursor, DB_val const *const a, DB_val const *const b) {
	assert(cursor);
	return mdb_cmp(mdb_cursor_txn((MDB_cursor *)cursor), MDB_MAIN_DBI, (MDB_val const *)a, (MDB_val const *)b);
}

int db_cursor_current(DB_cursor *const cursor, DB_val *const key, DB_val *const data) {
	if(!cursor) return DB_EINVAL;
	int rc = mdb_cursor_get((MDB_cursor *)cursor, (MDB_val *)key, (MDB_val *)data, MDB_GET_CURRENT);
	if(DB_EINVAL == rc) return DB_NOTFOUND;
	return rc;
}
int db_cursor_seek(DB_cursor *const cursor, DB_val *const key, DB_val *const data, int const dir) {
	if(!key) return DB_EINVAL;
	MDB_cursor *const c = (MDB_cursor *)cursor;
	MDB_val *const k = (MDB_val *)key;
	MDB_val *const d = (MDB_val *)data;
	MDB_val const orig = *k;
	MDB_cursor_op const op = 0 == dir ? MDB_SET : MDB_SET_RANGE;
	int rc = mdb_cursor_get(c, k, d, op);
	if(dir >= 0) return rc;
	if(MDB_SUCCESS == rc) {
		MDB_txn *const txn = mdb_cursor_txn(c);
		if(0 == mdb_cmp(txn, MDB_MAIN_DBI, &orig, k)) return rc;
		return mdb_cursor_get(c, k, d, MDB_PREV);
	} else if(MDB_NOTFOUND == rc) {
		return mdb_cursor_get(c, k, d, MDB_LAST);
	} else return rc;
}
int db_cursor_first(DB_cursor *const cursor, DB_val *const key, DB_val *const data, int const dir) {
	if(0 == dir) return DB_EINVAL;
	MDB_cursor_op const op = dir < 0 ? MDB_FIRST : MDB_LAST;
	return mdb_cursor_get((MDB_cursor *)cursor, (MDB_val *)key, (MDB_val *)data, op);
}
int db_cursor_next(DB_cursor *const cursor, DB_val *const key, DB_val *const data, int const dir) {
	if(0 == dir) return DB_EINVAL;
	MDB_cursor_op const op = dir < 0 ? MDB_PREV : MDB_NEXT;
	return mdb_cursor_get((MDB_cursor *)cursor, (MDB_val *)key, (MDB_val *)data, op);
}

int db_cursor_put(DB_cursor *const cursor, DB_val *const key, DB_val *const data, unsigned const flags) {
	return mdb_cursor_put((MDB_cursor *)cursor, (MDB_val *)key, (MDB_val *)data, flags);
}
int db_cursor_del(DB_cursor *const cursor) {
	return mdb_cursor_del((MDB_cursor *)cursor, 0);
}

char const *db_strerror(int const err) {
	return mdb_strerror(err);
}

