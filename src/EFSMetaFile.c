#include <yajl/yajl_parse.h>
#include "EarthFS.h"

#define META_MAX (1024 * 100)

typedef enum {
	s_start = 0,
	s_top,
		s_meta_uri,
		s_field_value,
		s_field_array,
	s_end,
} meta_state;

struct EFSMetaFile {
	size_t size;
	byte_t *buf;
	size_t len;
};

EFSMetaFileRef EFSMetaFileCreate(strarg_t const type) {
	if(!type) return NULL;
	if(0 != strcasecmp("text/efs-meta+json; charset=utf-8", type)) return NULL;

	EFSMetaFileRef meta = calloc(1, sizeof(struct EFSMetaFile));
	if(!meta) return NULL;
	meta->size = 0;
	meta->buf = NULL;
	meta->len = 0;
	return meta;
}
void EFSMetaFileFree(EFSMetaFileRef *const metaptr) {
	EFSMetaFileRef meta = *metaptr;
	if(!meta) return;
	meta->size = 0;
	FREE(&meta->buf);
	meta->len = 0;
	FREE(metaptr); meta = NULL;
}

err_t EFSMetaFileWrite(EFSMetaFileRef const meta, byte_t const *const buf, size_t const len) {
	if(!meta) return 0;
	if(meta->len + len > meta->size) {
		meta->size = MIN(META_MAX, MAX(meta->len + len, meta->size) * 2);
		meta->buf = realloc(meta->buf, meta->size);
		if(!meta->buf) return -1;
	}
	size_t const use = MIN(meta->size - meta->len, len);
	memcpy(meta->buf + meta->len, buf, use);
	meta->len += use;
	return 0;
}
err_t EFSMetaFileEnd(EFSMetaFileRef const meta) {
	if(!meta) return 0;
	return 0;
}


typedef struct {
	sqlite3 *db;
	int64_t fileID; // Us
	strarg_t fileURI;

	meta_state state;
	str_t *metaURI; // Target
	str_t *field;
} parse_state;

static yajl_callbacks const callbacks;

err_t EFSMetaFileStore(EFSMetaFileRef const meta, int64_t const fileID, strarg_t const fileURI, sqlite3 *const db) {
	if(!meta) return 0;
	parse_state state = {
		.db = db,
		.fileID = fileID,
		.fileURI = fileURI,

		.state = s_start,
		.metaURI = NULL,
		.field = NULL,
	};
	EXEC(QUERY(db, "SAVEPOINT metafile"));

	yajl_handle parser = yajl_alloc(&callbacks, NULL, &state);
	yajl_config(parser, yajl_allow_partial_values, (int)true);
	(void)yajl_parse(parser, meta->buf, meta->len);
	yajl_status const status = yajl_complete_parse(parser);
	if(yajl_status_ok != status) {
		EXEC(QUERY(db, "ROLLBACK TO metafile"));
		unsigned char *msg = yajl_get_error(parser, true, meta->buf, meta->len);
		fprintf(stderr, "%s", msg);
		yajl_free_error(parser, msg); msg = NULL;
		yajl_free(parser); parser = NULL;
		FREE(&state.metaURI);
		return -1;
	}
	yajl_free(parser); parser = NULL;
	FREE(&state.metaURI);

	EXEC(QUERY(db, "RELEASE metafile"));
	return 0;
}

static int yajl_null(parse_state *const state) {
	return false;
}
static int yajl_boolean(parse_state *const state, int const flag) {
	return false;
}
static int yajl_number(parse_state *const state, strarg_t const str, size_t const len) {
	return false;
}
static int yajl_string(parse_state *const state, strarg_t const str, size_t const len) {
	switch(state->state) {
		case s_meta_uri: {
			if(state->metaURI) return false;
			state->metaURI = strndup(str, len);
			sqlite3_stmt *insertMeta = QUERY(state->db,
				"INSERT OR IGNORE INTO meta_data\n"
				"	(meta_file_id, uri, field, value)\n"
				"VALUES (?, ?, 'link', ?)");
			sqlite3_bind_int64(insertMeta, 1, state->fileID);
			sqlite3_bind_text(insertMeta, 2, state->fileURI, -1, SQLITE_STATIC);
			sqlite3_bind_text(insertMeta, 3, state->metaURI, -1, SQLITE_STATIC);
			EXEC(insertMeta); insertMeta = NULL;
			state->state = s_top;
			return true;
		} case s_field_value: {
			/* fallthrough */
		} case s_field_array: {
			sqlite3_stmt *insertMeta = QUERY(state->db,
				"INSERT OR IGNORE INTO meta_data\n"
				"	(meta_file_id, uri, field, value)\n"
				"VALUES (?, ?, ?, ?)");
			sqlite3_bind_int64(insertMeta, 1, state->fileID);
			sqlite3_bind_text(insertMeta, 2, state->metaURI, -1, SQLITE_STATIC);
			sqlite3_bind_text(insertMeta, 3, state->field, -1, SQLITE_STATIC);
			sqlite3_bind_text(insertMeta, 4, str, len, SQLITE_STATIC);
			EXEC(insertMeta); insertMeta = NULL;
			// TODO: Full text indexing.
			if(s_field_value == state->state) {
				FREE(&state->field);
				state->state = s_top;
			}
			return true;
		} default: {
			return false;
		}
	}
	return true;
}
static int yajl_start_map(parse_state *const state) {
	switch(state->state) {
		case s_start: state->state = s_top; break;
		default: return false;
	}
	return true;
}
static int yajl_map_key(parse_state *const state, strarg_t const key, size_t const len) {
	switch(state->state) {
		case s_top:
			assertf(!state->field, "Already parsing field");
			if(substr("metaURI", key, len)) state->state = s_meta_uri;
			else {
				state->field = strndup(key, len);
				state->state = s_field_value;
			}
			break;
		default: assertf(0, "Unexpected map key in state %d", state->state);
	}
	return true;
}
static int yajl_end_map(parse_state *const state) {
	switch(state->state) {
		case s_top: state->state = s_end; break;
		default: assertf(0, "Unexpected map end in state %d", state->state);
	}
	return true;
}
static int yajl_start_array(parse_state *const state) {
	switch(state->state) {
		case s_field_value: state->state = s_field_array; break;
		default: return false;
	}
	return true;
}
static int yajl_end_array(parse_state *const state) {
	switch(state->state) {
		case s_field_array:
			FREE(&state->field);
			state->state = s_top;
			break;
		default: assertf(0, "Unexpected array end in state %d", state->state);
	}
	return true;
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

