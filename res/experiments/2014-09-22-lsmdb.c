#include "../deps/liblmdb/lmdb.h"

#define LSMDB_RDWR   0
#define LSMDB_RDONLY MDB_RDONLY

#define LSMDB_LSMTREE ((LSMDB_level)0x00)
#define LSMDB_BTREE   ((LSMDB_level)0xff)

typedef uint8_t LSMDB_level;

typedef struct LSMDB_env LSMDB_env;
typedef struct LSMDB_txn LSMDB_txn;
typedef struct LSMDB_cursor LSMDB_cursor;

int lsmdb_get_mode(MDB_val const *const key, LSMDB_level *const out);

int lsmdb_env_create(LSMDB_env **const env);
int lsmdb_env_open(LSMDB_env *const env, char const *const path, unsigned const flags, mdb_mode_t const mode);
MDB_env *lsmdb_env_internal(LSMDB_env *const env);
void lsmdb_env_close(LSMDB_env *const env);

int lsmdb_txn_begin(LSMDB_env *const env, LSMDB_txn *const parent, unsigned const flags, LSMDB_txn **const txn);
int lsmdb_txn_commit(LSMDB_txn *const txn);
void lsmdb_txn_abort(LSMDB_txn *const txn);
void lsmdb_txn_reset(LSMDB_txn *const txn);
int lsmdb_txn_renew(LSMDB_txn *const txn);

int lsmdb_get(LSMDB_txn *const txn, MDB_val *const key, MDB_val *const data);
int lsmdb_put(LSMDB_txn *const txn, MDB_val *const key, MDB_val *const data, unsigned const flags);
int lsmdb_del(LSMDB_txn *const txn, MDB_val *const key);

int lsmdb_cursor_open(LSMDB_txn *const txn, LSMDB_cursor **const cursor);
void lsmdb_cursor_close(LSMDB_cursor *const cursor);
int lsmdb_cursor_renew(LSMDB_txn *const txn, LSMDB_cursor *const cursor);

int lsmdb_cursor_get(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data);
int lsmdb_cursor_jump(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, LSMDB_cursor_op const dir);
int lsmdb_cursor_step(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir);
//int lsmdb_cursor_put(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, unsigned const flags);
//int lsmdb_cursor_del(LSMDB_cursor *const cursor, MDB_val *const key);

int lsmdb_env_set_autocompact(LSMDB_env *const env, int const val);
int lsmdb_env_set_mergefactor(LSMDB_env *const env, unsigned const factor);
int lsmdb_compact(LSMDB_txn *const txn, LSMDB_level const level, unsigned const steps);
int lsmdb_compact_auto(LSMDB_txn *const txn);

int lsmdb_cmp(LSMDB_txn *const txn, MDB_val const *const a, MDB_val const *const b);
int lsmdb_check_prefix(LSMDB_txn *const txn, MDB_val const *const key, MDB_val const *const pfx);


#define LSMDB_LSM_MAX ((LSMDB_level)0x0f)
#define LSMDB_STATS   ((LSMDB_level)0x80)

struct LSMDB_env {
	MDB_env *env;
	MDB_dbi dbi;
	int autocompact;
	unsigned mergefactor;
};
struct LSMDB_txn {
	LSMDB_env *env;
	LSMDB_txn *parent;
	MDB_txn *txn;

	unsigned flags;
	MDB_cursor *hi;
	MDB_cursor *lo;
	LSMDB_level depth;
	unsigned writes;

	uint8_t *scratch;
	size_t scratchsize;
};

typedef struct {
	MDB_cursor *cursor;
	LSMDB_level level;
} LSMDB_sorted_cursor;
struct LSMDB_cursor {
	LSMDB_txn *txn;

	LSMDB_level count;
	MDB_cursor **cursors;
	LSMDB_sorted_cursor *sorted;
	int dir;
};


static int mdb_cursor_set(MDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	MDB_val orig = *key;
	MDB_cursor_op const op = 0 == dir ? MDB_SET : MDB_SET_RANGE;
	int rc = mdb_cursor_get(cursor, key, data, op);
	if(-1 != dir) return rc;
	if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
	MDB_txn *const txn = mdb_cursor_txn(cursor);
	MDB_dbi const dbi = mdb_cursor_dbi(cursor);
	if(0 == mdb_cmp(txn, dbi, &orig, key)) return MDB_SUCCESS;
	return mdb_cursor_get(cursor, &key, &data, MDB_PREV);
}

