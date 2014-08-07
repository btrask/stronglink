#include <yajl/yajl_parse.h>
#include "strndup.h"
#include "async.h"
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
	size_t length;
	yajl_handle parser;
	sqlite3f *tmpdb;

	meta_state state;
	str_t *targetURI;
	str_t *field;
};

static yajl_callbacks const callbacks;

static void cleanup(EFSMetaFileRef const meta);
static void parse_error(EFSMetaFileRef const meta, byte_t const *const buf, size_t const len);

EFSMetaFileRef EFSMetaFileCreate(strarg_t const type) {
	if(!type) return NULL;
	if(0 != strcasecmp("text/efs-meta+json; charset=utf-8", type)) return NULL;

	EFSMetaFileRef meta = calloc(1, sizeof(struct EFSMetaFile));
	if(!meta) return NULL;
	meta->parser = yajl_alloc(&callbacks, NULL, meta);
	if(!meta->parser) {
		EFSMetaFileFree(&meta);
		return NULL;
	}
	yajl_config(meta->parser, yajl_allow_partial_values, (int)true);
	sqlite3 *db = NULL;
	if(SQLITE_OK != sqlite3_open_v2(
		":memory:",
		&db,
		SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE,
		NULL)
	) {
		EFSMetaFileFree(&meta);
		return NULL;
	}
	meta->tmpdb = sqlite3f_create(db);
	if(!db) {
		sqlite3_close(db); db = NULL;
		EFSMetaFileFree(&meta);
		return NULL;
	}
	EXEC(QUERY(meta->tmpdb,
		"CREATE TABLE fields (\n"
		"	field TEXT NOT NULL,\n"
		"	value TEXT NOT NULL\n"
		")"));
	return meta;
}
void EFSMetaFileFree(EFSMetaFileRef *const metaptr) {
	EFSMetaFileRef meta = *metaptr;
	if(!meta) return;
	cleanup(meta);
	FREE(metaptr); meta = NULL;
}

err_t EFSMetaFileWrite(EFSMetaFileRef const meta, byte_t const *const buf, size_t const len) {
	if(!meta) return 0;
	if(!meta->parser) return -1;
	if(meta->length > META_MAX) return -1;
	meta->length += len;
	yajl_status const status = yajl_parse(meta->parser, buf, MIN(len, META_MAX - meta->length));
	if(yajl_status_ok != status) {
		parse_error(meta, buf, len);
		return -1;
	}
	return 0;
}
err_t EFSMetaFileEnd(EFSMetaFileRef const meta) {
	if(!meta) return 0;
	if(!meta->parser) return -1;
	yajl_status const status = yajl_complete_parse(meta->parser);
	if(yajl_status_ok != status) {
		parse_error(meta, NULL, 0);
		return -1;
	}
	return 0;
}


err_t EFSMetaFileStore(EFSMetaFileRef const meta, int64_t const fileID, strarg_t const fileURI, sqlite3f *const db) {
	if(!meta) return 0;
	if(!meta->parser) return -1;
	EXEC(QUERY(db, "SAVEPOINT metafile"));

	sqlite3_stmt *insertMetaFile = QUERY(db,
		"INSERT INTO meta_files (file_id, target_uri) VALUES (?, ?)");
	sqlite3_bind_int64(insertMetaFile, 1, fileID);
	sqlite3_bind_text(insertMetaFile, 2, fileURI, -1, SQLITE_STATIC);
	sqlite3_step(insertMetaFile);
	sqlite3_reset(insertMetaFile);
	int64_t const self = sqlite3_last_insert_rowid(db->conn);

	sqlite3_stmt *insertMetaData = QUERY(db,
		"INSERT INTO meta_data (meta_file_id, field, value) VALUES (?, ?, ?)");
	sqlite3_bind_int64(insertMetaData, 1, self);
	sqlite3_bind_text(insertMetaData, 2, "link", -1, SQLITE_STATIC);
	sqlite3_bind_text(insertMetaData, 3, meta->targetURI, -1, SQLITE_STATIC);
	sqlite3_step(insertMetaData);
	sqlite3_reset(insertMetaData);

	sqlite3_bind_text(insertMetaFile, 2, meta->targetURI, -1, SQLITE_STATIC);
	sqlite3_step(insertMetaFile);
	sqlite3_reset(insertMetaFile);
	sqlite3f_finalize(insertMetaFile); insertMetaFile = NULL;
	int64_t const metaFileID = sqlite3_last_insert_rowid(db->conn);

	sqlite3_bind_int64(insertMetaData, 1, metaFileID);
	sqlite3_stmt *selectMetaData = QUERY(meta->tmpdb,
		"SELECT field, value FROM fields");
	while(SQLITE_ROW == STEP(selectMetaData)) {
		strarg_t const field = (strarg_t)sqlite3_column_text(selectMetaData, 0);
		strarg_t const value = (strarg_t)sqlite3_column_text(selectMetaData, 1);
		sqlite3_bind_text(insertMetaData, 2, field, -1, SQLITE_STATIC);
		sqlite3_bind_text(insertMetaData, 3, value, -1, SQLITE_STATIC);
		STEP(insertMetaData);
		sqlite3_reset(insertMetaData);
	}
	sqlite3f_finalize(selectMetaData); selectMetaData = NULL;
	sqlite3f_finalize(insertMetaData); insertMetaData = NULL;

	EXEC(QUERY(db, "RELEASE metafile"));
	return 0;
}


