/*
 * Copyright 2014 Ben Trask
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted only as authorized by the OpenLDAP
 * Public License.
 *
 * A copy of this license is available in the file LICENSE in the
 * top-level directory of the distribution or, alternatively, at
 * <http://www.OpenLDAP.org/license.html>.
 */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lsmdb.h"

typedef struct {
	MDB_cursor *cursor;
	LSMDB_level level;
} LSMDB_xcursor;
struct LSMDB_cursor {
	MDB_txn *txn;
	MDB_dbi dbi;
	LSMDB_level depth;
	LSMDB_level count;
	MDB_cursor **cursors;
	LSMDB_xcursor *sorted;
	int dir;
};

typedef struct {
	MDB_txn *txn;
	MDB_dbi dbi;
	char const *name;
	MDB_cursor *n0;
	MDB_cursor *n2;
	MDB_cursor *n3;
} LSMDB_compacter;


static char *debug_val(MDB_val const *const x) {
	char const map[] = "0123456789abcdef";
	char *const hex = calloc(x->mv_size*2+1, 1);
	for(size_t i = 0; i < x->mv_size; ++i) {
		hex[i*2+0] = map[0xf & (((char *)x->mv_data)[i] >> 4)];
		hex[i*2+1] = map[0xf & (((char *)x->mv_data)[i] >> 0)];
	}
	return hex;
}
static void debug_dups(MDB_cursor *const cursor, MDB_val const *const key) {
	
}



static int mdb_get_op(MDB_txn *const txn, MDB_dbi const dbi, MDB_val *const key, MDB_val *const data, MDB_cursor_op const op) {
	MDB_cursor *tmp = NULL;
	int rc = mdb_cursor_open(txn, dbi, &tmp);
	if(MDB_SUCCESS != rc) return rc;
	rc = mdb_cursor_get(tmp, key, data, op);
	mdb_cursor_close(tmp);
	return rc;
}
static int mdb_cursor_dcmp_pfx(MDB_cursor *const cursor, MDB_val const *const pfx, MDB_val const *const key) {
	MDB_txn *const txn = mdb_cursor_txn(cursor);
	MDB_dbi const dbi = mdb_cursor_dbi(cursor);
	if(key->mv_size < pfx->mv_size) {
		return mdb_dcmp(txn, dbi, pfx, key);
	} else {
		MDB_val xkey = { pfx->mv_size, key->mv_data };
		return mdb_dcmp(txn, dbi, pfx, &xkey);
	}
}
static int mdb_cursor_get_dup_pfx(MDB_cursor *const cursor, MDB_val *const key, MDB_val *const data) {
	MDB_val k = *key, d = *data;
	int rc = mdb_cursor_get(cursor, &k, &d, MDB_GET_BOTH_RANGE);
	if(MDB_SUCCESS != rc) return rc;
	int x = mdb_cursor_dcmp_pfx(cursor, data, &d);
	if(0 != x) return MDB_NOTFOUND;
	*key = k;
	*data = d;
	return MDB_SUCCESS;
}
static int mdb_cursor_seek_dup(MDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	if(0 == dir) return mdb_cursor_get_dup_pfx(cursor, key, data);
	MDB_val orig = *data;
	int rc = mdb_cursor_get(cursor, key, data, MDB_GET_BOTH_RANGE);
	if(dir >= 0) return rc;
	if(MDB_SUCCESS == rc) {
		if(0 == mdb_cursor_dcmp_pfx(cursor, &orig, data)) return MDB_SUCCESS;
		return mdb_cursor_get(cursor, key, data, MDB_PREV_DUP);
	} else if(MDB_NOTFOUND == rc) {
		return mdb_cursor_get(cursor, key, data, MDB_LAST_DUP);
	} else {
		return rc;
	}
}


static int lsmdb_level(MDB_val const *const level, LSMDB_level *const out) {
	if(!level) return EINVAL;
	if(1 != level->mv_size) return MDB_CORRUPTED;
	*out = ((uint8_t *)level->mv_data)[0];
	return MDB_SUCCESS;
}
static int lsmdb_depth(MDB_cursor *const cursor, LSMDB_level *const out) {
	MDB_val last;
	int rc = mdb_cursor_get(cursor, &last, NULL, MDB_LAST);
	if(MDB_NOTFOUND == rc) {
		*out = 0;
		return MDB_SUCCESS;
	}
	if(MDB_SUCCESS != rc) return rc;
	LSMDB_level x;
	rc = lsmdb_level(&last, &x);
	if(MDB_SUCCESS != rc) return rc;
	*out = x/2+2;
	return MDB_SUCCESS;
}