static int lsmdb_get_level(MDB_val const *const key, LSMDB_level *const out) {
	if(!key) return EINVAL;
	if(key->mv_size > 1) {
		LSMDB_level const level = ((uint8_t *)key->mv_data)[0];
		if(level <= LSMDB_LSM_MAX) {
			*out = level;
			return MDB_SUCCESS;
		}
		if(LSMDB_BTREE == level) {
			*out = LSMDB_BTREE;
			return MDB_SUCCESS;
		}
	}
	return EINVAL;
}
int lsmdb_get_mode(MDB_val const *const key, LSMDB_level *const out) {
	int rc = lsmdb_level(key, out);
	if(MDB_SUCCESS != rc) return rc;
	if(*out <= LSMDB_LSM_MAX) *out = LSMDB_LSMTREE;
	return MDB_SUCCESS;
}
static int lsmdb_check_level(MDB_val const *const key, LSMDB_level const level) {
	LSMDB_level x;
	int rc = lsmdb_get_level(key, &x);
	if(MDB_SUCCESS != rc) return rc;
	if(level != x) return MDB_NOTFOUND;
	return MDB_SUCCESS;
}

static int lsmdb_cmp_default(MDB_val const *const a, MDB_val const *const b) {
	int x = memcmp(a->mv_data, b->mv_data, MIN(a->mv_size, b->mv_size));
	if(0 != x) return x;
	return a->mv_size - b->mv_size;
}
static int lsmdb_cmp_wrapper(MDB_val const *const a, MDB_val const *const b) {
	int x = a->mv_data[0] - b->mv_data[0];
	if(0 != x) return x;
	return lsmdb_cmp(a, b);
}

int lsmdb_env_create(LSMDB_env **const out) {
	if(!out) return EINVAL;
	LSMDB_env *env = malloc(sizeof(struct LSMDB_env));
	if(!env) return ENOMEM;
	int rc = mdb_env_create(&env->env);
	if(MDB_SUCCESS != rc) return rc;
	env->dbi = 0;
	env->autocompact = 1;
	env->mergefactor = 10;
	*out = env;
	return env;
}
int lsmdb_env_open(LSMDB_env *const env, char const *const path, unsigned const flags, mdb_mode_t const mode) {
	if(!env) return EINVAL;
	int rc = mdb_env_open(env->env, path, flags, mode);
	if(MDB_SUCCESS != rc) return rc;
	MDB_txn *txn = NULL;
	rc = mdb_txn_begin(env->env, NULL, MDB_RDONLY, &txn);
	if(MDB_SUCCESS != rc) return rc;
	rc = mdb_dbi_open(txn, NULL, 0, &env->dbi);
	if(MDB_SUCCESS != rc) {
		mdb_txn_abort(txn);
		return rc;
	}
	rc = mdb_set_compare(txn, env->dbi, lsmdb_cmp_wrapper);
	if(MDB_SUCCESS != rc) {
		mdb_txn_abort(txn);
		env->dbi = 0;
		return rc;
	}
	rc = mdb_txn_commit(txn);
	if(MDB_SUCCESS != rc) {
		env->dbi = 0;
		return rc;
	}
	return rc;
}
MDB_env *lsmdb_env_internal(LSMDB_env *const env) {
	if(!env) return EINVAL;
	return env->env;
}
void lsmdb_env_close(LSMDB_env *const env) {
	if(!env) return;
	mdb_env_close(env->env);
	free(env);
}


