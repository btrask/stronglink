// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <yajl/yajl_parse.h>
#include "util/fts.h"
#include "StrongLink.h"
#include "SLNDB.h"

#define BUF_LEN (1024 * 8)
#define PARSE_MAX (1024 * 1024 * 1)

#define DEPTH_MAX 1 // TODO
#define IGNORE_MAX 1023 // Just to prevent overflow.

typedef struct {
	DB_txn *txn;
	int64_t metaFileID;
	strarg_t targetURI;
	str_t *fields[DEPTH_MAX];
	int depth;
} parser_t;

static yajl_callbacks const callbacks;

// TODO: Error handling.
static uint64_t add_metafile(DB_txn *const txn, uint64_t const fileID, strarg_t const targetURI);
static void add_metadata(DB_txn *const txn, uint64_t const metaFileID, strarg_t const field, strarg_t const value);
static void add_fulltext(DB_txn *const txn, uint64_t const metaFileID, strarg_t const str, size_t const len);


int SLNSubmissionParseMetaFile(SLNSubmissionRef const sub, uint64_t const fileID, DB_txn *const txn, uint64_t *const out) {
	assert(out);
	if(!sub) return DB_EINVAL;
	if(!fileID) return DB_EINVAL;
	if(!txn) return DB_EINVAL;

	strarg_t const type = SLNSubmissionGetType(sub);
	if(!type) return 0;
	if(0 != strcasecmp(SLN_META_TYPE, type) &&
	   0 != strcasecmp("text/x-sln-meta+json; charset=utf-8", type) &&
	   0 != strcasecmp("text/efs-meta+json; charset=utf-8", type)) return 0;
	// TODO: Get rid of these obsolete types.

	uv_file const fd = SLNSubmissionGetFile(sub);
	if(fd < 0) return DB_EINVAL;

	int rc = 0;
	uv_buf_t buf[1] = {};
	DB_txn *subtxn = NULL;
	parser_t ctx[1] = {};
	yajl_handle parser = NULL;

	buf->base = malloc(BUF_LEN);
	if(!buf->base) { rc = DB_ENOMEM; goto cleanup; }

	buf->len = URI_MAX;
	int64_t pos = 0;
	ssize_t len = async_fs_read(fd, buf, 1, pos);
	if(len < 0) {
		alogf("Submission meta-file read error: %s\n", sln_strerror(len));
		rc = DB_EIO;
		goto cleanup;
	}

	size_t i;
	for(i = 0; i < len; i++) {
		char const c = buf->base[i];
		if('\r' == c || '\n' == c) break;
	}
	if(i >= len) {
		alogf("Submission meta-file parse error (invalid target URI)\n");
		rc = DB_EIO;
		goto cleanup;
	}
	assert(i < len);
	str_t targetURI[URI_MAX];
	memcpy(targetURI, buf->base, i);
	targetURI[i] = '\0';
	pos += i;

	strarg_t const knownTarget = SLNSubmissionGetKnownTarget(sub);
	if(knownTarget) {
		if(0 != strcmp(knownTarget, targetURI)) rc = SLN_TARGETMISMATCH;
		if(rc < 0) goto cleanup;
	}

	// TODO: Support sub-transactions in LevelDB backend.
	// TODO: db_txn_begin should support NULL env.
//	rc = db_txn_begin(NULL, txn, DB_RDWR, &subtxn);
//	if(rc < 0) goto cleanup;
	subtxn = txn;

	uint64_t const metaFileID = add_metafile(subtxn, fileID, targetURI);
	if(!metaFileID) goto cleanup;
	// Duplicate meta-file, not an error.
	// TODO: Unless the previous version wasn't actually a meta-file.

	ctx->txn = subtxn;
	ctx->metaFileID = metaFileID;
	ctx->targetURI = targetURI;
	ctx->depth = -1;
	parser = yajl_alloc(&callbacks, NULL, ctx);
	if(!parser) rc = DB_ENOMEM;
	if(rc < 0) goto cleanup;
	yajl_config(parser, yajl_allow_partial_values, (int)true);

	yajl_status status = yajl_status_ok;
	for(;;) {
		buf->len = MIN(BUF_LEN, PARSE_MAX-pos);
		if(!buf->len) break;
		len = async_fs_read(fd, buf, 1, pos);
		if(len < 0) {
			alogf("Submission meta-file read error: %s\n", sln_strerror(len));
			rc = DB_EIO;
			goto cleanup;
		}
		if(0 == len) break;
		status = yajl_parse(parser, (byte_t const *)buf->base, len);
		if(yajl_status_ok != status) break;
		pos += len;
	}
	status = yajl_complete_parse(parser);
	if(yajl_status_ok != status) {
		unsigned char *msg = yajl_get_error(parser, true, (byte_t const *)buf->base, len);
		alogf("%s", msg);
		yajl_free_error(parser, msg); msg = NULL;
		for(i = 0; i < DEPTH_MAX; i++) {
			FREE(&ctx->fields[i]);
		}
		ctx->depth = -1;
		rc = DB_EIO;
		goto cleanup;
	}

//	rc = db_txn_commit(subtxn); subtxn = NULL;
	if(rc < 0) goto cleanup;

	*out = metaFileID;

cleanup:
//	db_txn_abort(subtxn); subtxn = NULL;
	FREE(&buf->base);
	if(parser) yajl_free(parser); parser = NULL;
	assert(-1 == ctx->depth);
	assert_zeroed(ctx->fields, DEPTH_MAX);
	return rc;
}