static int lsmdb_level_pair_gt(MDB_cursor **const a, MDB_cursor **const b, LSMDB_level *const c, LSMDB_level *const d) {
//	assert(0);
	assert(1 != *c);
	assert(0 != *d);
	return MDB_SUCCESS;
}
static int lsmdb_level_pair_eq(MDB_cursor **const a, MDB_cursor **const b, LSMDB_level *const c, LSMDB_level *const d) {
//	assert(0);
	assert(1 != *c);
	assert(0 != *d);
	return MDB_SUCCESS;
}
static int lsmdb_level_pair_lt(MDB_cursor **const a, MDB_cursor **const b, LSMDB_level *const c, LSMDB_level *const d) {
//	assert(0);
	MDB_cursor *swap1 = *a;
	*a = *b;
	*b = swap1;
	LSMDB_level swap2 = *c;
	*c = *d;
	*d = swap2;
	assert(1 != *c);
	assert(0 != *d);
	return MDB_SUCCESS;
}
static int lsmdb_level_pair(MDB_cursor **const a, MDB_cursor **const b, LSMDB_level *const c, LSMDB_level *const d) {
	assert(*c + 1 == *d || *d + 1 == *c);
	int rca, rcb;

	MDB_val c1 = { sizeof(*c), c };
	MDB_val d1 = { sizeof(*d), d };
	MDB_val ignored;
	rca = mdb_cursor_get(*a, &c1, &ignored, MDB_SET);
	rcb = mdb_cursor_get(*b, &d1, &ignored, MDB_SET);
	// If one tree doesn't exist, consider it the high tree.
	if(MDB_SUCCESS == rca && MDB_NOTFOUND == rcb) return lsmdb_level_pair_gt(a, b, c, d);
	if(MDB_NOTFOUND == rca && MDB_SUCCESS == rcb) return lsmdb_level_pair_lt(a, b, c, d);
	if(MDB_NOTFOUND == rca && MDB_NOTFOUND == rcb) return lsmdb_level_pair_eq(a, b, c, d);
	if(MDB_SUCCESS != rca) return rca;
	if(MDB_SUCCESS != rcb) return rcb;

	MDB_val lasta, lastb;
	rca = mdb_cursor_get(*a, NULL, &lasta, MDB_LAST_DUP);
	rcb = mdb_cursor_get(*b, NULL, &lastb, MDB_LAST_DUP);
	if(MDB_SUCCESS != rca) return rca;
	if(MDB_SUCCESS != rcb) return rcb;

	MDB_txn *const txn = mdb_cursor_txn(*a);
	MDB_dbi const dbi = mdb_cursor_dbi(*a);
	int x = mdb_dcmp(txn, dbi, &lasta, &lastb);
	// If one tree has an earlier last key, consider it the high tree.
	if(x > 0) return lsmdb_level_pair_gt(a, b, c, d);
	if(x < 0) return lsmdb_level_pair_lt(a, b, c, d);

	size_t sizea = 0, sizeb = 0;
	rca = mdb_cursor_count(*a, &sizea);
	rcb = mdb_cursor_count(*b, &sizeb);
	if(MDB_SUCCESS != rca) return rca;
	if(MDB_SUCCESS != rcb) return rcb;
	// If one tree is larger, consider it the high tree.
	if(sizea > sizeb) return lsmdb_level_pair_lt(a, b, c, d);
	if(sizea < sizeb) return lsmdb_level_pair_gt(a, b, c, d);
	return lsmdb_level_pair_eq(a, b, c, d);
}



int lsmdb_dbi_open(MDB_txn *const txn, char const *const name, unsigned const flags, LSMDB_dbi *const dbi) {
	unsigned x = MDB_DUPSORT;
	if(MDB_CREATE & flags) x |= MDB_CREATE;
	if(MDB_REVERSEKEY & flags) x |= MDB_REVERSEDUP;
	if(MDB_INTEGERKEY & flags) x |= MDB_INTEGERDUP;
	return mdb_dbi_open(txn, name, x, dbi);
}
int lsmdb_set_compare(MDB_txn *const txn, LSMDB_dbi const dbi, MDB_cmp_func *const cmp) {
	return mdb_set_dupsort(txn, dbi, cmp);
}




