#include <yajl/yajl_parse.h>
#include "util/fts.h"
#include "StrongLink.h"
#include "SLNDB.h"

#define BUF_LEN (1024 * 8)
#define PARSE_MAX (1024 * 1024 * 1)

typedef enum {
	s_start = 0,
	s_top,
		s_field_value,
		s_field_array,
	s_end,
} parser_state;

typedef struct {
	DB_txn *txn;
	int64_t metaFileID;
	strarg_t targetURI;
	parser_state state;
	str_t *field;
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
	if(!type) return DB_SUCCESS;
	if(0 != strcasecmp("text/x-sln-meta+json; charset=utf-8", type) &&
	   0 != strcasecmp("text/efs-meta+json; charset=utf-8", type)) return DB_SUCCESS;

	uv_file const fd = SLNSubmissionGetFile(sub);
	if(fd < 0) return DB_EINVAL;

	int rc = DB_SUCCESS;
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
		fprintf(stderr, "Submission meta-file read error (%s)\n", uv_strerror(len));
		rc = -1;
		goto cleanup;
	}

	size_t i;
	for(i = 0; i < len; i++) {
		char const c = buf->base[i];
		if('\r' == c || '\n' == c) break;
	}
	if(i >= len) {
		fprintf(stderr, "Submission meta-file parse error (invalid target URI)\n");
		rc = -1;
		goto cleanup;
	}
	str_t targetURI[URI_MAX];
	memcpy(targetURI, buf->base, i);
	targetURI[i] = '\0';
	pos += i;

	// TODO: Support sub-transactions in LevelDB backend.
	// TODO: db_txn_begin should support NULL env.
//	rc = db_txn_begin(NULL, txn, DB_RDWR, &subtxn);
//	if(DB_SUCCESS != rc) goto cleanup;
	subtxn = txn;

	uint64_t metaFileID = 0;
	metaFileID = add_metafile(subtxn, fileID, targetURI);
//	if(DB_SUCCESS != rc) goto cleanup;

	ctx->txn = subtxn;
	ctx->metaFileID = metaFileID;
	ctx->targetURI = targetURI;
	ctx->state = s_start;
	ctx->field = NULL;
	parser = yajl_alloc(&callbacks, NULL, ctx);
	if(!parser) { rc = DB_ENOMEM; goto cleanup; }
	yajl_config(parser, yajl_allow_partial_values, (int)true);

	yajl_status status = yajl_status_ok;
	for(;;) {
		buf->len = MIN(BUF_LEN, PARSE_MAX-pos);
		if(!buf->len) break;
		len = async_fs_read(fd, buf, 1, pos);
		if(len < 0) {
			fprintf(stderr, "Submission meta-file read error (%s)\n", uv_strerror(len));
			rc = -1;
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
		fprintf(stderr, "%s", msg);
		yajl_free_error(parser, msg); msg = NULL;
		rc = -1;
		goto cleanup;
	}

//	rc = db_txn_commit(subtxn); subtxn = NULL;
	if(DB_SUCCESS != rc) goto cleanup;

	*out = metaFileID;

cleanup:
//	db_txn_abort(subtxn); subtxn = NULL;
	FREE(&buf->base);
	if(parser) yajl_free(parser); parser = NULL;
	FREE(&ctx->field);
	return rc;
}




// TODO: Improve style, error handling.