int lsmdb_txn_begin(LSMDB_env *const env, LSMDB_txn *const parent, unsigned const flags, LSMDB_txn **const out) {
	if(!env) return EINVAL;
	if(!out) return EINVAL;
	LSMDB_txn *const txn = calloc(1, sizeof(struct LSMDB_txn));
	if(!txn) return ENOMEM;
	txn->env = env;
	txn->flags = flags;
	MDB_txn *const ptxn = parent ? parent->txn : NULL;
	int rc = mdb_txn_begin(env->env, ptxn, flags, &txn->txn);		
	if(MDB_SUCCESS != rc) {
		lsmdb_txn_abort(txn);
		return rc;
	}
	if(parent) {
		txn->lo = parent->lo;
		txn->hi = parent->hi;
		txn->depth = parent->depth;
	} else {
		rc = rc ? rc : mdb_cursor_open(txn->txn, txn->env->dbi, &txn->hi);
		rc = rc ? rc : mdb_cursor_open(txn->txn, txn->env->dbi, &txn->lo);
		if(MDB_SUCCESS != rc) {
			lsmdb_txn_abort(txn);
			return rc;
		}
		uint8_t buf[] = { LSMDB_STATS, 0x00 };
		MDB_val key = { sizeof(buf), buf };
		MDB_val depth;
		rc = mdb_cursor_get(txn->hi, &key, &depth, MDB_SET);
		if(MDB_SUCCESS == rc) {
			if(1 != depth->mv_size) {
				lsmdb_txn_abort(txn);
				return MDB_CORRUPTED;
			}
			txn->depth = ((uint8_t *)depth->mv_data)[0];
			if(txn->depth < 1) txn->depth = 1;
			if(txn->depth > LSMDB_LSM_MAX) {
				lsmdb_txn_abort(txn);
				return MDB_CORRUPTED;
			}
		} else if(MDB_NOTFOUND == rc) {
			txn->depth = 1;
		} else {
			lsmdb_txn_abort(txn);
			return rc;
		}
	}
	txn->writes = 0;
	*out = txn;
	return MDB_SUCCESS;
}
int lsmdb_txn_commit(LSMDB_txn *const txn) {
	if(!txn) return EINVAL;
	int rc = MDB_SUCCESS;
	if(!(MDB_RDONLY & txn->flags)) {
		if(MDB_SUCCESS == rc) {
			rc = lsmdb_compact(txn, 0, txn->writes);
		}
		if(MDB_SUCCESS == rc && txn->env->autocompact) {
			rc = lsmdb_compact_auto(txn);
		}
	}
	if(!txn->parent) {
		mdb_cursor_close(txn->hi);
		mdb_cursor_close(txn->lo);
	}
	if(MDB_SUCCESS == rc) {
		rc = mdb_txn_commit(txn->txn);
		if(MDB_SUCCESS == rc && txn->parent) {
			txn->parent->depth = txn->depth;
			txn->parent->writes += txn->writes;
		}
	} else {
		mdb_txn_abort(txn->txn);
	}
	free(txn->scratch);
	free(txn);
	return rc;
}
void lsmdb_txn_abort(LSMDB_txn *const txn) {
	if(!txn) return;
	if(!txn->parent) {
		mdb_cursor_close(txn->hi);
		mdb_cursor_close(txn->lo);
	}
	mdb_txn_abort(txn->txn);
	free(txn->scratch);
	free(txn);
}
void lsmdb_txn_reset(LSMDB_txn *const txn) {
	if(!txn) return;
	mdb_txn_reset(txn->txn);
	txn->writes = 0;
	txn->depth = 0;
}
int lsmdb_txn_renew(LSMDB_txn *const txn) {
	int rc = mdb_txn_renew(txn->txn);
	if(MDB_SUCCESS != rc) return rc;
	rc = rc ? rc : mdb_cursor_renew(txn->txn, txn->hi);
	rc = rc ? rc : mdb_cursor_renew(txn->txn, txn->lo);
	return rc;
}
static uint8_t *lsmdb_txn_scratch(LSMDB_txn *const txn, MDB_val const *const key) {
	if(txn->scratchsize < key->mv_size) {
		free(txn->scratch);
		txn->scratchsize = key->mv_size;
		txn->scratch = malloc(txn->scratchsize);
		if(!txn->scratch) return NULL;
	}
	memcpy(txn->scratch, key->mv_data, key->mv_size);
	return txn->scratch;
}


