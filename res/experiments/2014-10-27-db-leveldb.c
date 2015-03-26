#include <assert.h>
#include <stdlib.h>
#include "db_base.h"
#include <leveldb/c.h>
#include "../../deps/lsmdb/liblmdb/lmdb.h"

#define MDB_MAIN_DBI 1
#define MDB_RDWR 0

typedef enum {
	S_INVALID = 0,
	S_EQUAL,
	S_PENDING,
	S_PERSIST,
} DB_state;

typedef struct {
	DB_val key;
	DB_val data;
} DB_write;

struct DB_env {
	leveldb_options_t *opts;
	leveldb_filterpolicy_t *filterpolicy;
	leveldb_t *db;
	MDB_env *tmpenv;
	leveldb_writeoptions_t *wopts;
	MDB_cmp_func cmp;
};
struct DB_txn {
	DB_env *env;
	DB_txn *parent;
	unsigned flags;
	leveldb_readoptions_t *ropts;
	MDB_txn *tmptxn;
	DB_cursor *cursor;
	DB_cursor *cursors;
};
struct DB_cursor {
	DB_txn *txn;
	DB_state state;
	MDB_cursor *pending;
	LDB_cursor *persist;
	DB_cursor *prev;
	DB_cursor *next;
};


static int compare_default(MDB_val const *const a, MDB_val const *const b) {
	size_t const min = a->mv_size < b->mv_size ? a->mv_size : b->mv_size;
	int x = memcmp(a->mv_data, b->mv_data, min);
	if(0 != x) return x;
	if(a->mv_size < b->mv_size) return -1;
	if(a->mv_size > b->mv_size) return +1;
	return 0;
}


static int mdb_cursor_seek(MDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	if(!key) return EINVAL;
	MDB_val const orig = *key;
	MDB_cursor_op const op = 0 == dir ? MDB_SET : MDB_SET_RANGE;
	int rc = mdb_cursor_get(cursor, key, data, op);
	if(dir >= 0) return rc;
	if(MDB_SUCCESS == rc) {
		MDB_txn *const txn = mdb_cursor_txn(cursor);
		MDB_dbi const dbi = mdb_cursor_dbi(cursor);
		if(0 == mdb_cmp(txn, dbi, &orig, key)) return rc;
		return mdb_cursor_get(cursor, key, data, MDB_PREV);
	} else if(MDB_NOTFOUND == rc) {
		return mdb_cursor_get(cursor, key, data, MDB_LAST);
	} else return rc;
}


