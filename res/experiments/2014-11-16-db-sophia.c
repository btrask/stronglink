#include <assert.h>
#include <stdlib.h>
#include "db_base.h"
#include <sophia.h>

typedef struct SP_env SP_env;
typedef struct SP_db SP_db;
typedef struct SP_cursor SP_cursor;

struct DB_env {
	SP_env *env;
	SP_db *db;
};
struct DB_txn {
	DB_env *env;
	DB_cursor *cursor;
};
struct DB_cursor {
	DB_txn *txn;
	SP_cursor *cursor;
	int dir;
};


static int compare_default(DB_val const *const a, DB_val const *const b) {
	size_t const min = a->size < b->size ? a->size : b->size;
	int x = memcmp(a->data, b->data, min);
	if(0 != x) return x;
	if(a->size < b->size) return -1;
	if(a->size > b->size) return +1;
	return 0;
}


int db_env_create(DB_env **const out) {
	if(!out) return DB_EINVAL;
	DB_env *env = calloc(1, sizeof(struct DB_env));
	if(!env) return DB_ENOMEM;
	env->env = sp_env();
	if(!env->env) {
		db_env_close(env);
		return DB_ENOMEM;
	}
	*out = env;
	return DB_SUCCESS;
}
int db_env_set_mapsize(DB_env *const env, size_t const size) {
	return DB_SUCCESS;
}
int db_env_open(DB_env *const env, char const *const name, unsigned const flags, unsigned const mode) {
	if(!env) return DB_EINVAL;
	if(!name) return DB_EINVAL;
	if(!env->env) return DB_EINVAL;
	if(env->db) return DB_EINVAL;
	int rc = sp_ctl(env->env, SPDIR, SPO_RDWR | SPO_CREAT | SPO_SYNC, name);
	if(0 != rc) return -1;
	env->db = sp_open(env->env);
	if(!env->db) return -1;
	return DB_SUCCESS;
}
void db_env_close(DB_env *const env) {
	if(!env) return;
	sp_destroy(env->db);
	sp_destroy(env->env);
	free(env);
}

int db_txn_begin(DB_env *const env, DB_txn *const parent, unsigned const flags, DB_txn **const out) {
	if(!out) return DB_EINVAL;
	if(parent) return DB_EINVAL; // TODO
	int rc = sp_begin(env->db);
	if(0 != rc) return -1;
	DB_txn *txn = calloc(1, sizeof(struct DB_txn));
	if(!txn) {
		sp_rollback(env->db);
		return DB_ENOMEM;
	}
	txn->env = env;
	txn->cursor = NULL;
	*out = txn;
	return DB_SUCCESS;
}
int db_txn_commit(DB_txn *const txn) {
	if(!txn) return DB_EINVAL;
	int rc = sp_commit(txn->env->db);
	free(txn);
	if(0 != rc) return -1;
	return DB_SUCCESS;
}
void db_txn_abort(DB_txn *const txn) {
	if(!txn) return;
	int rc = sp_rollback(txn->env->db);
	free(txn);
	assert(0 == rc);
}
void db_txn_reset(DB_txn *const txn) {
	if(!txn) return;
	int rc = sp_rollback(txn->env->db);
	assert(0 == rc);
	if(txn->cursor) db_cursor_reset(txn->cursor);
}
int db_txn_renew(DB_txn *const txn) {
	if(!txn) return DB_EINVAL;
	int rc = sp_begin(txn->env->db);
	if(0 != rc) return -1;
	if(txn->cursor) {
		rc = db_cursor_renew(txn, &txn->cursor);
		if(DB_SUCCESS != rc) return rc;
	}
	return DB_SUCCESS;
}
int db_txn_get_flags(DB_txn *const txn, unsigned *const flags) {
	if(!txn) return DB_EINVAL;
	if(flags) *flags = DB_RDWR;
	return DB_SUCCESS;
}
int db_txn_cmp(DB_txn *const txn, DB_val const *const a, DB_val const *const b) {
	return compare_default(a, b);
}
int db_txn_cursor(DB_txn *const txn, DB_cursor **const out) {
	if(!txn) return DB_EINVAL;
	if(!txn->cursor) {
		int rc = db_cursor_renew(txn, &txn->cursor);
		if(DB_SUCCESS != rc) return rc;
	}
	if(out) *out = txn->cursor;
	return DB_SUCCESS;
}

int db_cursor_open(DB_txn *const txn, DB_cursor **const out) {
	if(!txn) return DB_EINVAL;
	if(!out) return DB_EINVAL;
	DB_cursor *cursor = calloc(1, sizeof(struct DB_cursor));
	if(!cursor) return DB_ENOMEM;
	cursor->txn = txn;
	*out = cursor;
	return DB_SUCCESS;
}
void db_cursor_close(DB_cursor *const cursor) {
	if(!cursor) return;
	sp_destroy(cursor->cursor);
	free(cursor);
}
void db_cursor_reset(DB_cursor *const cursor) {
	if(!cursor) return;
	sp_destroy(cursor->cursor); cursor->cursor = NULL;
	cursor->dir = 0;
}
int db_cursor_renew(DB_txn *const txn, DB_cursor **const out) {
	if(!out) return DB_EINVAL;
	if(!*out) return db_cursor_open(txn, out);
	out->txn = txn;
	return DB_SUCCESS;
}
int db_cursor_clear(DB_cursor *const cursor) {
	if(!cursor) return DB_EINVAL;
	sp_destroy(cursor->cursor); cursor->cursor = NULL;
	cursor->dir = 0;
	return DB_SUCCESS;
}
int db_cursor_cmp(DB_cursor *const cursor, DB_val const *const a, DB_val const *const b) {
	assert(cursor);
	return db_txn_cmp(cursor->txn, a, b);
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

