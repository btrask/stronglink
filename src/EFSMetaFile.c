#include <yajl/yajl_parse.h>
#include "async.h"
#include "EarthFS.h"

typedef enum {
	s_start = 0,
	s_top,
		s_meta_uri,
		s_field_value,
		s_field_array,
	s_end,
} meta_state;

struct EFSMetaFile {
	yajl_handle parser;
	sqlite3 *tmpdb;

	meta_state state;
	str_t *metaURI;
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
	if(SQLITE_OK != sqlite3_open_v2(
		NULL,
		&meta->tmpdb,
		SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE,
		NULL)
	) {
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
	yajl_status const status = yajl_parse(meta->parser, buf, len);
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


err_t EFSMetaFileStore(EFSMetaFileRef const meta, int64_t const fileID, strarg_t const fileURI, sqlite3 *const db) {
	if(!meta) return 0;
	if(!meta->parser) return -1;
	EXEC(QUERY(db, "SAVEPOINT metafile"));

	sqlite3_stmt *insertMetaLink = QUERY(db,
		"INSERT OR IGNORE INTO meta_data\n"
		"	(meta_file_id, uri, field, value)\n"
		"VALUES (?, ?, 'link', ?)");
	sqlite3_bind_int64(insertMetaLink, 1, fileID);
	sqlite3_bind_text(insertMetaLink, 2, fileURI, -1, SQLITE_STATIC);
	sqlite3_bind_text(insertMetaLink, 3, meta->metaURI, -1, SQLITE_STATIC);
	EXEC(insertMetaLink); insertMetaLink = NULL;


	// Would've been nice if we could ATTACH inside a transaction so we could INSERT SELECT. Although in practice this is probably just as fast because SQLite would have to do pretty much the same thing internally.
	sqlite3_stmt *insertField = QUERY(db,
		"INSERT OR IGNORE INTO meta_data\n"
		"	(meta_file_id, uri, field, value)\n"
		"VALUES (?, ?, ?, ?)");
	sqlite3_bind_int64(insertField, 1, fileID);
	sqlite3_bind_text(insertField, 2, meta->metaURI, -1, SQLITE_STATIC);
	sqlite3_stmt *selectField = QUERY(meta->tmpdb,
		"SELECT field, value FROM fields");
	while(SQLITE_ROW == STEP(selectField)) {
		strarg_t const field = (strarg_t)sqlite3_column_text(selectField, 0);
		strarg_t const value = (strarg_t)sqlite3_column_text(selectField, 1);
		sqlite3_bind_text(insertField, 3, field, -1, SQLITE_STATIC);
		sqlite3_bind_text(insertField, 4, value, -1, SQLITE_STATIC);
		STEP(insertField);
		sqlite3_reset(insertField);
	}
	sqlite3_finalize(selectField); selectField = NULL;
	sqlite3_finalize(insertField); insertField = NULL;

	EXEC(QUERY(db, "RELEASE metafile"));
	return 0;
}


static void cleanup(EFSMetaFileRef const meta) {
	if(meta->parser) { yajl_free(meta->parser); meta->parser = NULL; }
	sqlite3_close(meta->tmpdb); meta->tmpdb = NULL;
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
			if(meta->metaURI) return false;
			meta->metaURI = strndup(str, len);
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

