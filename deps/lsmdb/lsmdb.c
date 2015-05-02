/* Copyright Ben Trask 2014-2015
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted only as authorized by the OpenLDAP
 * Public License.
 *
 * A copy of this license is available in the file LICENSE in the
 * top-level directory of the distribution or, alternatively, at
 * <http://www.OpenLDAP.org/license.html>.
 */
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "lsmdb.h"

#define LEVEL_MAX 10

#define TABLES_PER_LEVEL 3
#define CURSORS_PER_LEVEL 2
#define TABLE_MAX (LEVEL_MAX * TABLES_PER_LEVEL)
#define CURSOR_MAX (LEVEL_MAX * CURSORS_PER_LEVEL)

#define MDB_DBI_START 2
#define LSMDB_UNUSED_DBI (MDB_DBI_START+0x00)
#define LSMDB_WRITE_DBI (MDB_DBI_START+0x01)
#define LSMDB_META_DBI (MDB_DBI_START+0x02)

#define META_STATE 0x00

#define C_INITIALIZED (1<<0)

#define CX_LEVEL_BASE 5000
#define CX_LEVEL_GROWTH 10
#define CX_MERGE_BATCH 20000

typedef uint8_t LSMDB_state;
enum {
	STATE_NIL = 0,
	STATE_ABC = 1,
	STATE_ACB = 2,
	STATE_BAC = 3,
	STATE_BCA = 4,
	STATE_CAB = 5,
	STATE_CBA = 6,
	STATE_MAX,
};

struct LSMDB_env {
	MDB_env *env;
};
struct LSMDB_txn {
	LSMDB_env *env;
	LSMDB_txn *parent;
	unsigned flags;
	MDB_txn *txn;

	LSMDB_state state[LEVEL_MAX-1]; // TODO: Preserve state[0]
	LSMDB_cursor *cursor;
};

typedef struct {
	MDB_cursor *cursor;
	LSMDB_level level;
	uint8_t flags;
} LSMDB_xcursor;
struct LSMDB_cursor {
	LSMDB_txn *txn;
	unsigned depth;
	LSMDB_xcursor cursors[CURSOR_MAX][1];
	LSMDB_xcursor *sorted[CURSOR_MAX];
	int dir;
};


/* DEBUG */
static char *tohex(MDB_val const *const x) {
	char const *const map = "0123456789abcdef";
	char const *const buf = x->mv_data;
	char *const hex = calloc(x->mv_size*2+1, 1);
	for(off_t i = 0; i < x->mv_size; ++i) {
		hex[i*2+0] = map[0xf & (buf[i] >> 4)];
		hex[i*2+1] = map[0xf & (buf[i] >> 0)];
	}
	return hex;
}

/*#include <time.h>
static uint64_t now(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * (uint64_t) 1e9 + t.tv_nsec;
}*/



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



static LSMDB_state lsmdb_swap_12(LSMDB_state const x) {
	switch(x) {
		case STATE_ABC: return STATE_BAC;
		case STATE_ACB: return STATE_CAB;
		case STATE_BAC: return STATE_ABC;
		case STATE_BCA: return STATE_CBA;
		case STATE_CAB: return STATE_ACB;
		case STATE_CBA: return STATE_BCA;
		default: assert(0); return 0;
	}
}
static LSMDB_state lsmdb_swap_23(LSMDB_state const x) {
	switch(x) {
		case STATE_ABC: return STATE_ACB;
		case STATE_ACB: return STATE_ABC;
		case STATE_BAC: return STATE_BCA;
		case STATE_BCA: return STATE_BAC;
		case STATE_CAB: return STATE_CBA;
		case STATE_CBA: return STATE_CAB;
		default: assert(0); return 0;
	}
}