// TODO: Better error handling. Use errno?

static int yajl_start_map(parser_t *const ctx) {
	if(ctx->depth >= IGNORE_MAX) return false;
	ctx->depth++;
	return true;
}
static int yajl_map_key(parser_t *const ctx, strarg_t const key, size_t const len) {
	if(1 == ctx->depth) { // TODO
		strarg_t const field = ctx->fields[ctx->depth-1];
		assert(field);
		if(0 == strcmp("fulltext", field)) {
			add_fulltext(ctx->txn, ctx->metaFileID, key, len);
		} else {
			str_t *x = strndup(key, len);
			if(!x) return false;
			add_metadata(ctx->txn, ctx->metaFileID, field, x);
			FREE(&x);
		}
	}
	if(ctx->depth < DEPTH_MAX) {
		assert(!ctx->fields[ctx->depth]);
		str_t *x = strndup(key, len);
		if(!x) return false;
		ctx->fields[ctx->depth] = x; x = NULL;
	}
	return true;
}
static int yajl_end_map(parser_t *const ctx) {
	if(ctx->depth < 0) return false;
	ctx->depth--;
	if(ctx->depth >= 0 && ctx->depth < DEPTH_MAX) {
		FREE(&ctx->fields[ctx->depth]);
	}
	return true;
}
static int yajl_string(parser_t *const ctx, strarg_t const str, size_t const len) {
	int x = true;
	x = !x ? x : yajl_start_map(ctx);
	x = !x ? x : yajl_map_key(ctx, str, len);
	x = !x ? x : yajl_start_map(ctx);
	x = !x ? x : yajl_end_map(ctx);
	x = !x ? x : yajl_end_map(ctx);
	return x;
}
static int yajl_null(parser_t *const ctx) {
	return false;
}
static int yajl_boolean(parser_t *const ctx, int const flag) {
	return false;
}
static int yajl_number(parser_t *const ctx, strarg_t const str, size_t const len) {
	return false;
}
static int yajl_start_array(parser_t *const ctx) {
	return false;
}
static int yajl_end_array(parser_t *const ctx) {
	return false;
}
static yajl_callbacks const callbacks = {
	.yajl_null = (int (*)())yajl_null,
	.yajl_boolean = (int (*)())yajl_boolean,
	.yajl_number = (int (*)())yajl_number,
	.yajl_string = (int (*)())yajl_string,
	.yajl_start_map = (int (*)())yajl_start_map,
	.yajl_map_key = (int (*)())yajl_map_key,
	.yajl_end_map = (int (*)())yajl_end_map,
	.yajl_start_array = (int (*)())yajl_start_array,
	.yajl_end_array = (int (*)())yajl_end_array,
};

