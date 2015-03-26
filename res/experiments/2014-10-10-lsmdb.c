
typedef struct LSMDB_env LSMDB_env;
typedef struct LSMDB_txn LSMDB_txn;
typedef struct LSMDB_cursor LSMDB_cursor;

int lsmdb_env_create(LSMDB_env **const out);
int lsmdb_env_set_mapsize(LSMDB_env *const env, size_t const size);
int lsmdb_env_open(LSMDB_env *const env, char const *const name, unsigned const flags, mdb_mode_t const mode);

int lsmdb_txn_begin(LSMDB_env *const env, LSMDB_txn *const parent, unsigned const flags, LSMDB_txn **const out);
int lsmdb_txn_commit(LSMDB_txn *const txn);
void lsmdb_txn_abort(LSMDB_txn *const txn);

int lsmdb_get(LSMDB_txn *const txn, MDB_val const *const key, MDB_val *const data);
int lsmdb_put(LSMDB_txn *const txn, MDB_val const *const key, MDB_val const *const data, unsigned const flags);

int lsmdb_cursor_open(LSMDB_txn *const txn, LSMDB_cursor **const out);
void lsmdb_cursor_close(LSMDB_cursor *const cursor);
int lsmdb_cursor_get(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, MDB_cursor_op const op);
int lsmdb_cursor_put(LSMDB_cursor *const cursor, MDB_val const *const key, MDB_val const *const data, unsigned const flags);

int lsmdb_compact(LSMDB_txn *const txn, LSMDB_level const level, size_t const steps);
int lsmdb_autocompact(LSMDB_txn *const txn);



#define MDB_RDWR 0

#define LEVEL_MAX 10

#define TABLE_MAX (LEVEL_MAX * 3)
#define TABLE_WRITER 0x00
#define TABLE_META 0x01
#define TABLE_UNUSED 0x02

#define META_PREV 0x00
#define META_NEXT 0x01
#define META_PEND 0x02

typedef uint8_t LSMDB_level;
typedef uint8_t LSMDB_table;
typedef uint8_t MDB_db8;

struct {
	MDB_env *env;
	MDB_db8 tables[TABLE_MAX];
} LSMDB_env;
struct {
	LSMDB_env *env;
	LSMDB_txn *parent;
	unsigned flags;
	MDB_txn *txn;

	LSMDB_cursor *cursor;
	MDB_db8 prev[LEVEL_MAX];
	MDB_db8 next[LEVEL_MAX];
	MDB_db8 pend[LEVEL_MAX];
	// TODO: We actually only need one byte per level
} LSMDB_txn;

struct {
	MDB_cursor *cursor;
	LSMDB_level level;
} LSMDB_xcursor;
struct {
	LSMDB_txn *txn;
	unsigned depth;
	LSMDB_xcursor cursors[LEVEL_MAX];
	LSMDB_xcursor *sorted[LEVEL_MAX];
	int dir;
} LSMDB_cursor;



int lsmdb_env_create(LSMDB_env **const out) {
	if(!env) return EINVAL;
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
		char name[3] = { map[i/16], map[i%16], '\0' };
		MDB_dbi dbi;
		rc = mdb_dbi_open(txn, name, MDB_CREATE, &dbi);
		assert(MDB_SUCCESS == rc);
		if(dbi > UINT8_MAX) {
			mdb_txn_abort(txn);
			return MDB_DBS_FULL;
		}
	}
	// TODO: Parse meta-data and make sure TABLES_MAX isn't too small

	rc = mdb_txn_commit(txn);
	assert(MDB_SUCCESS == rc);

	return MDB_SUCCESS;
}