int lsmdb_get(LSMDB_txn *const txn, MDB_val *const key, MDB_val *const data) {
	if(!txn) return EINVAL;
	LSMDB_level mode;
	int rc = lsmdb_get_mode(key, &mode);
	if(MDB_SUCCESS != rc) return rc;
	if(LSMDB_BTREE == mode) return mdb_cursor_get(txn->hi, key, data, MDB_SET);
	if(LSMDB_LSMTREE != mode) return EINVAL;

	rc = mdb_cursor_get(txn->lo, key, data, MDB_SET);
	if(MDB_NOTFOUND != rc) return rc;
	uint8_t *buf = lsmdb_txn_scratch(txn, key);
	if(!buf) return ENOMEM;
	for(LSMDB_level i = 1; i < txn->depth; ++i) {
		MDB_val xkey = { key->mv_size, buf };
		buf[0] = i;
		rc = mdb_cursor_get(txn->hi, &xkey, data, MDB_SET);
		if(MDB_NOTFOUND != rc) return rc;
	}
	return MDB_NOTFOUND;
}
int lsmdb_put(LSMDB_txn *const txn, MDB_val *const key, MDB_val *const data, unsigned const flags) {
	if(!txn) return EINVAL;
	LSMDB_level mode;
	int rc = lsmdb_get_mode(key, &mode);
	if(MDB_SUCCESS != rc) return rc;
	if(LSMDB_BTREE == mode) return mdb_cursor_put(txn->hi, key, data, flags);
	if(LSMDB_LSMTREE != mode) return EINVAL;

	if(MDB_NOOVERWRITE & flags) {
		rc = lsmdb_get(txn, key, data);
		if(MDB_SUCCESS == rc) return MDB_KEYEXIST;
		if(MDB_NOTFOUND != rc) return rc;
	}
	return mdb_cursor_put(txn->lo, key, data, flags);
}
int lsmdb_del(LSMDB_txn *const txn, MDB_val *const key) {
	if(!txn) return EINVAL;
	LSMDB_level mode;
	int rc = lsmdb_get_mode(key, &mode);
	if(MDB_SUCCESS != rc) return rc;
	if(LSMDB_BTREE == mode) return mdb_cursor_del(txn->hi, key, NULL);
	if(LSMDB_LSMTREE != mode) return EINVAL;

	uint8_t *buf = lsmdb_txn_scratch(txn, key);
	LSMDB_level i = txn->depth;
	int found = 0;
	while(i-- > 1) {
		MDB_key xkey = { key->mv_size, buf };
		buf[0] = i;
		rc = mdb_cursor_del(txn->hi, &xkey, NULL);
		if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
		if(MDB_SUCCESS == rc) found = 1;
	}
	rc = mdb_cursor_del(txn->lo, key, NULL);
	if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
	if(MDB_SUCCESS == rc) found = 1;
	return found ? MDB_SUCCESS : MDB_NOTFOUND;
}

int lsmdb_cursor_open(LSMDB_txn *const txn, LSMDB_cursor **const out) {
	if(!txn) return EINVAL;
	if(!out) return EINVAL;
	if(LSMDB_LSMTREE != mode && LSMDB_BTREE != mode) return EINVAL;
	LSMDB_cursor *cursor = malloc(sizeof(struct LSMDB_cursor));
	if(!cursor) return ENOMEM;
	cursor->txn = txn;
	cursor->count = 0;
	cursor->cursors = NULL;
	cursor->dir = 0;
	*out = cursor;
	return MDB_SUCCESS;
}
void lsmdb_cursor_close(LSMDB_cursor *const cursor) {
	if(!cursor) return;
	for(LSMDB_level i = 0; i < cursor->count; ++i) {
		mdb_cursor_close(cursor->cursors[i]);
	}
	free(cursor->cursors);
	free(cursor->sorted);
	free(cursor);
}
int lsmdb_cursor_renew(LSMDB_txn *const txn, LSMDB_cursor *const cursor) {
	if(!txn) return EINVAL;
	if(!cursor) return EINVAL;
	cursor->txn = txn;
	for(LSMDB_level i = 0; i < cursor->count; ++i) {
		int rc = mdb_cursor_renew(txn->txn, cursor->cursors[i]);
		if(MDB_SUCCESS != rc) return rc;
	}
	return MDB_SUCCESS;
}