static uint64_t add_metafile(DB_txn *const txn, uint64_t const fileID, strarg_t const targetURI) {
	uint64_t const metaFileID = fileID;
	uint64_t const latestMetaFileID = db_next_id(SLNMetaFileByID, txn);
	if(metaFileID < latestMetaFileID) return 0;
	// If it's not a new file, then it's not a new meta-file.
	// Note that ordinary files can't be "promoted" to meta-files later
	// because that would break the ordering.

	DB_val null = { 0, NULL };
	DB_cursor *cursor = NULL;
	int rc = db_txn_cursor(txn, &cursor);
	assert(rc >= 0);

	DB_val metaFileID_key[1];
	SLNMetaFileByIDKeyPack(metaFileID_key, txn, metaFileID);
	DB_val metaFile_val[1];
	SLNMetaFileByIDValPack(metaFile_val, txn, fileID, targetURI);
	rc = db_put(txn, metaFileID_key, metaFile_val, DB_NOOVERWRITE_FAST);
	assert(rc >= 0);

	DB_range alts[1];
	SLNTargetURIAndMetaFileIDRange1(alts, txn, targetURI);
	rc = db_cursor_firstr(cursor, alts, NULL, NULL, +1);
	assert(rc >= 0 || DB_NOTFOUND == rc);
	if(DB_NOTFOUND == rc) {
		DB_val unique[1];
		SLNFirstUniqueMetaFileIDKeyPack(unique, txn, metaFileID);
		rc = db_put(txn, unique, &null, DB_NOOVERWRITE_FAST);
		assert(rc >= 0);
	}

	DB_val targetURI_key[1];
	SLNTargetURIAndMetaFileIDKeyPack(targetURI_key, txn, targetURI, metaFileID);
	rc = db_put(txn, targetURI_key, &null, DB_NOOVERWRITE_FAST);
	assert(rc >= 0);

	return metaFileID;
}
static void add_metadata(DB_txn *const txn, uint64_t const metaFileID, strarg_t const field, strarg_t const value) {
	assert(field);
	assert(value);
	if('\0' == value[0]) return;

	DB_val null = { 0, NULL };
	int rc;

	DB_val fwd[1];
	SLNMetaFileIDFieldAndValueKeyPack(fwd, txn, metaFileID, field, value);
	rc = db_put(txn, fwd, &null, DB_NOOVERWRITE_FAST);
	assertf(rc >= 0 || DB_KEYEXIST == rc, "Database error %s", sln_strerror(rc));

	DB_val rev[1];
	SLNFieldValueAndMetaFileIDKeyPack(rev, txn, field, value, metaFileID);
	rc = db_put(txn, rev, &null, DB_NOOVERWRITE_FAST);
	assertf(rc >= 0 || DB_KEYEXIST == rc, "Database error %s", sln_strerror(rc));
}
static void add_fulltext(DB_txn *const txn, uint64_t const metaFileID, strarg_t const str, size_t const len) {
	if(0 == len) return;
	assert(str);

	int rc;

	sqlite3_tokenizer_module const *fts = NULL;
	sqlite3_tokenizer *tokenizer = NULL;
	fts_get(&fts, &tokenizer);

	sqlite3_tokenizer_cursor *tcur = NULL;
	rc = fts->xOpen(tokenizer, str, len, &tcur);
	assert(SQLITE_OK == rc);

	DB_cursor *cursor = NULL;
	rc = db_cursor_open(txn, &cursor);
	assert(rc >= 0);

	for(;;) {
		strarg_t token;
		int tlen;
		int tpos; // TODO
		int ignored1, ignored2;
		rc = fts->xNext(tcur, &token, &tlen, &ignored1, &ignored2, &tpos);
		if(SQLITE_OK != rc) break;

		assert('\0' == token[tlen]); // Assumption
		DB_val token_val[1];
		SLNTermMetaFileIDAndPositionKeyPack(token_val, txn, token, metaFileID, 0);
		// TODO: Record tpos. Requires changes to SLNFulltextFilter so that each document only gets returned once, no matter how many times the token appears within it.
		DB_val null = { 0, NULL };
		rc = db_cursor_put(cursor, token_val, &null, DB_NOOVERWRITE_FAST);
		assert(rc >= 0 || DB_KEYEXIST == rc);
	}

	db_cursor_close(cursor); cursor = NULL;

	fts->xClose(tcur); tcur = NULL;
}

