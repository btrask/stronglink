

typedef struct {
	MDB_cursor *cursor;
	LSMDB_level level;
} LSMDB_xcursor;

typedef struct {
	MDB_txn *txn;
	MDB_dbi dbi;
	MDB_cursor *in1;
	MDB_cursor *in2;
	MDB_cursor *out;
} LSMDB_compaction;



static int lsmdb_compacter_open(MDB_txn *const txn, MDB_dbi const dbi, uint8_t const level, LSMDB_xcursor *const xcursor) {
	xcursor->level = level;
	MDB_cursor *a, *b;

	uint8_t buf1 = level << 1 | 0;
	MV_val k1 = { sizeof(buf1), &buf1 };
	MV_val d1;
	int rc1 = mdb_cursor_get(&a, &k1, &d1, MDB_SET);
	if(MDB_SUCCESS != rc1 && MDB_NOTFOUND != rc1) return rc1;

	uint8_t buf2 = level << 1 | 1;
	MV_val k2 = { sizeof(buf2), &buf2 };
	MV_val d2;
	int rc2 = mdb_cursor_get(&b, &k2, &d2, MDB_SET);
	if(MDB_SUCCESS != rc2 && MDB_NOTFOUND != rc2) return rc2;




}


static int lsmdb_compact_step(LSMDB_compaction const *const ctx) {
	MDB_val skey;
	MDB_val sdata;
	int rc = mdb_cursor_get(ctx->lo, &skey, &sdata, MDB_GET_CURRENT);
	if(MDB_NOTFOUND == rc) return MDB_SUCCESS;
	if(MDB_SUCCESS != rc) return rc;

	for(;;) {
		MDB_val dkey;
		MDB_val ddata;
		rc = mbd_cursor_get(ctx->hi, &dkey, &ddata, MDB_GET_CURRENT);
		if(MDB_NOTFOUND == rc) break;
		if(MDB_SUCCESS != rc) return rc;

		if(mdb_dcmp(ctx->txn, ctx->dbi, &skey, &dkey) > 0) break;

		rc = mdb_cursor_put(ctx->nu, &dkey, &ddata, MDB_APPENDDUP);
		if(MDB_SUCCESS != rc) return rc;

		rc = mdb_cursor_del(ctx->hi, 0);
		if(MDB_SUCCESS != rc) return rc;
		// TODO: We assume this increments the cursor...
	}

	rc = mdb_cursor_put(ctx->nu, &skey, &sdata, MDB_APPENDDUP);
	if(MDB_SUCCESS != rc) return rc;

	rc = mdb_cursor_del(ctx->lo, 0);
	if(MDB_SUCCESS != rc) return rc;
	// TODO: We assume this increments the cursor...

	return MDB_SUCCESS;
}
int lsmdb_compact(MDB_txn *const txn, MDB_dbi const dbi, LSMDB_level const level, unsigned const steps) {
	LSMDB_compaction ctx[1] = {{ txn, dbi }};
	rc = mdb_cursor_open(txn, dbi, &ctx->lo);
	if(MDB_SUCCESS != rc) goto cleanup;
	rc = mdb_cursor_open(txn, dbi, &ctx->hi);
	if(MDB_SUCCESS != rc) goto cleanup;
	rc = mdb_cursor_open(txn, dbi, &ctx->nu);
	if(MDB_SUCCESS != rc) goto cleanup; // TODO


	uint8_t buf[2];
	MDB_val key;

	buf = { level, 0x00 };
	key = { sizeof(buf), buf };
	rc = mdb_cursor_get(ctx->lo, &key, NULL, MDB_SET_RANGE);
	if(MDB_SUCCESS != rc) return rc;





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

