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
	MDB_cursor *src;
	MDB_cursor *dst;
} LSMDB_compacter;


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
	if(MDB_SUCCESS != rc) return rc;
	LSMDB_level x;
	rc = lsmdb_level(&last, &x);
	if(MDB_SUCCESS != rc) return rc;
	*out = x+1;
	return MDB_SUCCESS;
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
	for(LSMDB_level i = 0; i < depth; ++i) {
		MDB_val level = { sizeof(i), &i };
		rc = mdb_cursor_get_dup_pfx(tmp, &level, key);
		if(MDB_NOTFOUND == rc) continue;
		if(MDB_SUCCESS != rc) break;
		if(data) { data->mv_size = 0; data->mv_data = NULL; }
		mdb_cursor_close(tmp);
		return MDB_SUCCESS;
	}
	mdb_cursor_close(tmp);
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

	LSMDB_level i = depth;
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
	rc = rc ? rc : mdb_cursor_open(txn, dbi, &c->src);
	rc = rc ? rc : mdb_cursor_open(txn, dbi, &c->dst);
	if(MDB_SUCCESS != rc) {
		lsmdb_compacter_close(c);
		return rc;
	}
	*out = c;
	return MDB_SUCCESS;
}
static void lsmdb_compacter_close(LSMDB_compacter *const c) {
	if(!c) return;
	mdb_cursor_close(c->src);
	mdb_cursor_close(c->dst);
	free(c);
}
static int lsmdb_compact_step(LSMDB_compacter *const c, LSMDB_level const level) {
	int rc;
	MDB_val slevel, skey;
	rc = mdb_cursor_get(c->src, &slevel, &skey, MDB_GET_CURRENT);
	if(EINVAL == rc) return MDB_NOTFOUND; // We deleted the last item from this level
	if(MDB_SUCCESS != rc) { fprintf(stderr, "%d\n", rc) ; assert(0); } //return rc;

	MDB_val dlevel, dkey;
	rc = mdb_cursor_get(c->dst, &dlevel, &dkey, MDB_GET_CURRENT);
	for(;;) {
		if(MDB_NOTFOUND == rc || EINVAL == rc) break; // TODO: Use MDB_APPEND from here on
		if(MDB_SUCCESS != rc) assert(0); //return rc;
		if(mdb_dcmp(c->txn, c->dbi, &skey, &dkey) >= 0) break;
		rc = mdb_cursor_get(c->dst, &dlevel, &dkey, MDB_NEXT_DUP);
	}

	LSMDB_level x = level+1;
	MDB_val x2 = { sizeof(x), &x };
	rc = mdb_cursor_put(c->dst, &x2, &skey, 0);
	if(MDB_SUCCESS != rc) assert(0); //return rc;

	rc = mdb_del(c->txn, c->dbi, &slevel, &skey);
	if(MDB_SUCCESS != rc) { fprintf(stderr, "rc=%d\n", rc); assert(0); } //return rc;

	return MDB_SUCCESS;
}
static int lsmdb_compact_manual(LSMDB_compacter *const c, LSMDB_level const level, size_t const steps) {
	if(!c) return EINVAL;
	if(steps < 1) return EINVAL;

	// TODO: Rolling compactions using level pairs and
	// MDB_APPEND. Would require twice as many levels,
	// but each level would be half as full, so it'd be
	// an even trade. Once we finish merging x and y
	// into z, swap y and z and start over.
	// 
	// But I suspect MDB_APPEND would actually be less
	// of a win than one might think.
	int rc;
	LSMDB_level l1 = level+0;
	LSMDB_level l2 = level+1;
	MDB_val level1 = { sizeof(l1), &l1 };
	MDB_val level2 = { sizeof(l2), &l2 };
	MDB_val first = {};

	rc = mdb_cursor_get(c->src, &level1, &first, MDB_SET);
	if(MDB_NOTFOUND == rc) return MDB_SUCCESS; // Nothing to merge
	if(MDB_SUCCESS != rc) assert(0); //return rc;

	size_t max;
	rc = mdb_cursor_count(c->src, &max);
	if(MDB_SUCCESS != rc) return rc;
	if(max <= 2) return MDB_SUCCESS;
	max = steps > max-2 ? max-2 : steps;

	rc = mdb_cursor_get(c->dst, &level2, &first, MDB_GET_BOTH_RANGE);
	if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) assert(0); //return rc;

	size_t i = 0;
	for(; i < max; ++i) {
		rc = lsmdb_compact_step(c, level);
		if(MDB_NOTFOUND == rc) break;
		if(MDB_SUCCESS != rc) assert(0); //return rc;
	}

	fprintf(stderr, "%s: compacted level %u by %u items\n", c->name, level, i);
	return MDB_SUCCESS;
}
static int lsmdb_compact_auto(LSMDB_compacter *const c) {
	if(!c) return EINVAL;

	size_t const base = 1000;
	size_t const growth = 10;
	size_t const min = 1000;

	LSMDB_level depth = 0;
	LSMDB_level worst_level = 0;
	size_t worst_bloat = 0;
	size_t total_bloat = 0;
	size_t incoming = 0;
	int rc;
	MDB_val level_val, ignored;
	rc = mdb_cursor_get(c->src, &level_val, &ignored, MDB_FIRST);
	for(;;) {
		if(MDB_NOTFOUND == rc) break;
		if(MDB_SUCCESS != rc) return rc;

		LSMDB_level level;
		rc = lsmdb_level(&level_val, &level);
		if(MDB_SUCCESS != rc) return MDB_CORRUPTED;
		if(level > depth) depth = level;

		size_t count;
		rc = mdb_cursor_count(c->src, &count);
		assert(0 == rc);
		size_t const target = base * (size_t)pow(growth, level);
		fprintf(stderr, "%s: autocompact level %d: %zu / %zu\n", c->name, level, count, level ? target+min : 2);

		size_t const bloat = count > target ? count - target : 0;
		if(bloat > worst_bloat) {
			worst_bloat = overflow;
			worst_level = level;
		}
		total_bloat += bloat;
		if(0 == level) incoming = count;

		rc = mdb_cursor_get(c->src, &level_val, &ignored, MDB_NEXT_NODUP);
	}

	(void)incoming;
	if(worst_size < min) return MDB_SUCCESS;
	return lsmdb_compact_manual(c, worst_level, worst_bloat*2);
}
int lsmdb_autocompact(MDB_txn *const txn, LSMDB_dbi const dbi, char const *const name) {
	LSMDB_compacter *c = NULL;
	int rc = lsmdb_compacter_open(txn, dbi, name, &c); // TODO: Eliminate allocation
	if(MDB_SUCCESS != rc) return rc;
	rc = lsmdb_compact_auto(c);
	lsmdb_compacter_close(c);
	return rc;
}