static int lsmdb_cursor_cmp_fwd(LSMDB_sorted_cursor const *const *const a, LSMDB_sorted_cursor const *const *const b) {
	MDB_val va, vb;
	int rca = mdb_cursor_get((*a)->cursor, &va, NULL, MDB_GET_CURRENT);
	int rcb = mdb_cursor_get((*b)->cursor, &vb, NULL, MDB_GET_CURRENT);
	if(MDB_SUCCESS == rca || MDB_SUCCESS == rcb) {
		if(MDB_SUCCESS != rca) return +1;
		if(MDB_SUCCESS != rcb) return -1;
		int x = lsmdb_cmp(NULL, &va, &vb);
		if(0 != x) return x;
	}
	return ((*a)->level - (*b)->level);
}
static int lsmdb_cursor_cmp_rev(void *const a, void *const b) {
	return lsmdb_cursor_cmp_fwd(a, b) * -1;
}
static void lsmdb_cursor_sort(LSMDB_cursor *const cursor, int const dir) {
	if(0 == dir) return;
	int (*cmp)();
	if(dir > 0) cmp = lsmdb_cursor_cmp_fwd;
	if(dir < 0) cmp = lsmdb_cursor_cmp_rev;
	qsort(cursor->sorted, cursor->count, sizeof(LSMDB_sorted_cursor), cmp);
	cursor->dir = dir;
}
static int lsmdb_cursor_grow(LSMDB_cursor *const cursor, LSMDB_level const mode) {
	if(!cursor) return EINVAL;
	if(LSMDB_LSMTREE != mode && LSMDB_BTREE != mode) return EINVAL;
	LSMDB_level const ocount = cursor->count;
	LSMDB_level const ncount = LSMDB_LSMTREE == mode ? cursor->txn->depth : 1;
	if(ncount <= ocount) return MDB_SUCCESS;
	MDB_cursor **const a = realloc(cursor->cursors, sizeof(MDB_cursor *) * ncount);
	MDB_cursor **const b = realloc(cursor->sorted, sizeof(LSMDB_sorted_cursor) * ncount);
	if(!a || !b) return ENOMEM;
	cursor->cursors = a;
	cursor->sorted = b;

	uint8_t *buf = NULL;
	MDB_val current;
	rc = lsmdb_cursor_get(cursor, &current, NULL);
	if(MDB_SUCCESS == rc) {
		buf = lsmdb_txn_scratch(cursor->txn, &current);
		if(!buf) return ENOMEM;
	}

	int const olddir = cursor->dir;
	cursor->dir = 0;
	for(LSMDB_level i = ocount; i < ncount; ++i) {
		LSMDB_txn *const txn = cursor->txn;
		MDB_dbi const dbi = txn->env->dbi;
		MDB_cursor *c = NULL;
		int rc = mdb_cursor_open(txn, dbi, &c);
		if(MDB_SUCCESS != rc) return rc;
		cursor->cursors[i] = c;
		cursor->sorted[i].cursor = c;
		cursor->sorted[i].level = i;
		cursor->count++;
		if(buf) {
			buf[0] = i;
			MDB_val key = { current.mv_size, buf };
			rc = mdb_cursor_set(c, &key, NULL, olddir);
			if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
		}
	}
	if(LSMDB_LSMTREE == mode) lsmdb_cursor_sort(cursor, olddir);
	return MDB_SUCCESS;
}
int lsmdb_cursor_get(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data) {
	if(!cursor) return EINVAL;
	if(0 == cursor->dir) return EINVAL;
	if(cursor->count < 1) return EINVAL;
	MDB_val k, d;
	MDB_cursor *const c = cursor->sorted[0].cursor;
	rc = mdb_cursor_get(c, &k, &d, MDB_GET_CURRENT);
	if(MDB_SUCCESS != rc) return rc;
	if(key) *key = k;
	if(data) *data = d;
	return MDB_SUCCESS;
}
int lsmdb_cursor_set(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	if(!cursor) return EINVAL;
	LSMDB_level mode;
	int rc = lsmdb_get_mode(key, &mode);
	if(MDB_SUCCESS != rc) return rc;
	rc = lsmdb_cursor_grow(cursor, mode);
	if(MDB_SUCCESS != rc) return rc;

	uint8_t *buf = lsmdb_txn_scratch(cursor->txn, key);
	if(!buf) return ENOMEM;

	cursor->dir = 0;
	for(LSMDB_level i = 0; i < cursor->count; ++i) {
		MDB_cursor *const c = cursor->cursors[i].cursor;
		MDB_val xkey = { key->mv_size, buf };
		buf[0] = LSMDB_LSMTREE == mode ? i : mode;
		rc = mdb_cursor_set(c, &xkey, NULL, dir);
		if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
		rc = lsmdb_check_level(&xkey, i);
		if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
		if(MDB_SUCCESS != rc) mdb_cursor_renew(cursor->txn->txn, c);
		if(LSMDB_LSMTREE != mode) break;
	}
	if(LSMDB_LSMTREE == mode) lsmdb_cursor_sort(cursor, dir);
	return lsmdb_cursor_get(cursor, key, data);
}
int lsmdb_cursor_step(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	if(!cursor) return EINVAL;
	if(0 == dir) return EINVAL;

	MDB_val pos;
	rc = lsmdb_cursor_get(cursor, &pos, NULL);
	if(MDB_SUCCESS != rc) return rc;

	LSMDB_level mode;
	int rc = lsmdb_get_mode(&pos, &mode);
	if(MDB_SUCCESS != rc) return MDB_CORRUPTED;

	MDB_cursor_op const step = dir < 0 ? MDB_PREV : MDB_NEXT;
	int const olddir = cursor->dir;
	cursor->dir = 0;

	LSMDB_level i = 0;
	for(; i < cursor->count; ++i) {
		MDB_cursor *const c = cursors->sorted[i].cursor;
		MDB_val xkey;
		rc = mdb_cursor_get(c, &xkey, NULL, MDB_GET_CURRENT);
		if(MDB_SUCESS != rc) break;
		if(0 != lsmdb_cmp(cursor->txn, &x, &pos)) break;
		rc = mdb_cursor_get(c, &xkey, NULL, step);
		if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
		rc = lsmdb_check_level(&xkey, i);
		if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
		if(MDB_NOTFOUND == rc) mdb_cursor_renew(cursor->txn->txn, c);
		if(LSMDB_LSMTREE != mode) break;
	}

	if(LSMDB_LSMTREE != mode) return lsmdb_cursor_get(cursor, key, data);

	// Flip direction. Cursors that were ahead before have to be ahead after.
	if(i < cursor->count && (dir > 0) != (olddir > 0)) {
		uint8_t *buf = lsmdb_txn_scratch(cursor->txn, &pos);
		if(!buf) return ENOMEM;
		for(; i < cursor->count; ++i) {
			MDB_cursor *const c = cursors->sorted[i].cursor;
			MDB_val xkey = { pos->mv_size, buf };
			buf[0] = i;
			rc = mdb_cursor_set(c, &xkey, NULL, dir);
			if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
			rc = lsmdb_check_level(&xkey, i);
			if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
			if(MDB_NOTFOUND == rc) mdb_cursor_renew(cursor->txn->txn, c);
		}
	}
	lsmdb_cursor_sort(cursor, dir);
	return lsmdb_cursor_get(cursor, key, data);
}