static int yajl_null(parser_t *const ctx) {
	return false;
}
static int yajl_boolean(parser_t *const ctx, int const flag) {
	return false;
}
static int yajl_number(parser_t *const ctx, strarg_t const str, size_t const len) {
	return false;
}
static int yajl_string(parser_t *const ctx, strarg_t const str, size_t const len) {
	switch(ctx->state) {
	case s_field_value:
	case s_field_array: {
		if(len) {
			if(0 == strcmp("fulltext", ctx->field)) {
				add_fulltext(ctx->txn, ctx->metaFileID, str, len);
			} else {
				str_t *dup = strndup(str, len);
				assert(dup); // TODO
				add_metadata(ctx->txn, ctx->metaFileID, ctx->field, dup);
				FREE(&dup);
			}
		}
		if(s_field_value == ctx->state) {
			FREE(&ctx->field);
			ctx->state = s_top;
		}
		return true;
	}
	default:
		return false;
	}
}
static int yajl_start_map(parser_t *const ctx) {
	switch(ctx->state) {
	case s_start:
		ctx->state = s_top;
		return true;
	default:
		return false;
	}
}
static int yajl_map_key(parser_t *const ctx, strarg_t const key, size_t const len) {
	switch(ctx->state) {
	case s_top:
		assertf(!ctx->field, "Already parsing field");
		ctx->field = strndup(key, len);
		ctx->state = s_field_value;
		return true;
	default:
		assertf(0, "Unexpected map key in state %d", ctx->state);
		return false;
	}
}
static int yajl_end_map(parser_t *const ctx) {
	switch(ctx->state) {
	case s_top:
		ctx->state = s_end;
		return true;
	default:
		assertf(0, "Unexpected map end in state %d", ctx->state);
		return false;
	}
}
static int yajl_start_array(parser_t *const ctx) {
	switch(ctx->state) {
	case s_field_value:
		ctx->state = s_field_array;
		return true;
	default:
		return false;
	}
}
static int yajl_end_array(parser_t *const ctx) {
	switch(ctx->state) {
	case s_field_array:
		FREE(&ctx->field);
		ctx->state = s_top;
		return true;
	default:
		assertf(0, "Unexpected array end in state %d", ctx->state);
		return false;
	}
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
	uint64_t const metaFileID = db_next_id(SLNMetaFileByID, txn);
	assert(metaFileID);
	int rc;
	DB_val null = { 0, NULL };

	DB_val metaFileID_key[1];
	SLNMetaFileByIDKeyPack(metaFileID_key, txn, metaFileID);
	DB_val metaFile_val[1];
	SLNMetaFileByIDValPack(metaFile_val, txn, fileID, targetURI);
	rc = db_put(txn, metaFileID_key, metaFile_val, DB_NOOVERWRITE_FAST);
	assert(!rc);

	DB_val fileID_key[1];
	SLNFileIDAndMetaFileIDKeyPack(fileID_key, txn, fileID, metaFileID);
	rc = db_put(txn, fileID_key, &null, DB_NOOVERWRITE_FAST);
	assert(!rc);

	DB_val targetURI_key[1];
	SLNTargetURIAndMetaFileIDKeyPack(targetURI_key, txn, targetURI, metaFileID);
	rc = db_put(txn, targetURI_key, &null, DB_NOOVERWRITE_FAST);
	assert(!rc);

	return metaFileID;
}
static void add_metadata(DB_txn *const txn, uint64_t const metaFileID, strarg_t const field, strarg_t const value) {
	DB_val null = { 0, NULL };
	int rc;

	DB_val fwd[1];
	SLNMetaFileIDFieldAndValueKeyPack(fwd, txn, metaFileID, field, value);
	rc = db_put(txn, fwd, &null, DB_NOOVERWRITE_FAST);
	assertf(DB_SUCCESS == rc || DB_KEYEXIST == rc, "Database error %s", db_strerror(rc));

	DB_val rev[1];
	SLNFieldValueAndMetaFileIDKeyPack(rev, txn, field, value, metaFileID);
	rc = db_put(txn, rev, &null, DB_NOOVERWRITE_FAST);
	assertf(DB_SUCCESS == rc || DB_KEYEXIST == rc, "Database error %s", db_strerror(rc));
}
static void add_fulltext(DB_txn *const txn, uint64_t const metaFileID, strarg_t const str, size_t const len) {
	int rc;

	sqlite3_tokenizer_module const *fts = NULL;
	sqlite3_tokenizer *tokenizer = NULL;
	fts_get(&fts, &tokenizer);

	sqlite3_tokenizer_cursor *tcur = NULL;
	rc = fts->xOpen(tokenizer, str, len, &tcur);
	assert(SQLITE_OK == rc);

	DB_cursor *cursor = NULL;
	rc = db_cursor_open(txn, &cursor);
	assert(DB_SUCCESS == rc);

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
		assert(DB_SUCCESS == rc || DB_KEYEXIST == rc);
	}

	db_cursor_close(cursor); cursor = NULL;

	fts->xClose(tcur); tcur = NULL;
}