int lsmdb_env_create(LSMDB_env **const out) {
	if(!out) return EINVAL;
	LSMDB_env *env = calloc(1, sizeof(struct LSMDB_env));
	if(!env) return ENOMEM;
	int rc = mdb_env_create(&env->env);
	if(MDB_SUCCESS != rc) {
		free(env);
		return rc;
	}
	rc = mdb_env_set_maxdbs(env->env, TABLE_MAX);
	assert(MDB_SUCCESS == rc);
	*out = env;
	return MDB_SUCCESS;
}
int lsmdb_env_set_mapsize(LSMDB_env *const env, size_t const size) {
	if(!env) return EINVAL;
	return mdb_env_set_mapsize(env->env, size);
}
int lsmdb_env_open(LSMDB_env *const env, char const *const name, unsigned const flags, mdb_mode_t const mode) {
	if(!env) return EINVAL;
	int rc = mdb_env_open(env->env, name, flags, mode);
	if(MDB_SUCCESS != rc) return rc;

	MDB_txn *txn = NULL;
	rc = mdb_txn_begin(env->env, NULL, MDB_RDWR, &txn);
	assert(MDB_SUCCESS == rc);

	char const *const map = "0123456789abcdef";
	for(LSMDB_level i = 0; i < TABLE_MAX; ++i) {
		char table[3] = { map[i/16], map[i%16], '\0' };
		MDB_dbi dbi;
		rc = mdb_dbi_open(txn, table, MDB_CREATE, &dbi);
		assert(MDB_SUCCESS == rc);
		if(dbi != MDB_DBI_START+i) {
			mdb_txn_abort(txn);
			return MDB_INCOMPATIBLE;
		}
	}
	// TODO: Parse meta-data and make sure LEVEL_MAX isn't too small

	rc = mdb_txn_commit(txn);
	assert(MDB_SUCCESS == rc);

	return MDB_SUCCESS;
}
void lsmdb_env_close(LSMDB_env *const env) {
	if(!env) return;
	mdb_env_close(env->env);
	free(env);
}


static int lsmdb_state_load(LSMDB_txn *const txn) {
	uint8_t k = META_STATE;
	MDB_val key[1] = {{ sizeof(k), &k }}, val[1];
	int rc = mdb_get(txn->txn, LSMDB_META_DBI, key, val);
	if(MDB_NOTFOUND == rc) {
		rc = MDB_SUCCESS;
		val->mv_size = 0;
		val->mv_data = NULL;
	}
	if(MDB_SUCCESS != rc) return rc;
	// TODO: Ignore extra levels as long as they are STATE_NONE.
	if(val->mv_size > LEVEL_MAX-1) return MDB_INCOMPATIBLE;
	uint8_t const *const state = val->mv_data;
	for(LSMDB_level i = 0; i < LEVEL_MAX; ++i) {
		LSMDB_state const x = i < val->mv_size ? state[i] : STATE_NIL;
		if(x >= STATE_MAX) return MDB_INCOMPATIBLE;
		txn->state[i] = x;
	}
	return MDB_SUCCESS;
}
static int lsmdb_state_store(LSMDB_txn *const txn) {
	uint8_t k = META_STATE;
	MDB_val key = { sizeof(k), &k };
	MDB_val val = { sizeof(txn->state), txn->state };
	return mdb_put(txn->txn, LSMDB_META_DBI, &key, &val, 0);
}
static int lsmdb_level_state(LSMDB_txn *const txn, LSMDB_level const level, MDB_dbi *const prev, MDB_dbi *const next, MDB_dbi *const pend) {
	if(0 == level) {
		*prev = LSMDB_WRITE_DBI;
		*next = LSMDB_WRITE_DBI;
		*pend = LSMDB_WRITE_DBI;
		return MDB_SUCCESS;
	}
	MDB_dbi const a = MDB_DBI_START+(level*3)+0x00;
	MDB_dbi const b = MDB_DBI_START+(level*3)+0x01;
	MDB_dbi const c = MDB_DBI_START+(level*3)+0x02;
	switch(txn->state[level-1]) {
		case STATE_NIL: return MDB_NOTFOUND;
		case STATE_ABC: *prev = a; *next = b; *pend = c; break;
		case STATE_ACB: *prev = a; *next = c; *pend = b; break;
		case STATE_BAC: *prev = b; *next = a; *pend = c; break;
		case STATE_BCA: *prev = b; *next = c; *pend = a; break;
		case STATE_CAB: *prev = c; *next = a; *pend = b; break;
		case STATE_CBA: *prev = c; *next = b; *pend = a; break;
		default: assert(0); return EINVAL;
	}
	return MDB_SUCCESS;
}

