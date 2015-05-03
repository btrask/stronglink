// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdlib.h>
#include "db_base.h"
#include "../../deps/lsmdb/lsmdb.h"

int db_env_create(DB_env **const out) {
	return lsmdb_env_create((LSMDB_env **)out);
}
int db_env_set_mapsize(DB_env *const env, size_t const size) {
	return lsmdb_env_set_mapsize((LSMDB_env *)env, size);
}
int db_env_open(DB_env *const env, char const *const name, unsigned const flags, unsigned const mode) {
	return lsmdb_env_open((LSMDB_env *)env, name, flags | MDB_NOSUBDIR, mode);
}
void db_env_close(DB_env *const env) {
	lsmdb_env_close((LSMDB_env *)env);
}

int db_txn_begin(DB_env *const env, DB_txn *const parent, unsigned const flags, DB_txn **const out) {
	return lsmdb_txn_begin((LSMDB_env *)env, (LSMDB_txn *)parent, flags, (LSMDB_txn **)out);
}
int db_txn_commit(DB_txn *const txn) {
	int rc = lsmdb_autocompact((LSMDB_txn *)txn);
	if(MDB_SUCCESS != rc) {
		lsmdb_txn_abort((LSMDB_txn *)txn);
		return rc;
	}
	return lsmdb_txn_commit((LSMDB_txn *)txn);
}
void db_txn_abort(DB_txn *const txn) {
	lsmdb_txn_abort((LSMDB_txn *)txn);
}
void db_txn_reset(DB_txn *const txn) {
	lsmdb_txn_reset((LSMDB_txn *)txn);
}
int db_txn_renew(DB_txn *const txn) {
	return lsmdb_txn_renew((LSMDB_txn *)txn);
}
int db_txn_get_flags(DB_txn *const txn, unsigned *const flags) {
	return lsmdb_txn_get_flags((LSMDB_txn *)txn, flags);
}
int db_txn_cmp(DB_txn *const txn, DB_val const *const a, DB_val const *const b) {
	return lsmdb_cmp((LSMDB_txn *)txn, (MDB_val *)a, (MDB_val *)b);
}
int db_txn_cursor(DB_txn *const txn, DB_cursor **const out) {
	return lsmdb_txn_cursor((LSMDB_txn *)txn, (LSMDB_cursor **)out);
}

int db_cursor_open(DB_txn *const txn, DB_cursor **const out) {
	return lsmdb_cursor_open((LSMDB_txn *)txn, (LSMDB_cursor **)out);
}
void db_cursor_close(DB_cursor *const cursor) {
	lsmdb_cursor_close((LSMDB_cursor *)cursor);
}
void db_cursor_reset(DB_cursor *const cursor) {
	// Do nothing.
}
int db_cursor_renew(DB_txn *const txn, DB_cursor **const out) {
	if(!out) return DB_EINVAL;
	if(*out) return lsmdb_cursor_renew((LSMDB_txn *)txn, (LSMDB_cursor *)*out);
	return lsmdb_cursor_open((LSMDB_txn *)txn, (LSMDB_cursor **)out);
}
int db_cursor_clear(DB_cursor *const cursor) {
	return lsmdb_cursor_clear((LSMDB_cursor *)cursor);
}
int db_cursor_cmp(DB_cursor *const cursor, DB_val const *const a, DB_val const *const b) {
	return lsmdb_cmp(lsmdb_cursor_txn((LSMDB_cursor *)cursor), (MDB_val *)a, (MDB_val *)b);
}

int db_cursor_current(DB_cursor *const cursor, DB_val *const key, DB_val *const data) {
	return lsmdb_cursor_current((LSMDB_cursor *)cursor, (MDB_val *)key, (MDB_val *)data);
}
int db_cursor_seek(DB_cursor *const cursor, DB_val *const key, DB_val *const data, int const dir) {
	return lsmdb_cursor_seek((LSMDB_cursor *)cursor, (MDB_val *)key, (MDB_val *)data, dir);
}
int db_cursor_first(DB_cursor *const cursor, DB_val *const key, DB_val *const data, int const dir) {
	return lsmdb_cursor_first((LSMDB_cursor *)cursor, (MDB_val *)key, (MDB_val *)data, dir);
}
int db_cursor_next(DB_cursor *const cursor, DB_val *const key, DB_val *const data, int const dir) {
	return lsmdb_cursor_next((LSMDB_cursor *)cursor, (MDB_val *)key, (MDB_val *)data, dir);
}

int db_cursor_put(DB_cursor *const cursor, DB_val *const key, DB_val *const data, unsigned const flags) {
	return lsmdb_cursor_put((LSMDB_cursor *)cursor, (MDB_val *)key, (MDB_val *)data, flags);
}
int db_cursor_del(DB_cursor *const cursor) {
	return lsmdb_cursor_del((LSMDB_cursor *)cursor);
}

char const *db_strerror(int const err) {
	return mdb_strerror(err);
}