int lsmdb_get(MDB_txn *const txn, LSMDB_dbi const dbi, MDB_val *const key, MDB_val *const data) {
	MDB_cursor *tmp = NULL;
	int rc = mdb_cursor_open(txn, dbi, &tmp);
	if(MDB_SUCCESS != rc) return rc;
	LSMDB_level depth;
	rc = lsmdb_depth(tmp, &depth);
	if(MDB_SUCCESS != rc) depth = 0;
	for(LSMDB_level i = 0; i < depth*2; ++i) {
		MDB_val level = { sizeof(i), &i };
		rc = mdb_cursor_get_dup_pfx(tmp, &level, key);
		if(MDB_NOTFOUND == rc) continue;
		if(MDB_SUCCESS != rc) break;
		if(data) { data->mv_size = 0; data->mv_data = NULL; }
		mdb_cursor_close(tmp);
		return MDB_SUCCESS;
	}
	mdb_cursor_close(tmp);
	if(MDB_SUCCESS == rc) return MDB_NOTFOUND;
	return rc;
}
int lsmdb_put(MDB_txn *const txn, LSMDB_dbi const dbi, MDB_val *const key, MDB_val *const data, unsigned const flags) {
	assert((!data || 0 == data->mv_size) &&
		"LSMDB doesn't currently support separate "
		"payload, append to key instead");
	if(MDB_NOOVERWRITE & flags) {
		int rc = lsmdb_get(txn, dbi, key, data);
		if(MDB_SUCCESS == rc) return MDB_KEYEXIST;
		if(MDB_NOTFOUND != rc) return rc;
	}

	LSMDB_level i = 0;
	MDB_val level = { sizeof(i), &i };
	int rc = mdb_put(txn, dbi, &level, key, MDB_NODUPDATA);
	if(MDB_KEYEXIST == rc) return MDB_SUCCESS;
	return rc;
}
int lsmdb_del(MDB_txn *const txn, LSMDB_dbi const dbi, MDB_val *const key) {
	MDB_cursor *tmp = NULL;
	int rc = mdb_cursor_open(txn, dbi, &tmp);
	if(MDB_SUCCESS != rc) return rc;
	LSMDB_level depth;
	rc = lsmdb_depth(tmp, &depth);
	if(MDB_SUCCESS != rc) depth = 0;

	LSMDB_level i = depth*2;
	int found = 0;
	while(i--) {
		for(;;) {
			MDB_val level = { sizeof(i), &i };
			rc = mdb_cursor_get_dup_pfx(tmp, &level, key);
			if(MDB_NOTFOUND == rc) break;
			if(MDB_SUCCESS != rc) {
				mdb_cursor_close(tmp);
				return rc;
			}
			rc = mdb_cursor_del(tmp, 0);
			if(MDB_SUCCESS != rc) {
				mdb_cursor_close(tmp);
				return rc;
			}
			found = 1;
		}
	}
	mdb_cursor_close(tmp);
	return found ? MDB_SUCCESS : MDB_NOTFOUND;
}