int lsmdb_txn_begin(LSMDB_env *const env, LSMDB_txn *const parent, unsigned const flags, LSMDB_txn **const out) {
	if(!env) return EINVAL;
	LSMDB_txn *const txn = calloc(1, sizeof(struct LSMDB_txn));
	if(!txn) return ENOMEM;
	txn->env = env;
	txn->parent = parent;
	txn->flags = flags;
	MDB_txn *const mparent = parent ? parent->txn : NULL;
	int rc = mdb_txn_begin(env->env, mparent, flags, &txn->txn);
	rc = rc ? rc : lsmdb_state_load(txn);
	if(MDB_SUCCESS != rc) {
		lsmdb_txn_abort(txn);
		return rc;
	}
	*out = txn;
	return MDB_SUCCESS;
}
int lsmdb_txn_commit(LSMDB_txn *const txn) {
	if(!txn) return EINVAL;
	lsmdb_cursor_close(txn->cursor);
	int rc = mdb_txn_commit(txn->txn);
	free(txn);
	return rc;
}
void lsmdb_txn_abort(LSMDB_txn *const txn) {
	if(!txn) return;
	lsmdb_cursor_close(txn->cursor);
	mdb_txn_abort(txn->txn);
	free(txn);
}
void lsmdb_txn_reset(LSMDB_txn *const txn) {
	if(!txn) return;
	mdb_txn_reset(txn->txn);
}
int lsmdb_txn_renew(LSMDB_txn *const txn) {
	if(!txn) return EINVAL;
	int rc = mdb_txn_renew(txn->txn);
	if(MDB_SUCCESS != rc) return rc;
	if(txn->cursor) {
		rc = lsmdb_cursor_renew(txn, txn->cursor);
		if(MDB_SUCCESS != rc) return rc;
	}
	return MDB_SUCCESS;
}
int lsmdb_txn_get_flags(LSMDB_txn *const txn, unsigned *const flags) {
	if(!txn) return EINVAL;
	if(flags) *flags = txn->flags;
	return MDB_SUCCESS;
}
int lsmdb_txn_cursor(LSMDB_txn *const txn, LSMDB_cursor **const out) {
	if(!txn) return EINVAL;
	if(!txn->cursor) {
		int rc = lsmdb_cursor_open(txn, &txn->cursor);
		if(MDB_SUCCESS != rc) return rc;
	}
	if(out) *out = txn->cursor;
	return MDB_SUCCESS;
}


static int lsmdb_cursor_del0(LSMDB_cursor *const cursor, MDB_val const *const key) {
	if(!cursor) return EINVAL;
	for(LSMDB_level i = 0; i < CURSOR_MAX; ++i) {
		LSMDB_xcursor *const xc = cursor->cursors[i];
		if(!xc->cursor) continue;
		xc->flags &= ~C_INITIALIZED;
		int rc = mdb_cursor_get(xc->cursor, (MDB_val *)key, NULL, MDB_SET);
		if(MDB_SUCCESS != rc) continue;
		rc = mdb_cursor_del(xc->cursor, 0);
		if(MDB_SUCCESS != rc) return rc;
	}
	return MDB_SUCCESS;
}

int lsmdb_get(LSMDB_txn *const txn, MDB_val const *const key, MDB_val *const data) {
	int rc = lsmdb_txn_cursor(txn, NULL);
	if(MDB_SUCCESS != rc) return rc;
	return lsmdb_cursor_seek(txn->cursor, (MDB_val *)key, data, 0);
}
int lsmdb_put(LSMDB_txn *const txn, MDB_val const *const key, MDB_val const *const data, unsigned const flags) {
	int rc = lsmdb_txn_cursor(txn, NULL);
	if(MDB_SUCCESS != rc) return rc;
	return lsmdb_cursor_put(txn->cursor, key, data, flags);
}
int lsmdb_del(LSMDB_txn *const txn, MDB_val const *const key) {
	int rc = lsmdb_txn_cursor(txn, NULL);
	if(MDB_SUCCESS != rc) return rc;
	return lsmdb_cursor_del0(txn->cursor, key);
}
int lsmdb_cmp(LSMDB_txn *const txn, MDB_val const *const a, MDB_val const *const b) {
	return mdb_cmp(txn->txn, LSMDB_WRITE_DBI, a, b);
}