typedef struct LDB_cursor LDB_cursor;
struct LDB_cursor {
	leveldb_iter_t *iter;
	MDB_cmp_func cmp;
	char const *key;
	char const *val;
	unsigned char valid;
};
static int ldb_cursor_open(leveldb_t *const db, leveldb_readoptions_t *const ropts, MDB_cmp_func const cmp, LDB_cursor **const out) {
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
static void ldb_cursor_close(LDB_cursor *const cursor) {
	if(!cursor) return;
	leveldb_iter_destroy(cursor->iter);
	cursor->cmp = NULL;
	free(cursor->key); cursor->key = NULL;
	free(cursor->val); cursor->val = NULL;
	cursor->valid = 0;
	free(cursor);
}
static int ldb_cursor_current(LDB_cursor *const cursor, MDB_val *const key, MDB_val *const val) {
	if(!cursor) return DB_EINVAL;
	if(!cursor->valid) return DB_NOTFOUND;
	free(cursor->key); cursor->key = NULL;
	free(cursor->val); cursor->val = NULL;
	if(key) {
		cursor->key =
		key->mv_data = leveldb_iter_key(cursor->iter, &key->mv_size);
	}
	if(val) {
		cursor->val =
		val->mv_data = leveldb_iter_key(cursor->iter, &val->mv_size);
	}
	return DB_SUCCESS;
}
static int ldb_cursor_seek(LDB_cursor *const cursor, MDB_val *const key, MDB_val *const val, int const dir) {
	if(!cursor) return DB_EINVAL;
	if(!key) return DB_EINVAL;
	MDB_val const orig = *key;
	leveldb_iter_seek(cursor->iter, key->mv_data, key->mv_size);
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
static int ldb_cursor_first(LDB_cursor *const cursor, MDB_val *const key, MDB_val *const val, int const dir) {
	if(!cursor) return DB_EINVAL;
	if(0 == dir) return DB_EINVAL;
	if(dir > 0) leveldb_iter_seek_to_first(cursor->iter);
	if(dir < 0) leveldb_iter_seek_to_last(cursor->iter);
	cursor->valid = !!leveldb_iter_valid(cursor->iter);
	return ldb_cursor_current(cursor, key, val);
}
static int ldb_cursor_next(LDB_cursor *const cursor, MDB_val *const key, MDB_val *const val, int const dir) {
	if(!cursor) return DB_EINVAL;
	if(!cursor->valid) return ldb_cursor_first(cursor, key, val, dir);
	if(0 == dir) return DB_EINVAL;
	if(dir > 0) leveldb_iter_next(cursor->iter);
	if(dir < 0) leveldb_iter_prev(cursor->iter);
	cursor->valid = !!leveldb_iter_valid(cursor->iter);
	return ldb_cursor_current(cursor, key, val);
}


int db_env_create(DB_env **const out) {
	DB_env *env = calloc(1, sizeof(struct DB_env));
	if(!env) return DB_ENOMEM;

	env->opts = leveldb_options_create();
	if(!env->opts) {
		db_env_close(env);
		return DB_ENOMEM;
	}
	leveldb_options_set_create_if_missing(env->opts, 1);

	env->filterpolicy = leveldb_filterpolicy_create_bloom(10);
	if(!env->filterpolicy) {
		db_env_close(env);
		return DB_ENOMEM;
	}
	leveldb_options_set_filter_policy(env->opts, env->filterpolicy);

	int rc = mdb_env_create(env->tmpenv);
	if(MDB_SUCCESS != rc) {
		db_env_close(env);
		return rc;
	}

	env->wopts = leveldb_writeoptions_create();
	if(!env->wopts) {
		db_env_close(env);
		return DB_ENOMEM;
	}
	leveldb_writeoptions_set_sync(env->wopts, 1);

	env->cmp = compare_default;
	*out = env;
	return DB_SUCCESS;
}
int db_env_set_mapsize(DB_env *const env, size_t const size) {
	return DB_SUCCESS;
}
int db_env_open(DB_env *const env, char const *const name, unsigned const flags, unsigned const mode) {
	if(!env) return DB_EINVAL;
	char *err = NULL;
	env->db = leveldb_open(env->opts, name, &err);
	free(err);
	if(!env->db || err) return -1;


	char tmppath[512];
	sprintf(tmppath, "%s/tmp.mdb", name); // TODO: Error checking.
	int rc = mdb_env_open(env->tmpenv, tmppath, MDB_NOSUBDIR | MDB_WRITEMAP, 0600);
	if(MDB_SUCCESS != rc) return rc;

	MDB_txn *tmptxn;
	rc = mdb_txn_begin(env->tmpenv, NULL, MDB_RDWR, &tmptxn);
	assert(!rc);
	MDB_dbi dbi;
	rc = mdb_dbi_open(tmptxn, NULL, 0, &dbi);
	assert(!rc);
	if(MDB_MAIN_DBI != dbi) assert(0);
	rc = mdb_txn_commit(tmptxn);
	assert(!rc);


	leveldb_writeoptions_set_sync(env->wopts, !(DB_NOSYNC & flags));
	return DB_SUCCESS;
}
void db_env_close(DB_env *const env) {
	if(!env) return;
	leveldb_filterpolicy_destroy(env->filterpolicy);
	leveldb_options_destroy(env->opts);
	leveldb_close(env->db);
	leveldb_writeoptions_destroy(env->wopts);
	mdb_env_close(env->tmpenv); // TODO: Unlink it, either right away or on close?
	free(env);
}

int db_txn_begin(DB_env *const env, DB_txn *const parent, unsigned const flags, DB_txn **const out) {
	if(!env) return DB_EINVAL;
	if(!out) return DB_EINVAL;

	MDB_txn *tmptxn = NULL;
	if(!(DB_RDONLY & flags)) {
		MDB_txn *p = parent ? parent->tmptxn : NULL;
		int rc = mdb_txn_begin(env->tmpenv, p, flags, &tmptxn);
		if(MDB_SUCCESS != rc) return rc;
	}

	DB_txn *txn = calloc(1, sizeof(struct DB_txn));
	if(!txn) {
		pthread_mutex_unlock(env->wlock);
		return DB_ENOMEM;
	}
	txn->env = env;
	txn->parent = parent;
	txn->flags = flags;
	txn->ropts = leveldb_readoptions_create();
	if(!txn->ropts) {
		db_txn_abort(txn);
		return DB_ENOMEM;
	}
	if(DB_RDONLY & flags) {
		int rc = db_txn_renew(txn);
		if(DB_SUCCESS != rc) {
			db_txn_abort(txn);
			return rc;
		}
	} else {
		txn->tmptxn = tmptxn;
	}
	return DB_SUCCESS;
}
int db_txn_commit(DB_txn *const txn) {
	if(!txn) return DB_EINVAL;
	if(DB_RDONLY & txn->flags) {
		db_txn_abort(txn);
		return DB_SUCCESS;
	}

	if(txn->parent) {
		assert(0); // TODO
	}

	leveldb_writebatch_t *batch = leveldb_writebatch_create();
	if(!batch) {
		db_txn_abort(txn);
		return DB_ENOMEM;
	}
	MDB_cursor *cursor = NULL;
	int rc = mdb_cursor_open(txn->tmptxn, MDB_MAIN_DBI, &cursor);
	assert(!rc);
	rc = mdb_cursor_get(cursor, key, data, MDB_FIRST);
	for(; MDB_SUCCESS == rc; rc = mdb_cursor_get(cursor, key, data, MDB_NEXT) {
		leveldb_writebatch_put(batch,
			key->mv_data, key->mv_size,
			data->mv_data, data->mv_size);
	}
	mdb_cursor_close(cursor); cursor = NULL;

	char *err = NULL;
	leveldb_write(env->db, env->wopts, batch, &err);
	free(err);
	leveldb_writebatch_destroy(batch);
	if(err) {
		db_txn_abort(txn);
		return -1;
	}
	db_txn_abort(txn);
	return DB_SUCCESS;
}
void db_txn_abort(DB_txn *const txn) {
	if(!txn) return;
	db_cursor_close(txn->cursor);
	DB_cursor *x = txn->cursors;
	while(x) {
		DB_cursor *const xx = x;
		x = x->next;
		db_cursor_close(xx);
	}
	leveldb_readoptions_destroy(txn->ropts);
	mdb_txn_abort(txn->tmptxn); txn->tmptxn = NULL;
	free(txn);
}
void db_txn_reset(DB_txn *const txn) {
	if(!txn) return;
	assert(txn->flags & DB_RDONLY);
	leveldb_readoptions_set_snapshot(txn->ropts, NULL);
}
int db_txn_renew(DB_txn *const txn) {
	// TODO: If renew fails, does the user have to explicitly abort?
	if(!txn) return DB_EINVAL;
	assert(txn->flags & DB_RDONLY);
	leveldb_snapshot_t *snapshot = leveldb_create_snapshot(env->db);
	if(!snapshot) return DB_ENOMEM;
	leveldb_readoptions_set_snapshot(snapshot);
	return DB_SUCCESS;
}
int db_txn_get_flags(DB_txn *const txn, unsigned *const flags) {
	if(!txn) return DB_EINVAL;
	if(flags) *flags = txn->flags;
	return DB_SUCCESS;
}
int db_txn_cmp(DB_txn *const txn, DB_val const *const a, DB_val const *const b) {
	if(!txn) return DB_EINVAL;
	return txn->env->cmp((MDB_val *)a, (MDB_val *)b);
}
int db_txn_cursor(DB_txn *const txn, DB_cursor **const out) {
	if(!txn) return DB_EINVAL;
	if(!txn->cursor) {
		int rc = db_cursor_open(txn, txn->cursor);
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
	*out = cursor;
	return db_cursor_renew(txn, out);
}
void db_cursor_close(DB_cursor *const cursor) {
	if(!cursor) return;
	db_cursor_reset(cursor);
	free(cursor);
}
void db_cursor_reset(DB_cursor *const cursor) {
	if(!cursor) return;
	if(cursor->prev) cursor->prev->next = cursor->next;
	if(cursor->next) cursor->next->prev = cursor->prev;
	cursor->state = S_INVALID;
	mdb_cursor_close(cursor->pending); cursor->pending = NULL;
	ldb_cursor_close(cursor->persist); cursor->persist = NULL;
}
int db_cursor_renew(DB_txn *const txn, DB_cursor **const out) {
	if(!out) return DB_EINVAL;
	if(!*out) return db_cursor_open(txn, out);
	cursor->txn = txn;
	cursor->state = S_INVALID;
	int rc = mdb_cursor_open(txn->tmptxn, MDB_MAIN_DBI, &cursor->pending);
	if(MDB_SUCCESS != rc) return rc;
	rc = ldb_cursor_open(txn->env->db, txn->ropts, txn->env->cmp, &cursor->persist);
	if(DB_SUCCESS != rc) return rc;
	return DB_SUCCESS;
}
int db_cursor_clear(DB_cursor *const cursor) {
	if(!cursor) return DB_EINVAL;
	cursor->state = S_INVALID;
	return DB_SUCCESS;
}
int db_cursor_cmp(DB_cursor *const cursor, DB_val const *const a, DB_val const *const b) {
	return db_txn_cmp(cursor->txn, a, b);
}



static int db_cursor_update(DB_cursor *const cursor, int const rc1, DB_val const *const k1, DB_val const *const d1, int const rc2, DB_val const *const k2, DB_val const *const d2, int const dir, DB_val *const key, DB_val *const data) {
	cursor->state = S_INVALID;
	if(MDB_SUCCESS != rc1 && MDB_NOTFOUND != rc1) return rc1;
	if(MDB_SUCCESS != rc2 && MDB_NOTFOUND != rc2) return rc2;
	if(MDB_NOTFOUND == rc1 && MDB_NOTFOUND == rc2) return MDB_NOTFOUND;
	int x;
	if(MDB_NOTFOUND == rc1) x = +1;
	if(MDB_NOTFOUND == rc2) x = -1;
	if(0 == x) x = cursor->txn->cmp(k1, k2) * (dir ? dir : 1);
	if(x <= 0) {
		cursor->state = 0 == x ? S_EQUAL : S_PENDING;
		*key = *k1;
		*data = *d1;
	} else {
		cursor->state = S_PERSIST;
		*key = *k2;
		*data = *d2;
	}
	return MDB_SUCCESS;
}
int db_cursor_current(DB_cursor *const cursor, DB_val *const key, DB_val *const data) {
	if(!cursor) return DB_EINVAL;
	switch(cursor->state) {
		case S_INVALID: return DB_NOTFOUND;
		case S_EQUAL:
		case S_PENDING:
			return mdb_cursor_get(cursor->pending, (MDB_val *)key, (MDB_val *)data, MDB_GET_CURRENT);
		case S_PERSIST:
			return ldb_cursor_current(cursor->persist, key, val);
	}
}
int db_cursor_seek(DB_cursor *const cursor, DB_val *const key, DB_val *const data, int const dir) {
	if(!cursor) return DB_EINVAL;
	MDB_val k1 = *key, d1;
	MDB_val k2 = *key, d2;
	int rc1 = mdb_cursor_seek(cursor->pending, &k1, &d1, dir);
	int rc2 = ldb_cursor_seek(cursor->persist, &k2, &d2, dir);
	return db_cursor_update(cursor, rc1, &k1, &d1, rc2, &k2, &d2, dir, key, data);
}
int db_cursor_first(DB_cursor *const cursor, DB_val *const key, DB_val *const data, int const dir) {
	if(!cursor) return DB_EINVAL;
	if(0 == dir) return DB_EINVAL;
	MDB_val k1, d1, k2, d2;
	MDB_cursor_op const op = dir < 0 ? MDB_LAST : MDB_FIRST;
	int rc1 = mdb_cursor_get(cursor->pending, &k1, &d1, op);
	int rc2 = ldb_cursor_first(cursor->persist, &k2, &d2, dir);
	return db_cursor_update(cursor, rc1, &k1, &d1, rc2, &k2, &d2, dir, key, data);
}
int db_cursor_next(DB_cursor *const cursor, DB_val *const key, DB_val *const data, int const dir) {
	if(!cursor) return DB_EINVAL;
	if(0 == dir) return DB_EINVAL;
	int rc1, rc2;
	MDB_val k1, d1, k2, d2;
	if(S_PERSIST != cursor->state) {
		MDB_cursor_op const op = dir < 0 ? MDB_PREV : MDB_NEXT;
		rc1 = mdb_cursor_get(cursor->pending, &k1, &d1, op);
	} else {
		rc1 = mdb_cursor_get(cursor->pending, &k1, &d1, MDB_GET_CURRENT);
	}
	if(S_PENDING != cursor->state) {
		rc2 = ldb_cursor_next(cursor->persist, &k2, &d2, dir);
	} else {
		rc2 = ldb_cursor_current(cursor->persist, &k2, &d2);
	}
	return db_cursor_update(cursor, rc1, &k1, &d1, rc2, &k2, &d2, dir, key, data);
}

int db_cursor_put(DB_cursor *const cursor, DB_val const *const key, DB_val const *const data, unsigned const flags) {
	if(!cursor) return DB_EINVAL;
	if(DB_NOOVERWRITE & flags) {
		DB_val k = *key, d;
		int rc = db_cursor_seek(cursor, &k, &d, 0);
		if(DB_SUCCESS == rc) {
			*key = k;
			*data = d;
			return DB_KEYEXIST;
		}
		if(DB_NOTFOUND != rc) return rc;
	}
	return mdb_cursor_put(cursor->pending, (MDB_val *)key, (MDB_val *)data, 0);
}
int db_cursor_del(DB_cursor *const cursor) {
	return DB_EINVAL; // TODO
}

char const *db_strerror(int const err) {
	return mdb_strerror(err);
}

