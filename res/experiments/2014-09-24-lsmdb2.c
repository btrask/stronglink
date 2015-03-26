/* LSMDB - LSM-tree wrapper for MDB
 * 
 * Things generally work like they do in MDB. Differences and
 * limitations:
 * 
 * - LSMDB only stores keys without separate payloads. The reason is
 *   that under the hood, LSMDB uses MDB's dupsort to implement
 *   nested b-trees. `Data` parameters are left in the API for future
 *   compatibility. Returned data is always zero length.
 * - For the above reason, lsmdb_get() and the like match the first
 *   key with the given prefix, not necessarily the whole key.
 * - Instead of MDB_cursor_ops, smaller functions are used. Most of
 *   them take a `dir` parameter, where >=1 indicates forward and
 *   <=1 indicates reverse. Some of them also accept 0, meaning exact
 *   match.
 * - As in every write-optimized data structure, lsmdb_put() with
 *   MDB_NOOVERWRITE involves an extra get(). Faster to gracefully
 *   handle replacements/duplicates whenever possible.
 * - You should generally call lsmdb_autocompact() after performing
 *   a batch of inserts (e.g. right before commit). Currently you
 *   have to track which DBIs you touched and explicitly compact all
 *   of them whenever appropriate.
 * - When you do a compaction, you have to renew() any open cursors
 *   in the same transaction, if you intend to keep using them.
 * - Doing large compactions in child transactions might result in
 *   MDB_TXN_FULL.
 * - lsmdb_del() does a full delete, removing every key with the
 *   specified prefix from every level of the LSM-tree. For faster
 *   deletions, put() a sentinel value instead.
 * - Dupsort is used internally, so it isn't available for clients.
 * - Compactions don't currently use MDB_APPEND. It's worth trying
 *   but I'm not sure it would be a big improvement.
 * 
 * 
 * Copyright 2014 Ben Trask
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted only as authorized by the OpenLDAP
 * Public License.
 *
 * A copy of this license is available in the file LICENSE in the
 * top-level directory of the distribution or, alternatively, at
 * <http://www.OpenLDAP.org/license.html>.
 */

#include <assert.h>
#include "../deps/liblmdb/lmdb.h"

typedef uint8_t LSMDB_level;
typedef struct LSMDB_cursor LSMDB_cursor;
typedef struct LSMDB_compacter LSMDB_compacter;

int lsmdb_dbi_open(MDB_txn *const txn, char const *const name, unsigned const flags, MDB_dbi *const dbi);
int lsmdb_set_compare(MDB_txn *const txn, MDB_dbi const dbi, MDB_cmp_func *const cmp);

int lsmdb_get(MDB_txn *const txn, MDB_dbi const dbi, MDB_val *const key, MDB_val *const data);
int lsmdb_put(MDB_txn *const txn, MDB_dbi const dbi, MDB_val *const key, MDB_val *const data, unsigned const flags); // MDB_NOOVERWRITE
int lsmdb_del(MDB_txn *const txn, MDB_dbi const dbi, MDB_val *const key);

int lsmdb_cursor_open(MDB_txn *const txn, MDB_dbi const dbi, LSMDB_cursor **const cursor);
void lsmdb_cursor_close(LSMDB_cursor *const cursor);
int lsmdb_cursor_renew(MDB_txn *const txn, LSMDB_cursor *const cursor);

int lsmdb_cursor_get(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data); // Equivalent to MDB_GET_CURRENT
int lsmdb_cursor_seek(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir); // Equivalent to MDB_SET or MDB_SET_RANGE
int lsmdb_cursor_step(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir); // Equivalent to MDB_NEXT or MDB_PREV
int lsmdb_cursor_start(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir); // Equivalent to MDB_FIRST or MDB_LAST

// TODO
//int lsmdb_cursor_put(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, unsigned const flags);
//int lsmdb_cursor_del(LSMDB_cursor *const cursor, MDB_val *const key);

int lsmdb_autocompact(MDB_txn *const txn, MDB_dbi const dbi);


typedef struct {
	MDB_cursor *cursor;
	LSMDB_level level;
} LSMDB_xcursor;
struct LSMDB_cursor {
	MDB_txn *txn;
	MDB_dbi dbi;
	LSMDB_level count;
	MDB_cursor **cursors;
	LSMDB_xcursor *sorted;
	int dir;
};

struct LSMDB_compacter {
	MDB_txn *txn;
	MDB_dbi dbi;
	MDB_cursor *src;
	MDB_cursor *dst;
};


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
	int rc = mdb_cursor_get(tmp, &k, &d, MDB_GET_BOTH_RANGE);
	if(MDB_SUCCESS != rc) return rc;
	if(0 != mdb_cursor_dcmp_pfx(cursor, key, &k)) return MDB_NOTFOUND;
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


