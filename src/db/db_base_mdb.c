// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdlib.h>
#include "db_base.h"
#include "../../deps/lsmdb/liblmdb/lmdb.h"

// MDB private definition but seems unlikely to change.
// We double check it at run time and return an error if it's different.
#define MDB_MAIN_DBI 1

struct DB_txn {
	MDB_txn *txn;
	unsigned flags;
	DB_cursor *cursor;
};

static int mdberr(int const rc) {
	return rc <= 0 ? rc : -rc;
}

int db_env_create(DB_env **const out) {
	return mdberr(mdb_env_create((MDB_env **)out));
}
int db_env_set_mapsize(DB_env *const env, size_t const size) {
	return mdberr(mdb_env_set_mapsize((MDB_env *)env, size));
}
int db_env_open(DB_env *const env, char const *const name, unsigned const flags, unsigned const mode) {
	int rc = mdberr(mdb_env_open((MDB_env *)env, name, flags | MDB_NOSUBDIR, mode));
	if(rc < 0) return rc;
	MDB_txn *txn;
	rc = mdberr(mdb_txn_begin((MDB_env *)env, NULL, 0, &txn));
	if(rc < 0) return rc;
	MDB_dbi dbi;
	rc = mdberr(mdb_dbi_open(txn, NULL, 0, &dbi));
	mdb_txn_abort(txn);
	if(rc < 0) return rc;
	if(MDB_MAIN_DBI != dbi) return DB_PANIC;
	return 0;
}
void db_env_close(DB_env *const env) {
	mdb_env_close((MDB_env *)env);
}

int db_txn_begin(DB_env *const env, DB_txn *const parent, unsigned const flags, DB_txn **const out) {
	if(!out) return DB_EINVAL;
	MDB_txn *const psub = parent ? parent->txn : NULL;
	MDB_txn *subtxn;
	int rc = mdberr(mdb_txn_begin((MDB_env *)env, psub, flags, &subtxn));
	if(rc < 0) return rc;
	DB_txn *txn = malloc(sizeof(struct DB_txn));
	if(!txn) {
		mdb_txn_abort(subtxn);
		return DB_ENOMEM;
	}
	txn->txn = subtxn;
	txn->flags = flags;
	txn->cursor = NULL;
	*out = txn;
	return 0;
}
int db_txn_commit(DB_txn *const txn) {
	if(!txn) return DB_EINVAL;
	db_cursor_close(txn->cursor);
	int rc = mdberr(mdb_txn_commit(txn->txn));
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
	int rc = mdberr(mdb_txn_renew(txn->txn));
	if(rc < 0) return rc;
	if(txn->cursor) {
		rc = db_cursor_renew(txn, &txn->cursor);
		if(rc < 0) return rc;
	}
	return 0;
}
int db_txn_get_flags(DB_txn *const txn, unsigned *const flags) {
	if(!txn) return DB_EINVAL;
	if(flags) *flags = txn->flags;
	return 0;
}
int db_txn_cmp(DB_txn *const txn, DB_val const *const a, DB_val const *const b) {
	return mdb_cmp(txn->txn, MDB_MAIN_DBI, (MDB_val *)a, (MDB_val *)b);
}
int db_txn_cursor(DB_txn *const txn, DB_cursor **const out) {
	if(!txn) return DB_EINVAL;
	if(!txn->cursor) {
		int rc = db_cursor_renew(txn, &txn->cursor);
		if(rc < 0) return rc;
	}
	if(out) *out = txn->cursor;
	return 0;
}

int db_cursor_open(DB_txn *const txn, DB_cursor **const out) {
	return mdberr(mdb_cursor_open(txn->txn, MDB_MAIN_DBI, (MDB_cursor **)out));
}
void db_cursor_close(DB_cursor *const cursor) {
	mdb_cursor_close((MDB_cursor *)cursor);
}
void db_cursor_reset(DB_cursor *const cursor) {
	// Do nothing.
}
int db_cursor_renew(DB_txn *const txn, DB_cursor **const out) {
	if(!out) return DB_EINVAL;
	if(*out) return mdberr(mdb_cursor_renew(txn->txn, (MDB_cursor *)*out));
	return mdberr(mdb_cursor_open(txn->txn, MDB_MAIN_DBI, (MDB_cursor **)out));
}
int db_cursor_clear(DB_cursor *const cursor) {
	return mdberr(mdb_cursor_renew(mdb_cursor_txn((MDB_cursor *)cursor), (MDB_cursor *)cursor));
}
int db_cursor_cmp(DB_cursor *const cursor, DB_val const *const a, DB_val const *const b) {
	assert(cursor);
	return mdb_cmp(mdb_cursor_txn((MDB_cursor *)cursor), MDB_MAIN_DBI, (MDB_val const *)a, (MDB_val const *)b);
}

int db_cursor_current(DB_cursor *const cursor, DB_val *const key, DB_val *const data) {
	if(!cursor) return DB_EINVAL;
	int rc = mdberr(mdb_cursor_get((MDB_cursor *)cursor, (MDB_val *)key, (MDB_val *)data, MDB_GET_CURRENT));
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
	int rc = mdberr(mdb_cursor_get(c, k, d, op));
	if(dir >= 0) return rc;
	if(rc >= 0) {
		MDB_txn *const txn = mdb_cursor_txn(c);
		if(0 == mdb_cmp(txn, MDB_MAIN_DBI, &orig, k)) return rc;
		return mdb_cursor_get(c, k, d, MDB_PREV);
	} else if(DB_NOTFOUND == rc) {
		return mdberr(mdb_cursor_get(c, k, d, MDB_LAST));
	} else return rc;
}
int db_cursor_first(DB_cursor *const cursor, DB_val *const key, DB_val *const data, int const dir) {
	if(0 == dir) return DB_EINVAL;
	MDB_cursor_op const op = dir < 0 ? MDB_FIRST : MDB_LAST;
	MDB_val _k[1], _d[1];
	MDB_val *const k = key ? (MDB_val *)key : _k;
	MDB_val *const d = data ? (MDB_val *)data : _d;
	return mdberr(mdb_cursor_get((MDB_cursor *)cursor, (MDB_val *)k, (MDB_val *)d, op));
}
int db_cursor_next(DB_cursor *const cursor, DB_val *const key, DB_val *const data, int const dir) {
	if(0 == dir) return DB_EINVAL;
	MDB_cursor_op const op = dir < 0 ? MDB_PREV : MDB_NEXT;
	MDB_val _k[1], _d[1];
	MDB_val *const k = key ? (MDB_val *)key : _k;
	MDB_val *const d = data ? (MDB_val *)data : _d;
	return mdberr(mdb_cursor_get((MDB_cursor *)cursor, (MDB_val *)key, (MDB_val *)data, op));
}

int db_cursor_put(DB_cursor *const cursor, DB_val *const key, DB_val *const data, unsigned const flags) {
	return mdberr(mdb_cursor_put((MDB_cursor *)cursor, (MDB_val *)key, (MDB_val *)data, flags));
}
int db_cursor_del(DB_cursor *const cursor) {
	return mdberr(mdb_cursor_del((MDB_cursor *)cursor, 0));
}

