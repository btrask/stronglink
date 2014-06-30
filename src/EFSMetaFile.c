#include <yajl/yajl_parse.h>
#include "EarthFS.h"

#define META_MAX (1024 * 100)

typedef enum {
	s_start = 0,
	s_top,
		s_skip,
		s_meta_uri,
		s_title,
		s_desc,
		s_link_start,
			s_link,
	s_end,
} meta_state;

struct EFSMetaFile {
	yajl_handle parser;
	int64_t size;
	meta_state state;
	int skip;
	str_t *URI;
	str_t *title;
	str_t *desc;
	URIListRef links;
};

static yajl_callbacks const callbacks;

EFSMetaFileRef EFSMetaFileCreate(strarg_t const type) {
	if(!type) return NULL;
	if(0 != strcasecmp("text/efs-meta+json; charset=utf-8", type)) return NULL;

	EFSMetaFileRef const meta = calloc(1, sizeof(struct EFSMetaFile));
	meta->parser = yajl_alloc(&callbacks, NULL, meta);
	meta->size = 0;
	meta->state = s_start;
	meta->skip = 0;
	meta->URI = NULL;
	meta->title = NULL;
	meta->desc = NULL;
	meta->links = URIListCreate();
	return meta;
}
void EFSMetaFileFree(EFSMetaFileRef const meta) {
	if(!meta) return;
	if(meta->parser) {
		yajl_free(meta->parser); meta->parser = NULL;
	}
	FREE(&meta->URI);
	FREE(&meta->title);
	FREE(&meta->desc);
	URIListFree(meta->links); meta->links = NULL;
	free(meta);
}

err_t EFSMetaFileWrite(EFSMetaFileRef const meta, byte_t const *const buf, size_t const len) {
	if(!meta) return 0;
	meta->size += len;
	if(meta->size > META_MAX) return -1;
	if(!meta->parser) return -1;
	yajl_status const status = yajl_parse(meta->parser, buf, len);
	if(yajl_status_ok != status) {
		unsigned char *msg = yajl_get_error(meta->parser, true, buf, len);
		fprintf(stderr, "%s", msg);
		yajl_free_error(meta->parser, msg); msg = NULL;
		yajl_free(meta->parser); meta->parser = NULL;
	}
	return yajl_status_ok == status ? 0 : -1;
}
err_t EFSMetaFileEnd(EFSMetaFileRef const meta) {
	if(!meta) return 0;
	if(meta->size > META_MAX) return -1;
	if(!meta->parser) return -1;
	yajl_status const status = yajl_complete_parse(meta->parser);
	if(yajl_status_ok != status) {
		unsigned char *msg = yajl_get_error(meta->parser, true, NULL, 0);
		fprintf(stderr, "%s", msg);
		yajl_free_error(meta->parser, msg); msg = NULL;
	}
	return yajl_status_ok == status ? 0 : -1;
}
err_t EFSMetaFileStore(EFSMetaFileRef const meta, int64_t const fileID, strarg_t const URI, sqlite3 *const db) {
	if(!meta) return 0;
	if(!meta->parser) return -1;
	if(meta->size > META_MAX) {
		fprintf(stderr, "Meta-file too large (%llu)\n", (unsigned long long)meta->size);
		return -1;
	}
	if(s_end != meta->state) {
		fprintf(stderr, "Meta-file parse incomplete (state: %d)\n", meta->state);
		return -1;
	}
	if(!meta->URI) {
		fprintf(stderr, "Meta-file missing URI\n");
		return -1;
	}

	sqlite3_stmt *const insertURI = QUERY(db,
		"INSERT OR IGNORE INTO uris (uri) VALUES (?)");
	sqlite3_stmt *const insertLink = QUERY(db,
		"INSERT OR IGNORE INTO links\n"
		"	(source_uri_id, target_uri_id, meta_file_id)\n"
		"SELECT s.uri_id, t.uri_id, ?\n"
		"FROM uris AS s\n"
		"INNER JOIN uris AS t\n"
		"WHERE s.uri = ? AND t.uri = ?");
	sqlite3_bind_int64(insertLink, 1, fileID);

	sqlite3_bind_text(insertURI, 1, meta->URI, -1, SQLITE_STATIC);
	sqlite3_step(insertURI); sqlite3_reset(insertURI);
	sqlite3_bind_text(insertLink, 2, URI, -1, SQLITE_STATIC);
	sqlite3_bind_text(insertLink, 3, meta->URI, -1, SQLITE_STATIC);
	sqlite3_step(insertLink); sqlite3_reset(insertLink);

	count_t const linkCount = URIListGetCount(meta->links);
	sqlite3_bind_text(insertLink, 2, meta->URI, -1, SQLITE_STATIC);
	for(index_t i = 0; i < linkCount; ++i) {
		strarg_t const link = URIListGetURI(meta->links, i);
		size_t const len = strlen(link);
		sqlite3_bind_text(insertURI, 1, link, len, SQLITE_STATIC);
		sqlite3_step(insertURI); sqlite3_reset(insertURI);
		sqlite3_bind_text(insertLink, 3, link, len, SQLITE_STATIC);
		sqlite3_step(insertLink); sqlite3_reset(insertLink);
	}

	sqlite3_finalize(insertURI);
	sqlite3_finalize(insertLink);

	// TODO: title, description

	return 0;
}