static int lsmdb_depth(MDB_txn *const txn, MDB_dbi const dbi, LSMDB_level *const out) {
	MDB_val last;
	int rc = mdb_get_op(txn, dbi, &last, NULL, MDB_LAST);
	if(MDB_SUCCESS != rc) return rc;
	if(1 != last->mv_size) return MDB_CORRUPTED;
	*out = ((uint8_t *)last->mv_data)[0] + 1;
	return MDB_SUCCESS;
}



int lsmdb_dbi_open(MDB_txn *const txn, char const *const name, unsigned const flags, MDB_dbi *const dbi) {
	unsigned x = MDB_DUPSORT;
	if(MDB_CREATE & flags) x |= MDB_CREATE;
	if(MDB_REVERSEKEY & flags) x |= MDB_REVERSEDUP;
	if(MDB_INTEGERKEY & flags) x |= MDB_INTEGERDUP;
	return mdb_dbi_open(txn, name, x, dbi);
}
int lsmdb_set_compare(MDB_txn *const txn, MDB_dbi const dbi, MDB_cmp_func *const cmp) {
	return mdb_set_dupsort(txn, dbi, cmp);
}




int lsmdb_get(MDB_txn *const txn, MDB_dbi const dbi, MDB_val *const key, MDB_val *const data) {
	MDB_cursor *tmp = NULL;
	int rc = mdb_cursor_open(txn, dbi, &tmp);
	if(MDB_SUCCESS != rc) return rc;
	for(LSMDB_level i = 0; i < txn->depth; ++i) {
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
int lsmdb_put(MDB_txn *const txn, MDB_dbi const dbi, MDB_val *const key, MDB_val *const data, unsigned const flags) {
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
	return mdb_put(txn, dbi, &level, key, 0);
}
int lsmdb_del(MDB_txn *const txn, MDB_dbi const dbi, MDB_val *const key) {
	LSMDB_level i;
	int rc = lsmdb_depth(txn, dbi, &i);
	if(MDB_SUCCESS != rc) return rc;

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
	int rc2 = mdb_cursor_get((*b)->cursor, &ignored, k2, MDB_GET_CURRENT);
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

	int rc = lsmdb_depth(cursor->txn, cursor->dbi, &cursor->depth);
	if(MDB_SUCCESS != rc) return rc;
	LSMDB_level const ocount = cursor->count;
	LSMDB_level const ncount = cursor->depth;
	if(ncount <= ocount) return MDB_SUCCESS;

	MDB_cursor **const a = realloc(cursor->cursors, sizeof(MDB_cursor *) * ncount);
	MDB_cursor **const b = realloc(cursor->sorted, sizeof(LSMDB_xcursor) * ncount);
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

int lsmdb_cursor_open(MDB_txn *const txn, MDB_dbi const dbi, LSMDB_cursor **const out) {
	if(!out) return EINVAL;
	LSMDB_cursor *cursor = calloc(1, sizeof(struct LSMDB_cursor));
	if(!cursor) return ENOMEM;
	cursor->txn = txn;
	cursor->dbi = dbi;
	cursor->depth = 0;
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
	rc = mdb_cursor_get(c, &level, &k, MDB_GET_CURRENT);
	if(MDB_SUCCESS != rc) return rc;
	if(key) *key = k;
	if(data) { data->mv_size = 0; data->mv_data = NULL; }
	return MDB_SUCCESS;
}
int lsmdb_cursor_seek(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir) {
	if(!cursor) return EINVAL;
	cursor->dir = 0;
	for(LSMDB_level i = 0; i < cursor->count; ++i) {
		MDB_cursor *const c = cursor->cursors[i].cursor;
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
	rc = lsmdb_cursor_get(cursor, &orig, NULL);
	if(MDB_SUCCESS != rc) return rc;

	MDB_txn *const txn = cursor->txn;
	MDB_dbi const dbi = cursor->dbi;
	MDB_cursor_op const step = dir < 0 ? MDB_PREV_DUP : MDB_NEXT_DUP;
	int const flipped = (dir > 0) != (cursor->dir > 0);
	int const olddir = cursor->dir;
	cursor->dir = 0;

	for(LSMDB_level i = 0; i < cursor->count; ++i) {
		MDB_cursor *const c = cursors->sorted[i].cursor;
		MDB_val level, k;
		rc = mdb_cursor_get(c, &level, &k, MDB_GET_CURRENT);
		int const current =
			MDB_SUCESS == rc &&
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
	MDB_cursor_op const op = dir > 0 ? MDB_FIRST_DUP : MDB_LAST_DUP;
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


static int lsmdb_compacter_open(MDB_txn *const txn, MDB_dbi const dbi, LSMDB_compacter *const out) {
	LSMDB_compacter *const c = calloc(1, sizeof(struct LSMDB_compactor));
	if(!c) return ENOMEM;
	c->txn = txn;
	c->dbi = dbi;
	int rc = 0;
	rc = rc ? rc : mdb_cursor_open(txn, dbi, &c->src);
	rc = rc ? rc : mdb_cursor_open(txn, dbi, &c->dst);
	if(MDB_SUCCESS != rc) {
		lsmdb_compacter_close(c);
		return NULL;
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
	if(MDB_SUCCESS != rc) return rc;

	MDB_val dlevel, dkey;
	rc = mbd_cursor_get(c->dst, &dlevel, &dkey, MDB_GET_CURRENT);
	for(;;) {
		if(MDB_NOTFOUND == rc) break; // TODO: Use MDB_APPEND from here on
		if(MDB_SUCCESS != rc) return rc;
		if(mdb_dcmp(c->txn, c->dbi, &skey, &dkey) >= 0) break;
		rc = mdb_cursor_get(c->dst, &dlevel, &dkey, MDB_NEXT_DUP);
	}

	LSMDB_level x = level+1;
	MDB_val x2 = { sizeof(x), &x };
	rc = mdb_cursor_put(c->dst, &x2, &skey, 0);
	if(MDB_SUCCESS != rc) return rc;

	rc = mdb_cursor_del(c->src, 0);
	if(MDB_SUCCESS != rc) return rc;

	return MDB_SUCCESS;
}
static int lsmdb_compact_manual(LSMDB_compacter *const c, LSMDB_level const level, size_t const steps) {
	if(!c) return EINVAL;
	if(steps < 1) return EINVAL;

	// TODO: Rolling compactions using level pairs and
	// MDB_APPEND. Would require twice as many levels,
	//  but each level would be half as full, so it'd be
	// an even trade. Once we finish merging x and y
	// into z, swap y and z and start over.
	// 
	// But I suspect MDB_APPEND would actually be less
	// of a win than one might think.
	int rc;
	LSMDB_level a1 = level+0;
	LSMDB_level b1 = level+1;
	MDB_val a2 = { sizeof(a1), &a1 };
	MDB_val b2 = { sizeof(b1), &b1 };
	MDB_val first;
	rc = mdb_cursor_get(c->src, &a2, &first, MDB_FIRST_DUP);
	if(MDB_NOTFOUND == rc) return MDB_SUCCESS; // Nothing to merge
	if(MDB_SUCCESS != rc) return rc;
	rc = mdb_cursor_get(c->dst, &b2, &first, MDB_GET_BOTH_RANGE);
	if(MDB_SUCCESS != rc && MDB_NOTFOUND != rc) return rc;

	for(size_t i = 0; i < steps; ++i) {
		rc = lsmdb_compact_step(c, level);
		if(MDB_NOTFOUND == rc) return MDB_SUCCESS;
		if(MDB_SUCCESS != rc) return rc;
	}

	return MDB_SUCCESS;
}
static int lsmdb_compact_auto(LSMDB_compacter *const c) {
	if(!c) return EINVAL;

	double const base = 1000.0
	double const growth = 10.0;
	size_t const steps = 50; // TODO: Adjust based on severity

	LSMDB_level worstl = 0;
	double worstf = 0.0;
	double target = base;
	int rc;
	MDB_val level;
	rc = mdb_cursor_get(c->src, &level, NULL, MDB_FIRST);
	for(;;) {
		if(MDB_NOTFOUND == rc) break;
		if(MDB_SUCCESS != rc) return rc;

		size_t count;
		rc = mdb_cursor_count(c->src, &count);
		double const fullness = count / target;
		if(fullness > worstf) {
			worstf = fullness;
			worstl = ((uint8_t *)level->mv_data)[0];
		}

		rc = mdb_cursor_get(c->src, &level, NULL, MDB_NEXT_NODUP);
		expected *= growth;
	}

	if(worstf <= 1.0) return MDB_SUCCESS;
	return lsmdb_compact(c, worstl, steps);
}
int lsmdb_autocompact(MDB_txn *const txn, MDB_dbi const dbi) {
	LSMDB_compacter *c = NULL;
	int rc = lsmdb_compacter_open(txn, dbi, &c); // TODO: Eliminate allocation
	if(MDB_SUCCESS != rc) return rc;
	rc = rc ? rc : lsmdb_compact_manual(c, 0, SIZE_MAX);
	rc = rc ? rc : lsmdb_compact_auto(c);
	lsmdb_compacter_close(c);
	return rc;
}