static int lsmdb_cursor_load(LSMDB_cursor *const cursor) {
	cursor->dir = 0;
	for(LSMDB_level i = 0; i < CURSOR_MAX; ++i) {
		cursor->cursors[i]->level = i;
		cursor->cursors[i]->flags = 0;
		cursor->sorted[i] = cursor->cursors[i];
	}
	for(LSMDB_level i = 0; i < LEVEL_MAX; ++i) {
		LSMDB_xcursor *const x0 = cursor->cursors[i*2+0];
		LSMDB_xcursor *const x1 = cursor->cursors[i*2+1];
		if(x0->cursor) {
			mdb_cursor_close(x0->cursor);
			x0->cursor = NULL;
		}
		if(x1->cursor) {
			mdb_cursor_close(x1->cursor);
			x1->cursor = NULL;
		}
		MDB_dbi prev, next, pend;
		int rc = lsmdb_level_state(cursor->txn, i, &prev, &next, &pend);
		if(MDB_NOTFOUND == rc) break;
		if(MDB_SUCCESS != rc) return rc;
//		fprintf(stderr, "level: %d, prev: %d, next: %d, pend: %d\n", i, prev, next, pend);
		rc = mdb_cursor_open(cursor->txn->txn, next, &x0->cursor);
		if(MDB_SUCCESS != rc) return rc;
		if(0 == i) continue; // Level 0 doesn't have a prev table.
		rc = mdb_cursor_open(cursor->txn->txn, prev, &x1->cursor);
		if(MDB_SUCCESS != rc) return rc;
	}
	return MDB_SUCCESS;
}

static int lsmdb_cursor_cmp(LSMDB_xcursor const *const *const a, LSMDB_xcursor const *const *const b, int const dir) {
	if(!(*a)->cursor && !(*b)->cursor) goto eq;
	if(!(*a)->cursor) return +1;
	if(!(*b)->cursor) return -1;
	if(!(C_INITIALIZED & (*a)->flags) && !(C_INITIALIZED & (*b)->flags)) goto eq;
	if(!(C_INITIALIZED & (*a)->flags)) return +1;
	if(!(C_INITIALIZED & (*b)->flags)) return -1;
	MDB_val k1, k2;
	int rc1 = mdb_cursor_get((*a)->cursor, &k1, NULL, MDB_GET_CURRENT);
	int rc2 = mdb_cursor_get((*b)->cursor, &k2, NULL, MDB_GET_CURRENT);
	if(MDB_SUCCESS != rc1 && MDB_SUCCESS != rc2) goto eq;
	if(MDB_SUCCESS != rc1) return +1;
	if(MDB_SUCCESS != rc2) return -1;
	MDB_txn *const txn = mdb_cursor_txn((*a)->cursor);
	MDB_dbi const dbi = mdb_cursor_dbi((*a)->cursor);
	int x = mdb_cmp(txn, dbi, &k1, &k2) * dir;
	if(0 != x) return x;
eq:
	return (*a)->level - (*b)->level;
}
static int lsmdb_cursor_cmp_fwd(void *const a, void *const b) {
	return lsmdb_cursor_cmp(a, b, +1);
}
static int lsmdb_cursor_cmp_rev(void *const a, void *const b) {
	return lsmdb_cursor_cmp(a, b, -1);
}
static void lsmdb_cursor_sort(LSMDB_cursor *const cursor, int const dir) {
	assert(0 != dir);
	int (*cmp)();
	if(dir > 0) cmp = lsmdb_cursor_cmp_fwd;
	if(dir < 0) cmp = lsmdb_cursor_cmp_rev;
	qsort(cursor->sorted, CURSOR_MAX, sizeof(LSMDB_xcursor *), cmp);
	cursor->dir = dir;
}