static int yajl_null(EFSMetaFileRef const meta) {
	switch(meta->state) {
		case s_skip: if(!meta->skip) meta->state = s_top; break;
		default: return false;
	}
	return true;
}
static int yajl_boolean(EFSMetaFileRef const meta, int const flag) {
	switch(meta->state) {
		case s_skip: if(!meta->skip) meta->state = s_top; break;
		default: return false;
	}
	return true;
}
static int yajl_number(EFSMetaFileRef const meta, strarg_t const str, size_t const len) {
	switch(meta->state) {
		case s_skip: if(!meta->skip) meta->state = s_top; break;
		default: return false;
	}
	return true;
}
static int yajl_string(EFSMetaFileRef const meta, strarg_t const str, size_t const len) {
	switch(meta->state) {
		case s_skip: if(!meta->skip) meta->state = s_top; break;
		case s_meta_uri:
			meta->URI = strndup(str, len);
			meta->state = s_top;
			break;
		case s_link:
			URIListAddURI(meta->links, str, len);
			meta->state = s_link;
			break;
		case s_title:
			meta->title = strndup(str, len);
			meta->state = s_top;
			break;
		case s_desc:
			meta->desc = strndup(str, len);
			meta->state = s_top;
			break;
		default: return false;
	}
	return true;
}
static int yajl_start_map(EFSMetaFileRef const meta) {
	switch(meta->state) {
		case s_skip: ++meta->skip; break;
		case s_start: meta->state = s_top; break;
		default: return false;
	}
	return true;
}
static int yajl_map_key(EFSMetaFileRef const meta, strarg_t const key, size_t const len) {
	switch(meta->state) {
		case s_skip: break;
		case s_top:
			meta->state = s_skip;
			assertf(0 == meta->skip, "Invalid skip depth");
			if(substr("metaURI", key, len)) meta->state = s_meta_uri;
			if(substr("links", key, len)) meta->state = s_link_start;
			if(substr("description", key, len)) meta->state = s_desc;
			if(substr("title", key, len)) meta->state = s_title;
			break;
		default: assertf(0, "Unexpected map key in state %d", meta->state);
	}
	return true;
}
static int yajl_end_map(EFSMetaFileRef const meta) {
	switch(meta->state) {
		case s_skip: if(!--meta->skip) meta->state = s_top; break;
		case s_top: meta->state = s_end; break;
		default: assertf(0, "Unexpected map end in state %d", meta->state);
	}
	return true;
}
static int yajl_start_array(EFSMetaFileRef const meta) {
	switch(meta->state) {
		case s_skip: ++meta->skip; break;
		case s_link_start: meta->state = s_link; break;
		default: return false;
	}
	return true;
}
static int yajl_end_array(EFSMetaFileRef const meta) {
	switch(meta->state) {
		case s_skip: if(!--meta->skip) meta->state = s_top; break;
		case s_link_start:
		case s_link: meta->state = s_top; break;
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