int lsmdb_env_set_autocompact(LSMDB_env *const env, int const val) {
	if(!env) return EINVAL;
	env->autocompact = !!val;
	return MDB_SUCCESS;
}
int lsmdb_env_set_mergefactor(LSMDB_env *const env, unsigned const factor) {
	if(!env) return EINVAL;
	if(factor <= 1) return EINVAL;
	env->mergefactor = factor;
	return MDB_SUCCESS;
}
static int lsmdb_compact_step(LSMDB_txn *const txn, LSMDB_level const level, int const dir) {
	if(0 == dir) return EINVAL;
	MDB_cursor_op const step = dir < 0 ? MDB_PREV : MDB_NEXT;

	// TODO: We need a mdb_cursor_step(cursor, pfx, key, data, dir)
	MDB_val skey;
	MDB_val sdata;
	rc = mdb_cursor_get(txn->lo, &skey, &sdata, step);
	if(MDB_NOTFOUND == rc) return MDB_SUCCESS;
	if(MDB_SUCCESS != rc) return rc;
	rc = lsmdb_check_level(&skey, level+0);
	if(MDB_NOTFOUND == rc) return MDB_SUCCESS;
	if(MDB_SUCCESS != rc) return rc;

	for(;;) {
		MDB_val dkey;
		MDB_val ddata;
		rc = mbd_cursor_get(txn->hi, &dkey, &ddata, step);
		if(MDB_NOTFOUND == rc) break;
		if(MDB_SUCCESS != rc) return rc;
		rc = lsmdb_check_level(&dkey, level+1);
		if(MDB_NOTFOUND == rc) break;
		if(MDB_SUCCESS != rc) return rc;
		if(lsmdb_cmp(txn, &skey, &dkey)*dir >= 0) break;
	}

	uint8_t *const buf = lsmdb_txn_scratch(txn, &skey);
	if(!buf) return ENOMEM;
	MDB_val xkey = { skey->mv_size, buf };
	buf[0] = level+1;
	rc = mdb_cursor_put(txn->hi, &xkey, &sdata, 0);
	if(MDB_SUCCESS != rc) return rc;

	rc = mdb_cursor_del(txn->lo, 0);
	if(MDB_SUCCESS != rc) return rc;

	return MDB_SUCCESS;
}
int lsmdb_compact(LSMDB_txn *const txn, LSMDB_level const level, unsigned const steps) {
	if(!txn) return EINVAL;
	if(txn->flags & MDB_RDONLY) return EACCES;
	if(LSMDB_LSM_MAX == level) return MDB_SUCCESS;
	if(level > LSMDB_LSM_MAX) return EINVAL;
	// TODO: Skip compaction if txn has open cursors.

	int rc;
	int const dir = -1; // Start from the end of the level.
	LSMDB_level lo = level+0 + (dir < 0 ? 1 : 0);
	LSMDB_level hi = level+1 + (dir < 0 ? 1 : 0);
	MDB_val lo_v = { 1, &lo };
	MDB_val hi_v = { 1, &hi };
	rc = mdb_cursor_set(txn->lo, lo_v, NULL, -1*dir);
	if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
	rc = mdb_cursor_set(txn->hi, hi_v, NULL, -1*dir);
	if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;

	unsigned i = 0;
	for(; i < steps; ++i) {
		rc = lsmdb_compact_step(txn, level, dir);
		if(MDB_SUCCESS != rc) break;
	}

done:
	if(i > 0) {

		// TODO: Subtract i from level-n count, add i to level-m count
		// If level is zero, then don't subtract, we don't bother counting those

		if(level == txn->depth) txn->depth++;
		if(0 == level) txn->writes = txn->writes > i ? txn->writes - i : 0;

	}
	return rc;
}
int lsmdb_compact_auto(LSMDB_txn *const txn) {
	if(!txn) return EINVAL;



	uint8_t buf[] = { LSMDB_STATS, 1 };
	MDB_val vlevel = {};
	MDB_val vsize;
	mdb_cursor_get(txn->hi, &level, &size, MDB_SET);



// when we begin a new level
// it should be possible for each previous level to become 1/factor full

// how much merging is necessary as the database grows?

// writes * levels?


// how to represent our statistics on disk?
// fixed length uint64_t?












// besides the number of items in each level
// what other data do we need to store?

// the initial number should be the number of levels
// every transaction has to read it, so make it as fast as possible









}

int lsmdb_cmp(LSMDB_txn *const txn, MDB_val const *const a, MDB_val const *const b) {
	MDB_val xa = { a->mv_size-1, a->mv_data+1 };
	MDB_val xb = { b->mv_size-1, b->mv_data+1 };
	return lsmdb_cmp_default(&xa, &xb);
}
int lsmdb_check_prefix(LSMDB_txn *const txn, MDB_val const *const key, MDB_val const *const pfx) {
	if(key->mv_size < pfx->mv_size) return MDB_NOTFOUND;
	MDB_val x = { pfx->mv_size, key->mv_data };
	if(0 != lsmdb_cmp(txn, pfx, &x)) return MDB_NOTFOUND;
	return MDB_SUCCESS;
}