int lsmdb_cursor_open(LSMDB_txn *const txn, LSMDB_cursor **const out) {
	if(!txn) return EINVAL;
	LSMDB_cursor *cursor = calloc(1, sizeof(struct LSMDB_cursor));
	if(!cursor) return ENOMEM;
	cursor->txn = txn;
	int rc = lsmdb_cursor_load(cursor);
assert(!rc);
	if(MDB_SUCCESS != rc) {
		lsmdb_cursor_close(cursor);
		return rc;
	}
	*out = cursor;
	return MDB_SUCCESS;
}
void lsmdb_cursor_close(LSMDB_cursor *const cursor) {
	if(!cursor) return;
	for(LSMDB_level i = 0; i < CURSOR_MAX; ++i) {
		mdb_cursor_close(cursor->cursors[i]->cursor);
		cursor->cursors[i]->cursor = NULL;
	}
	free(cursor);
}
int lsmdb_cursor_renew(LSMDB_txn *const txn, LSMDB_cursor *const cursor) {
	if(!txn) return EINVAL;
	if(!cursor) return EINVAL;
	cursor->txn = txn;
	return lsmdb_cursor_load(cursor);
}
int lsmdb_cursor_clear(LSMDB_cursor *const cursor) {
	if(!cursor) return EINVAL;
	for(LSMDB_level i = 0; i < CURSOR_MAX; ++i) {
		MDB_cursor *const c = cursor->cursors[i]->cursor;
		if(!c) continue;
		int rc = mdb_cursor_renew(cursor->txn->txn, c);
		if(MDB_SUCCESS != rc) return rc;
	}
	return MDB_SUCCESS;
}
LSMDB_txn *lsmdb_cursor_txn(LSMDB_cursor *const cursor) {
	if(!cursor) return NULL;
	return cursor->txn;
}


int lsmdb_cursor_get(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, MDB_cursor_op const op) {
	switch(op) {
		case MDB_GET_CURRENT: return lsmdb_cursor_current(cursor, key, data);
		case MDB_SET: return lsmdb_cursor_seek(cursor, key, data, 0);
		case MDB_SET_RANGE: return lsmdb_cursor_seek(cursor, key, data, +1);
		case MDB_FIRST: return lsmdb_cursor_first(cursor, key, data, +1);
		case MDB_LAST: return lsmdb_cursor_first(cursor, key, data, -1);
		case MDB_PREV: return lsmdb_cursor_next(cursor, key, data, -1);
		case MDB_NEXT: return lsmdb_cursor_next(cursor, key, data, +1);
		default: return EINVAL;
	}
}
int lsmdb_cursor_current(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data) {
	if(!cursor) return EINVAL;
	if(0 == cursor->dir) return MDB_NOTFOUND;
	LSMDB_xcursor *const xc = cursor->sorted[0];
	if(!xc->cursor) return MDB_NOTFOUND;
	if(!(C_INITIALIZED & xc->flags)) return MDB_NOTFOUND;
	MDB_val _k[1], _d[1];
	MDB_val *const k = key ? (MDB_val *)key : _k;
	MDB_val *const d = data ? (MDB_val *)data : _d;
	return mdb_cursor_get(xc->cursor, k, d, MDB_GET_CURRENT);
}
int lsmdb_cursor_seek(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	if(!cursor) return EINVAL;
	cursor->dir = 0;
	for(LSMDB_level i = 0; i < CURSOR_MAX; ++i) {
		LSMDB_xcursor *const xc = cursor->cursors[i];
		if(!xc->cursor) continue;
		MDB_val k = *key;
		int rc = mdb_cursor_seek(xc->cursor, &k, NULL, dir);
		if(MDB_SUCCESS == rc) xc->flags |= C_INITIALIZED;
		else xc->flags &= ~C_INITIALIZED;
		if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
	}
	lsmdb_cursor_sort(cursor, dir ? dir : +1);
	int rc = lsmdb_cursor_current(cursor, key, data);
	if(EINVAL == rc) return MDB_NOTFOUND;
	return rc;
}
int lsmdb_cursor_first(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	if(!cursor) return EINVAL;
	if(0 == dir) return EINVAL;
	MDB_cursor_op const op = dir > 0 ? MDB_FIRST : MDB_LAST;
	cursor->dir = 0;
	for(LSMDB_level i = 0; i < CURSOR_MAX; ++i) {
		LSMDB_xcursor *const xc = cursor->cursors[i];
		if(!xc->cursor) continue;
		MDB_val ignore;
		int rc = mdb_cursor_get(xc->cursor, &ignore, NULL, op);
		if(MDB_SUCCESS == rc) xc->flags |= C_INITIALIZED;
		else xc->flags &= ~C_INITIALIZED;
		if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
	}
	lsmdb_cursor_sort(cursor, dir);
	int rc = lsmdb_cursor_current(cursor, key, data);
	if(EINVAL == rc) return MDB_NOTFOUND;
	return rc;
}
int lsmdb_cursor_next(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	if(!cursor) return EINVAL;
	if(0 == cursor->dir) return lsmdb_cursor_first(cursor, key, data, dir);
	if(0 == dir) return EINVAL;

	MDB_val orig;
	int rc = lsmdb_cursor_current(cursor, &orig, NULL);
	if(MDB_SUCCESS != rc) return rc;

	MDB_cursor_op const op = dir < 0 ? MDB_PREV : MDB_NEXT;
	int const flipped = (dir > 0) != (cursor->dir > 0);
	int const olddir = cursor->dir;
	cursor->dir = 0;

	for(LSMDB_level i = 0; i < CURSOR_MAX; ++i) {
		LSMDB_xcursor *const xc = cursor->sorted[i];
		if(!xc->cursor) break;
		MDB_val k;
		if(C_INITIALIZED & xc->flags) {
			rc = mdb_cursor_get(xc->cursor, &k, NULL, MDB_GET_CURRENT);
		} else {
			rc = MDB_NOTFOUND;
		}
		int const current =
			MDB_SUCCESS == rc &&
			0 == lsmdb_cmp(cursor->txn, &orig, &k);
		if(current) {
			rc = mdb_cursor_get(xc->cursor, &k, NULL, op);
		} else {
			if(!flipped) break;
			MDB_val level = { sizeof(i), &i };
			rc = mdb_cursor_seek(xc->cursor, &orig, NULL, dir);
		}
		if(MDB_SUCCESS == rc) xc->flags |= C_INITIALIZED;
		else xc->flags &= ~C_INITIALIZED;
		if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
	}

	lsmdb_cursor_sort(cursor, dir);
	rc = lsmdb_cursor_current(cursor, key, data);
	if(EINVAL == rc) return MDB_NOTFOUND;
	return rc;
}