static int lsmdb_txn_load(LSMDB_txn *const txn, uint8_t const type, MDB_db8 *const dbs) {
	MDB_val key = { sizeof(type), &type }, val;
	int rc = mdb_get(txn->txn, txn->env->tables[TABLE_META], &key, &val);
	if(MDB_NOTFOUND == rc) {
		rc = MDB_SUCCESS;
		val->mv_size = 0;
		val->mv_data = NULL;
	}
	if(MDB_SUCCESS != rc) return rc;
	if(val->mv_size+1 > LEVEL_MAX) return MDB_INCOMPATIBLE;
	uint8_t const *const names = val->mv_data;
	dbs[0] = 0;
	for(LSMDB_level i = 1; i < LEVEL_MAX; ++i) {
		LSMDB_table const table = i < val->mv_size ? names[i-1] : i*3+type;
		if(table >= TABLES_MAX) return MDB_INCOMPATIBLE;
		dbs[i] = table;
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
	rc = rc ? rc : lsmdb_txn_load(txn, META_PREV, txn->prev);
	rc = rc ? rc : lsmdb_txn_load(txn, META_NEXT, txn->next);
	rc = rc ? rc : lsmdb_txn_load(txn, META_PEND, txn->pend);
	if(MDB_SUCCESS != rc) {
		lsmdb_txn_abort(txn);
		return rc;
	}
	*out = txn;
	return MDB_SUCCESS;
}
int lsmdb_txn_commit(LSMDB_txn *const txn) {
	if(!txn) return EINVAL;
	int rc = mdb_txn_commit(txn->txn);
	lsmdb_cursor_close(txn->cursor);
	free(txn);
	return rc;
}
void lsmdb_txn_abort(LSMDB_txn *const txn) {
	if(!txn) return;
	mdb_txn_abort(txn->txn);
	lsmdb_cursor_close(txn->cursor);
	free(txn);
}

static int lsmdb_txn_cursor(LSMDB_txn *const txn) {
	if(!txn) return EINVAL;
	if(!txn->cursor) {
		int rc = lsmdb_cursor_open(txn, &txn->cursor);
		if(MDB_SUCCESS != rc) return rc;
	}
	return MDB_SUCCESS;
}
int lsmdb_get(LSMDB_txn *const txn, MDB_val const *const key, MDB_val *const data) {
	int rc = lsmdb_txn_cursor(txn);
	if(MDB_SUCCESS != rc) return rc;
	return lsmdb_cursor_get(txn->cursor, key, data, MDB_SET);
}
int lsmdb_put(LSMDB_txn *const txn, MDB_val const *const key, MDB_val const *const data, unsigned const flags) {
	int rc = lsmdb_txn_cursor(txn);
	if(MDB_SUCCESS != rc) return rc;
	return lsmdb_cursor_put(txn->cursor, key, data, flags);
}


static int lsmdb_cursor_load(LSMDB_cursor *const cursor) {
	if(!cursor) return EINVAL;
	for(LSMDB_level i = 0; i < LEVEL_MAX; ++i) {
		if(cursor->cursors[i].cursor) {
			mdb_cursor_close(cursor->cursors[i].cursor);
		}
		int rc = mdb_cursor_open(cursor->txn, cursor->active[i], &cursor->cursors[i].cursor);
		if(MDB_SUCCESS != rc) return rc;
		cursor->cursors[i].level = i;
		cursor->sorted[i] = &cursor->cursors[i];
	}
	cursor->dir = 0;
	return MDB_SUCCESS;
}
int lsmdb_cursor_open(LSMDB_txn *const txn, LSMDB_cursor **const out) {
	if(!txn) return EINVAL;
	LSMDB_cursor *cursor = calloc(1, sizeof(struct LSMDB_cursor));
	if(!cursor) return ENOMEM;
	cursor->txn = txn;
	int rc = lsmdb_cursor_load(cursor);
	if(MDB_SUCCESS != rc) {
		lsmdb_cursor_close(cursor);
		return rc;
	}
	*out = cursor;
	return MDB_SUCCESS;
}
void lsmdb_cursor_close(LSMDB_cursor *const cursor) {
	if(!cursor) return;
	for(LSMDB_level i = 0; i < LEVEL_MAX; ++i) {
		mdb_cursor_close(cursor->cursors[i].cursor);
	}
	free(cursor);
}
// TODO: Cursor renewal




int lsmdb_cursor_get(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, MDB_cursor_op const op) {
	return ENOTSUP;
}
int lsmdb_cursor_put(LSMDB_cursor *const cursor, MDB_val const *const key, MDB_val const *const data, unsigned const flags) {
	if(!cursor) return EINVAL;
	if(MDB_NOOVERWRITE & flags) {
		int rc = lsmdb_cursor_get(cursor, key, data, MDB_SET);
		if(MDB_SUCCESS == rc) return MDB_KEYEXIST;
		if(MDB_NOTFOUND != rc) return rc;
	}
	return mdb_cursor_put(cursor->cursors[0].cursor, key, data, 0);
}





typedef struct {
	LSMDB_txn *txn;
	LSMDB_level level;
	size_t steps;
	MDB_cursor *a;
	MDB_cursor *b;
	MDB_cursor *c;
} LSMDB_compaction;

static int mdb_cursor_cmp(MDB_cursor *const cursor, MDB_val const *const a, MDB_val const *const b) {
	return mdb_cmp(mdb_cursor_txn(cursor), mdb_cursor_dbi(cursor), a, b);
}

static int lsmdb_compact0(LSMDB_compaction *const c) {
	MDB_val key, ignore;
	int rc = mdb_cursor_get(c->c, &key, &ignore, MDB_LAST);

	MDB_val k1, k2, d1, d2;
	int rc1, rc2;
	if(MDB_SUCCESS == rc) {
		k1 = key;
		k2 = key;
		rc1 = mdb_cursor_get(c->a, &key, &d1, MDB_SET_RANGE);
		rc2 = mdb_cursor_get(c->b, &key, &d2, MDB_SET_RANGE);

		if(0 == mdb_cursor_cmp(c->a, &key, &k1)) {
			rc1 = mdb_cursor_get(c->a, &k1, &d1, MDB_NEXT);
		}
		if(0 == mdb_cursor_cmp(c->b, &key, &k2)) {
			rc2 = mdb_cursor_get(c->b, &k2, &d2, MDB_NEXT);
		}
	} else if(MDB_NOTFOUND == rc) {
		rc1 = mdb_cursor_get(c->a, &k1, &d1, MDB_FIRST);
		rc2 = mdb_cursor_get(c->b, &k2, &d2, MDB_FIRST);
	} else {
		return rc;
	}

	size_t i = 0;
	for(; i < c->steps; ++i) {
		if(MDB_NOTFOUND == rc1 && MDB_NOTFOUND == rc2) {
			LSMDB_txn *const txn = c->txn;
			rc = mdb_drop(txn->txn, txn->prev[c->level+0], 0);
			rc = mdb_drop(txn->txn, txn->pend[c->level+0], 0);
			rc = mdb_drop(txn->txn, txn->next[c->level+1], 0);
			swap(&txn->prev[c->level+0], &txn->next[c->level+0]);
			swap(&txn->next[c->level+1], &txn->pend[c->level+1]);
			// TODO: If txn->prev[c->level+1] is empty, swap it in right away?
			break;
		}

		int x = 0;
		if(MDB_NOTFOUND == rc1) x = +1;
		if(MDB_NOTFOUND == rc2) x = -1;
		if(0 == x) x = mdb_cursor_cmp(c->c, &k1, &k2);

		if(x <= 0) {
			rc = mdb_cursor_put(c->c, &k1, &d1, MDB_APPEND);
		} else {
			rc = mdb_cursor_put(c->c, &k2, &d2, MDB_APPEND);
		}

		rc1 = mdb_cursor_get(c->a, &k1, &d1, x <= 0 ? MDB_NEXT : MDB_GET_CURRENT);
		rc2 = mdb_cursor_get(c->b, &k2, &d2, x >= 0 ? MDB_NEXT : MDB_GET_CURRENT);
	}

	return MDB_SUCCESS;
}
int lsmdb_compact(LSMDB_txn *const txn, LSMDB_level const level, size_t const steps) {
	if(!txn) return EINVAL;
	if(level > LEVEL_MAX) return EINVAL;
	if(LEVEL_MAX == level) return MDB_SUCCESS;

	LSMDB_compaction compaction[1];
	compaction->txn = txn;
	compaction->level = level;
	compaction->steps = steps;

	int rc;
	rc = mdb_cursor_open(txn->txn, txn->prev[level+0], &compaction->a);
	rc = mdb_cursor_open(txn->txn, txn->next[level+1], &compaction->b);
	rc = mdb_cursor_open(txn->txn, txn->pend[level+1], &compaction->c);

	rc = lsmdb_compact0(compaction);

	mdb_cursor_close(compactio->a);
	mdb_cursor_close(compactio->b);
	mdb_cursor_close(compactio->c);

	return rc;
}
int lsmdb_autocompact(LSMDB_txn *const txn) {
	size_t const base = 1000;
	size_t const growth = 10;

	int rc;
	MDB_stat stats[1];
	rc = mdb_stat(txn->txn, txn->prev[0], stats);
	size_t const steps = stats->ms_entries;
	if(steps < min) return MDB_SUCCESS;

	for(LSMDB_level i = LEVEL_MAX-1; i--;) {
		if(i > 0) {
			size_t const target = base * (size_t)pow(growth, i);
			rc = mdb_stat(txn->txn, txn->prev[i], stats);
			if(stats->ms_entries < target) continue;
		}
		rc = lsmdb_compact(txn, i, steps);
	}

	return MDB_SUCCESS;
}