static int lsmdb_cursor_cmp_fwd(LSMDB_xcursor const *const *const a, LSMDB_xcursor const *const *const b) {
	MDB_val ignored, k1, k2;
	int rc1 = mdb_cursor_get((*a)->cursor, &ignored, &k1, MDB_GET_CURRENT);
	int rc2 = mdb_cursor_get((*b)->cursor, &ignored, &k2, MDB_GET_CURRENT);
	if(MDB_SUCCESS == rc1 || MDB_SUCCESS == rc2) {
		if(MDB_SUCCESS != rc1) return +1;
		if(MDB_SUCCESS != rc2) return -1;
		MDB_txn *const txn = mdb_cursor_txn((*a)->cursor);
		MDB_dbi const dbi = mdb_cursor_dbi((*a)->cursor);
		int x = mdb_dcmp(txn, dbi, &k1, &k2);
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
	qsort(cursor->sorted, cursor->count, sizeof(LSMDB_xcursor), cmp);
	cursor->dir = dir;
}
static int lsmdb_cursor_grow(LSMDB_cursor *const cursor) {
	if(!cursor) return EINVAL;

	MDB_cursor *tmp = NULL;
	int rc = mdb_cursor_open(cursor->txn, cursor->dbi, &tmp);
	if(MDB_SUCCESS != rc) { mdb_cursor_close(tmp); return rc; }
	LSMDB_level depth;
	rc = lsmdb_depth(tmp, &depth);
	if(MDB_SUCCESS != rc) { mdb_cursor_close(tmp); return rc; }
	mdb_cursor_close(tmp);

	LSMDB_level const ocount = cursor->count;
	LSMDB_level const ncount = depth;
	if(ncount <= ocount) return MDB_SUCCESS;

	MDB_cursor **const a = realloc(cursor->cursors, sizeof(MDB_cursor *) * ncount);
	LSMDB_xcursor *const b = realloc(cursor->sorted, sizeof(LSMDB_xcursor) * ncount);
	if(!a || !b) {
		free(a);
		free(b);
		return ENOMEM;
	}
	cursor->cursors = a;
	cursor->sorted = b;

	MDB_val current;
	rc = lsmdb_cursor_get(cursor, &current, NULL);
	int const valid = MDB_SUCCESS == rc;
	int const olddir = cursor->dir;
	cursor->dir = 0;

	// TODO: Use lsmdb_level_pair to get the lower cursor for this level. We also have to store the index, so use LSMDB_xcursor for cursor->cursors?
	for(LSMDB_level i = ocount; i < ncount; ++i) {
		MDB_cursor *c = NULL;
		rc = mdb_cursor_open(cursor->txn, cursor->dbi, &c);
		if(MDB_SUCCESS != rc) return rc;
		cursor->cursors[i] = c;
		cursor->sorted[i].cursor = c;
		cursor->sorted[i].level = i;
		cursor->count++;
		if(valid) {
			MDB_val level = { sizeof(i), &i };
			rc = mdb_cursor_seek_dup(c, &level, &current, olddir);
			if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
		}
	}
	lsmdb_cursor_sort(cursor, olddir);
	return MDB_SUCCESS;
}

int lsmdb_cursor_open(MDB_txn *const txn, LSMDB_dbi const dbi, LSMDB_cursor **const out) {
	if(!out) return EINVAL;
	LSMDB_cursor *cursor = calloc(1, sizeof(struct LSMDB_cursor));
	if(!cursor) return ENOMEM;
	cursor->txn = txn;
	cursor->dbi = dbi;
	cursor->count = 0;
	cursor->cursors = NULL;
	cursor->sorted = NULL;
	cursor->dir = 0;
	int rc = lsmdb_cursor_grow(cursor);
	if(MDB_SUCCESS != rc) {
		lsmdb_cursor_close(cursor);
		return rc;
	}
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
int lsmdb_cursor_renew(MDB_txn *const txn, LSMDB_cursor *const cursor) {
	if(!cursor) return EINVAL;
	cursor->txn = txn;
	cursor->dir = 0;
	for(LSMDB_level i = 0; i < cursor->count; ++i) {
		int rc = mdb_cursor_renew(txn, cursor->cursors[i]);
		if(MDB_SUCCESS != rc) return rc;
	}
	return lsmdb_cursor_grow(cursor);
}

int lsmdb_cursor_get(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data) {
	if(!cursor) return EINVAL;
	if(0 == cursor->dir) return EINVAL;
	if(cursor->count < 1) return EINVAL;
	MDB_val level, k;
	MDB_cursor *const c = cursor->sorted[0].cursor;
	int rc = mdb_cursor_get(c, &level, &k, MDB_GET_CURRENT);
	if(MDB_SUCCESS != rc) return rc;
	if(key) *key = k;
	if(data) { data->mv_size = 0; data->mv_data = NULL; }
	return MDB_SUCCESS;
}
int lsmdb_cursor_seek(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	if(!cursor) return EINVAL;
	cursor->dir = 0;
	for(LSMDB_level i = 0; i < cursor->count; ++i) {
		MDB_cursor *const c = cursor->cursors[i];
		MDB_val level = { sizeof(i), &i };
		MDB_val k = *key;
		int rc = mdb_cursor_seek_dup(c, &level, &k, dir);
		if(MDB_SUCCESS != rc) mdb_cursor_renew(cursor->txn, c);
		if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
	}
	lsmdb_cursor_sort(cursor, dir);
	return lsmdb_cursor_get(cursor, key, data);
}
int lsmdb_cursor_step(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	if(!cursor) return EINVAL;
	if(0 == dir) return EINVAL;

	MDB_val orig;
	int rc = lsmdb_cursor_get(cursor, &orig, NULL);
	if(MDB_SUCCESS != rc) return rc;

	MDB_txn *const txn = cursor->txn;
	MDB_dbi const dbi = cursor->dbi;
	MDB_cursor_op const step = dir < 0 ? MDB_PREV_DUP : MDB_NEXT_DUP;
	int const flipped = (dir > 0) != (cursor->dir > 0);
	int const olddir = cursor->dir;
	cursor->dir = 0;

	for(LSMDB_level i = 0; i < cursor->count; ++i) {
		MDB_cursor *const c = cursor->sorted[i].cursor;
		MDB_val level, k;
		rc = mdb_cursor_get(c, &level, &k, MDB_GET_CURRENT);
		int const current =
			MDB_SUCCESS == rc &&
			0 == mdb_dcmp(txn, dbi, &orig, &k);
		if(current) {
			rc = mdb_cursor_get(c, &level, &k, step);
		} else {
			if(!flipped) break;
			MDB_val level = { sizeof(i), &i };
			rc = mdb_cursor_seek_dup(c, &level, &orig, dir);
		}
		if(MDB_SUCCESS != rc) mdb_cursor_renew(cursor->txn, c);
		if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
	}

	lsmdb_cursor_sort(cursor, dir);
	return lsmdb_cursor_get(cursor, key, data);
}
int lsmdb_cursor_start(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	if(!cursor) return EINVAL;
	if(0 == dir) return EINVAL;
	MDB_cursor_op const op = dir > 0 ? MDB_FIRST : MDB_LAST;
	cursor->dir = 0;
	for(LSMDB_level i = 0; i < cursor->count; ++i) {
		MDB_cursor *const c = cursor->cursors[i];
		MDB_val level = { sizeof(i), &i }, ignored;
		int rc = mdb_cursor_get(c, &level, &ignored, op);
		if(MDB_SUCCESS != rc) mdb_cursor_renew(cursor->txn, c);
		if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;
	}
	lsmdb_cursor_sort(cursor, dir);
	return lsmdb_cursor_get(cursor, key, data);
}





static void lsmdb_compacter_close(LSMDB_compacter *const c);
static int lsmdb_compacter_open(MDB_txn *const txn, LSMDB_dbi const dbi, char const *const name, LSMDB_compacter **const out) {
	LSMDB_compacter *const c = calloc(1, sizeof(LSMDB_compacter));
	if(!c) return ENOMEM;
	c->txn = txn;
	c->dbi = dbi;
	c->name = name;
	int rc = 0;
	rc = rc ? rc : mdb_cursor_open(txn, dbi, &c->n0);
	rc = rc ? rc : mdb_cursor_open(txn, dbi, &c->n2);
	rc = rc ? rc : mdb_cursor_open(txn, dbi, &c->n3);
	if(MDB_SUCCESS != rc) {
		lsmdb_compacter_close(c);
		return rc;
	}
	*out = c;
	return MDB_SUCCESS;
}
static void lsmdb_compacter_close(LSMDB_compacter *const c) {
	if(!c) return;
	mdb_cursor_close(c->n0);
	mdb_cursor_close(c->n2);
	mdb_cursor_close(c->n3);
	free(c);
}

static int lsmdb_compact_manual(LSMDB_compacter *const c, LSMDB_level const level, size_t const steps) {
	if(!c) return EINVAL;
	if(steps < 1) return EINVAL;

	int rc;
	LSMDB_level l0 = level*2+0;
	LSMDB_level l1 = level*2+1;
	LSMDB_level l2 = level*2+2;
	LSMDB_level l3 = level*2+3;
	rc = lsmdb_level_pair(&c->n0, &c->n2, &l0, &l1);
	assert(0 == rc);
	rc = lsmdb_level_pair(&c->n2, &c->n3, &l2, &l3);
	assert(0 == rc);

	MDB_val level0 = { sizeof(l0), &l0 };
	MDB_val level2 = { sizeof(l2), &l2 };
	MDB_val level3 = { sizeof(l3), &l3 };
	MDB_val k0, k2;
	int rc0, rc2;
	MDB_val last;


	rc = mdb_cursor_get(c->n3, &level3, &last, MDB_SET);
	if(MDB_SUCCESS == rc) {
		rc = mdb_cursor_get(c->n3, NULL, &last, MDB_LAST_DUP);
		assert(MDB_SUCCESS == rc);
	}
	if(MDB_SUCCESS == rc) {
		k0 = last;
		k2 = last;
		MDB_val tmp0 = level0, tmp2 = level2;
		rc0 = mdb_cursor_get(c->n0, &tmp0, &k0, MDB_GET_BOTH_RANGE);
		if(MDB_SUCCESS != rc0 && MDB_NOTFOUND != rc0) assert(0); //return rc;
		rc2 = mdb_cursor_get(c->n2, &tmp2, &k2, MDB_GET_BOTH_RANGE);
		if(MDB_SUCCESS != rc2 && MDB_NOTFOUND != rc2) assert(0); //return rc;

//		fprintf(stderr, "continuing\n");

		if(0 == mdb_dcmp(c->txn, c->dbi, &k0, &last)) {
			rc0 = mdb_cursor_get(c->n0, NULL, &k0, MDB_NEXT_DUP);
			if(MDB_SUCCESS != rc0 && MDB_NOTFOUND != rc0) assert(0);
		}
		if(0 == mdb_dcmp(c->txn, c->dbi, &k2, &last)) {
			rc2 = mdb_cursor_get(c->n2, NULL, &k2, MDB_NEXT_DUP);
			if(MDB_SUCCESS != rc2 && MDB_NOTFOUND != rc2) assert(0);
		}

	} else {
		rc0 = mdb_cursor_get(c->n0, &level0, &k0, MDB_SET);
		if(MDB_SUCCESS != rc0 && MDB_NOTFOUND != rc0) assert(0); //return rc;
		rc2 = mdb_cursor_get(c->n2, &level2, &k2, MDB_SET);
		if(MDB_SUCCESS != rc2 && MDB_NOTFOUND != rc2) assert(0); //return rc;
/*		size_t c1, c2, c3;
		rc = mdb_cursor_count(c->n0, &c1);
		if(rc) c1 = 0;
		rc = mdb_cursor_count(c->n2, &c2);
		if(rc) c2 = 0;
		rc = mdb_cursor_count(c->n3, &c3);
		if(rc) c3 = 0;
		fprintf(stderr, "merging %d (%zu) + %d (%zu) -> %d (%zu)\n", l0, c1, l2, c2, l3, c3);*/
	}

	uint8_t b0[500];
	uint8_t b2[500];

	size_t i = 0;
	for(; i < steps; ++i) {

		assert(&l0 == level0.mv_data);
		assert(&l2 == level2.mv_data);
		assert(&l3 == level3.mv_data);

		if(MDB_NOTFOUND == rc0) {
			rc = mdb_del(c->txn, c->dbi, &level0, NULL);
			if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) assert(0);
		}
		if(MDB_NOTFOUND == rc2) {
			rc = mdb_del(c->txn, c->dbi, &level2, NULL);
			if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) assert(0);
		}
		if(MDB_NOTFOUND == rc0 && MDB_NOTFOUND == rc2) {
/*			size_t c1, c2, c3;
			rc = mdb_cursor_count(c->n0, &c1);
			if(rc) c1 = 0;
			rc = mdb_cursor_count(c->n2, &c2);
			if(rc) c2 = 0;
			rc = mdb_cursor_count(c->n3, &c3);
			if(rc) c3 = 0;
			fprintf(stderr, "merged %d (%zu) + %d (%zu) -> %d (%zu) : %zu dups\n", l0, c1, l2, c2, l3, c3, (c1+c2)-c3);*/
			break;
		}

		// WORKAROUND: We wouldn't need this if our cursors
		// didn't break after putting to a different tree.
		// deps/liblmdb/mdb.c:5306: Assertion 'IS_BRANCH(mc->mc_pg[mc->mc_top])'
		// failed in mdb_cursor_sibling()
		if(MDB_NOTFOUND != rc0 && b0 != k0.mv_data) {
			memcpy(b0, k0.mv_data, k0.mv_size);
			k0.mv_data = b0;
		}
		if(MDB_NOTFOUND != rc2 && b2 != k2.mv_data) {
			memcpy(b2, k2.mv_data, k2.mv_size);
			k2.mv_data = b2;
		}

		int x = 0;
		if(MDB_NOTFOUND == rc0) x = +1;
		if(MDB_NOTFOUND == rc2) x = -1;
		if(0 == x) x = mdb_dcmp(c->txn, c->dbi, &k0, &k2);
		if(x <= 0) {
			rc = mdb_cursor_put(c->n3, &level3, &k0, MDB_APPENDDUP);
			assert(MDB_KEYEXIST != rc);
			if(MDB_SUCCESS != rc) assert(0);

			// WORKAROUND
			MDB_val tmp = level0;
			rc0 = mdb_cursor_get(c->n0, &tmp, &k0, MDB_GET_BOTH);
			if(MDB_SUCCESS != rc0) assert(0); //return rc;

			rc0 = mdb_cursor_get(c->n0, NULL, &k0, MDB_NEXT_DUP);
			if(MDB_SUCCESS != rc0 && MDB_NOTFOUND != rc0) assert(0);
		}
		if(x > 0) {
			rc = mdb_cursor_put(c->n3, &level3, &k2, MDB_APPENDDUP);
			assert(MDB_KEYEXIST != rc);
			if(MDB_SUCCESS != rc) assert(0);
		}
		if(x >= 0) {

			// WORKAROUND
			MDB_val tmp = level2;
			rc2 = mdb_cursor_get(c->n2, &tmp, &k2, MDB_GET_BOTH);
			if(MDB_SUCCESS != rc2) assert(0); //return rc;

			rc2 = mdb_cursor_get(c->n2, NULL, &k2, MDB_NEXT_DUP);
			if(MDB_SUCCESS != rc2 && MDB_NOTFOUND != rc2) assert(0);
		}
	}

//	fprintf(stderr, "%s: compacted level %u by %u items\n", c->name, level, i);
	return MDB_SUCCESS;
}
static int lsmdb_compact_auto(LSMDB_compacter *const c) {
	if(!c) return EINVAL;

	size_t const base = 1000;
	size_t const growth = 10;
	size_t const min = 1000;

	LSMDB_level depth;
	int rc = lsmdb_depth(c->n0, &depth);
	if(MDB_SUCCESS != rc) assert(0);

	LSMDB_level worst_level = 0;
	double worst_bloat = 0;
	double total_bloat = 0;

	for(LSMDB_level i = 0; i < depth; ++i) {
		LSMDB_level l2 = i*2+0;
		LSMDB_level l3 = i*2+1;
		rc = lsmdb_level_pair(&c->n2, &c->n3, &l2, &l3);
		if(MDB_SUCCESS != rc) assert(0);

		size_t c2, c3;
		rc = mdb_cursor_count(c->n2, &c2);
		if(MDB_SUCCESS != rc) c2 = 0;
		rc = mdb_cursor_count(c->n3, &c3);
		if(MDB_SUCCESS != rc) c3 = 0;
		size_t const count = c2;
		size_t const target = base * (size_t)pow(growth, i);
		double const bloat = (double)count / target;

		fprintf(stderr, "%s: autocompact level %d: %zu + %zu / %zu (%f)\n", c->name, i, c2, c3, target+min, bloat);

		if(bloat > worst_bloat) {
			worst_bloat = bloat;
			worst_level = i;
		}
		total_bloat += bloat;
	}

	if(worst_bloat < 0.5) return MDB_SUCCESS;
	return lsmdb_compact_manual(c, worst_level, 0 == worst_level ? SIZE_MAX : 5000);
}
int lsmdb_autocompact(MDB_txn *const txn, LSMDB_dbi const dbi, char const *const name) {
	LSMDB_compacter *c = NULL;
	int rc = lsmdb_compacter_open(txn, dbi, name, &c); // TODO: Eliminate allocation
	if(MDB_SUCCESS != rc) return rc;
	rc = lsmdb_compact_auto(c);
	lsmdb_compacter_close(c);
	return rc;
}