int lsmdb_cursor_put(LSMDB_cursor *const cursor, MDB_val const *const key, MDB_val const *const data, unsigned const flags) {
	if(!cursor) return EINVAL;
	LSMDB_xcursor *const xc = cursor->cursors[0];
	if(!xc->cursor) return EINVAL; // Should never happen.
	if(MDB_NOOVERWRITE & flags) {
		int rc = lsmdb_cursor_seek(cursor, (MDB_val *)key, NULL, 0);
		if(MDB_SUCCESS == rc) return MDB_KEYEXIST;
		if(MDB_NOTFOUND != rc) return rc;
	}
	for(LSMDB_level i = 0; i < CURSOR_MAX; ++i) {
		cursor->cursors[i]->flags &= ~C_INITIALIZED;
	}
	return mdb_cursor_put(xc->cursor, (MDB_val *)key, (MDB_val *)data, flags);
}
int lsmdb_cursor_del(LSMDB_cursor *const cursor) {
	MDB_val key;
	int rc = lsmdb_cursor_current(cursor, &key, NULL);
	if(MDB_SUCCESS != rc) return rc;
	return lsmdb_cursor_del0(cursor, &key);
}





typedef struct {
	LSMDB_txn *txn;
	LSMDB_level level;
	size_t steps;
	MDB_cursor *a;
	MDB_cursor *b;
	MDB_cursor *c;
} LSMDB_compaction;


// TODO: Check errors properly
#define ok(x) ({ \
	int const __rc = (x); \
	if(MDB_SUCCESS != __rc && MDB_NOTFOUND != __rc) { \
		fprintf(stderr, "%s:%d %s: %s: %s\n", \
			__FILE__, __LINE__, __PRETTY_FUNCTION__, \
			#x, mdb_strerror(__rc)); \
		abort(); \
	} \
})