static void cleanup(EFSMetaFileRef const meta) {
	if(meta->parser) { yajl_free(meta->parser); meta->parser = NULL; }
	sqlite3f_close(meta->tmpdb); meta->tmpdb = NULL;
	meta->state = s_start;
	FREE(&meta->targetURI);
	FREE(&meta->field);
}
static void parse_error(EFSMetaFileRef const meta, byte_t const *const buf, size_t const len) {
	unsigned char *msg = yajl_get_error(meta->parser, true, buf, len);
	fprintf(stderr, "%s", msg);
	yajl_free_error(meta->parser, msg); msg = NULL;
	cleanup(meta);
}

static int yajl_null(EFSMetaFileRef const meta) {
	return false;
}
static int yajl_boolean(EFSMetaFileRef const meta, int const flag) {
	return false;
}
static int yajl_number(EFSMetaFileRef const meta, strarg_t const str, size_t const len) {
	return false;
}
static int yajl_string(EFSMetaFileRef const meta, strarg_t const str, size_t const len) {
	switch(meta->state) {
		case s_meta_uri: {
			if(meta->targetURI) return false;
			meta->targetURI = strndup(str, len);
			meta->state = s_top;
			return true;
		} case s_field_value: {
			/* fallthrough */
		} case s_field_array: {
			sqlite3_stmt *insertMeta = QUERY(meta->tmpdb,
				"INSERT OR IGNORE INTO fields (field, value)\n"
				"VALUES (?, ?)");
			sqlite3_bind_text(insertMeta, 1, meta->field, -1, SQLITE_STATIC);
			sqlite3_bind_text(insertMeta, 2, str, len, SQLITE_STATIC);
			EXEC(insertMeta); insertMeta = NULL;
			// TODO: Full text indexing.
			if(s_field_value == meta->state) {
				FREE(&meta->field);
				meta->state = s_top;
			}
			return true;
		} default: {
			return false;
		}
	}
	return true;
}
static int yajl_start_map(EFSMetaFileRef const meta) {
	switch(meta->state) {
		case s_start: meta->state = s_top; break;
		default: return false;
	}
	return true;
}
static int yajl_map_key(EFSMetaFileRef const meta, strarg_t const key, size_t const len) {
	switch(meta->state) {
		case s_top:
			assertf(!meta->field, "Already parsing field");
			// TODO: Rename this field targetURI since it's ambiguous, but also we want to move it out of the JSON object so that it is guaranteed to be at the beginning of the file and our whole parser can be single-pass.
			if(substr("metaURI", key, len)) meta->state = s_meta_uri;
			else {
				meta->field = strndup(key, len);
				meta->state = s_field_value;
			}
			break;
		default: assertf(0, "Unexpected map key in state %d", meta->state);
	}
	return true;
}
static int yajl_end_map(EFSMetaFileRef const meta) {
	switch(meta->state) {
		case s_top: meta->state = s_end; break;
		default: assertf(0, "Unexpected map end in state %d", meta->state);
	}
	return true;
}
static int yajl_start_array(EFSMetaFileRef const meta) {
	switch(meta->state) {
		case s_field_value: meta->state = s_field_array; break;
		default: return false;
	}
	return true;
}
static int yajl_end_array(EFSMetaFileRef const meta) {
	switch(meta->state) {
		case s_field_array:
			FREE(&meta->field);
			meta->state = s_top;
			break;
		default: assertf(0, "Unexpected array end in state %d", meta->state);
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