static int lsmdb_compact0(LSMDB_compaction *const c) {
	MDB_val key, ignore;
	int rc = mdb_cursor_get(c->c, &key, &ignore, MDB_LAST);

	MDB_val k1, k2, d1, d2;
	int rc1, rc2;
	if(MDB_SUCCESS == rc) {
		k1 = key;
		k2 = key;
		ok( rc1 = mdb_cursor_get(c->a, &k1, &d1, MDB_SET_RANGE) );
		ok( rc2 = mdb_cursor_get(c->b, &k2, &d2, MDB_SET_RANGE) );

		if(MDB_SUCCESS == rc1 && 0 == lsmdb_cmp(c->txn, &key, &k1)) {
			ok( rc1 = mdb_cursor_get(c->a, &k1, &d1, MDB_NEXT) );
		}
		if(MDB_SUCCESS == rc2 && 0 == lsmdb_cmp(c->txn, &key, &k2)) {
			ok( rc2 = mdb_cursor_get(c->b, &k2, &d2, MDB_NEXT) );
		}
	} else if(MDB_NOTFOUND == rc) {
		ok( rc1 = mdb_cursor_get(c->a, &k1, &d1, MDB_FIRST) );
		ok( rc2 = mdb_cursor_get(c->b, &k2, &d2, MDB_FIRST) );
	} else {
		return rc;
	}


	// TODO: Dynamic size...
	uint8_t b1[500];
	uint8_t b2[500];
	if(MDB_SUCCESS == rc1) {
		memcpy(b1, k1.mv_data, k1.mv_size);
		memcpy(b1+k1.mv_size, d1.mv_data, d1.mv_size);
		k1.mv_data = b1;
		d1.mv_data = b1+k1.mv_size;
	}
	if(MDB_SUCCESS == rc2) {
		memcpy(b2, k2.mv_data, k2.mv_size);
		memcpy(b2+k2.mv_size, d2.mv_data, d2.mv_size);
		k2.mv_data = b2;
		d2.mv_data = b2+k2.mv_size;
	}


	size_t i = 0, j = 0;
	while(i*CX_LEVEL_GROWTH+j < c->steps*CX_LEVEL_GROWTH*2) {
		if(MDB_NOTFOUND == rc1 && MDB_NOTFOUND == rc2) {
			LSMDB_txn *const txn = c->txn;
			LSMDB_level const level = c->level;

			MDB_dbi prev0, next0, pend0;
			MDB_dbi prev1, next1, pend1;
			rc = lsmdb_level_state(txn, level+0, &prev0, &next0, &pend0);
			rc = lsmdb_level_state(txn, level+1, &prev1, &next1, &pend1);

			rc = mdb_drop(txn->txn, prev0, 0);
			assert(MDB_SUCCESS == rc);
			rc = mdb_drop(txn->txn, pend0, 0);
			assert(MDB_SUCCESS == rc);
			rc = mdb_drop(txn->txn, next1, 0);
			assert(MDB_SUCCESS == rc);


			if(c->level > 0) {
				txn->state[c->level+0-1] = lsmdb_swap_12(txn->state[c->level+0-1]);
			}
			txn->state[c->level+1-1] = lsmdb_swap_23(txn->state[c->level+1-1]);

			rc = lsmdb_state_store(c->txn);
			assert(MDB_SUCCESS == rc);
			break;
		}

		int x = 0;
		if(MDB_NOTFOUND == rc1) x = +1;
		if(MDB_NOTFOUND == rc2) x = -1;
		if(0 == x) x = lsmdb_cmp(c->txn, &k1, &k2);

		if(x <= 0) {
//			fprintf(stderr, "put k1 %s\n", tohex(&k1));
			rc = mdb_cursor_put(c->c, &k1, &d1, MDB_APPEND);
			++i;
		} else {
//			fprintf(stderr, "put k2 %s\n", tohex(&k2));
			rc = mdb_cursor_put(c->c, &k2, &d2, MDB_APPEND);
			++j;
		}
		assert(MDB_KEYEXIST != rc);
		assert(MDB_SUCCESS == rc);

		if(x <= 0) {
			rc1 = mdb_cursor_get(c->a, &k1, &d1, MDB_NEXT);

			if(MDB_SUCCESS == rc1) {
				memcpy(b1, k1.mv_data, k1.mv_size);
				memcpy(b1+k1.mv_size, d1.mv_data, d1.mv_size);
				k1.mv_data = b1;
				d1.mv_data = b1+k1.mv_size;
			}
		}
		ok(rc1);

		if(x >= 0) {
			rc2 = mdb_cursor_get(c->b, &k2, &d2, MDB_NEXT);

			if(MDB_SUCCESS == rc2) {
				memcpy(b2, k2.mv_data, k2.mv_size);
				memcpy(b2+k2.mv_size, d2.mv_data, d2.mv_size);
				k2.mv_data = b2;
				d2.mv_data = b2+k2.mv_size;
			}
		}
		ok(rc2);

	}

//	fprintf(stderr, "Merged %d (%zu) with %d (%zu)\n", c->level+0, i, c->level+1, j);
	return MDB_SUCCESS;
}
int lsmdb_compact(LSMDB_txn *const txn, LSMDB_level const level, size_t const steps) {
	if(!txn) return EINVAL;
	if(txn->flags & MDB_RDONLY) return EACCES;
	if(level > LEVEL_MAX) return EINVAL;
	if(LEVEL_MAX == level) return MDB_SUCCESS;

//	fprintf(stderr, "Compacting %d\n", level);
	LSMDB_compaction compaction[1];
	compaction->txn = txn;
	compaction->level = level;
	compaction->steps = steps;
	compaction->a = NULL;
	compaction->b = NULL;
	compaction->c = NULL;

	int rc;
	MDB_dbi prev0, next0, pend0;
	MDB_dbi prev1, next1, pend1;
	rc = lsmdb_level_state(txn, level+0, &prev0, &next0, &pend0);
	assert(!rc);
	rc = lsmdb_level_state(txn, level+1, &prev1, &next1, &pend1);
	if(MDB_NOTFOUND == rc) {
		txn->state[level+1-1] = STATE_ABC;
		rc = lsmdb_state_store(compaction->txn);
		assert(MDB_SUCCESS == rc);
		rc = lsmdb_level_state(txn, level+1, &prev1, &next1, &pend1);
	}
	assert(!rc);

	rc = mdb_cursor_open(txn->txn, prev0, &compaction->a);
	assert(!rc);
	rc = mdb_cursor_open(txn->txn, next1, &compaction->b);
	assert(!rc);
	rc = mdb_cursor_open(txn->txn, pend1, &compaction->c);
	assert(!rc);

	rc = lsmdb_compact0(compaction);
	assert(!rc);

	mdb_cursor_close(compaction->a);
	mdb_cursor_close(compaction->b);
	mdb_cursor_close(compaction->c);

	return rc;
}
int lsmdb_autocompact(LSMDB_txn *const txn) {
	if(txn->flags & MDB_RDONLY) return EACCES;

	int rc;
	MDB_stat stats[1];
	rc = mdb_stat(txn->txn, LSMDB_WRITE_DBI, stats);
	assert(!rc);
	size_t const steps = stats->ms_entries+1; // Off by one?

	// TODO: Store this in the database
	static size_t _writes = 0;
	static size_t _old = 0;

	size_t const old = _old;
	size_t const cur = _writes+steps;
	_old = cur;


	if(steps >= CX_LEVEL_BASE) {
//		fprintf(stderr, "Level %d: %zu (%zu)\n", 0, steps, base);
		rc = lsmdb_compact(txn, 0, SIZE_MAX);
		assert(!rc);
		_writes += steps;
	}

	for(LSMDB_level i = 1; i < LEVEL_MAX; ++i) {
		size_t const off = CX_MERGE_BATCH * (LEVEL_MAX-i) / LEVEL_MAX;
		size_t const inc = (cur+off)/CX_MERGE_BATCH - (old+off)/CX_MERGE_BATCH;
		if(inc < 1) continue;

		MDB_dbi prev, next, pend;
		rc = lsmdb_level_state(txn, i, &prev, &next, &pend);
		if(MDB_NOTFOUND == rc) continue;
		assert(!rc);

		rc = mdb_stat(txn->txn, prev, stats);
		assert(!rc);
		size_t const s1 = stats->ms_entries;
		rc = mdb_stat(txn->txn, next, stats);
		assert(!rc);
		size_t const s2 = stats->ms_entries;

		size_t const target = CX_LEVEL_BASE * (size_t)pow(CX_LEVEL_GROWTH, i);
//		fprintf(stderr, "Level %d: %zu, %zu (%zu)\n", i, s1, s2, target);
		if(s1+s2 < target) continue;

		rc = lsmdb_compact(txn, i, CX_MERGE_BATCH * inc * CURSORS_PER_LEVEL);
		assert(!rc);
	}

	return MDB_SUCCESS;
}

