#include "EFSFilter.h"
#include "../fts.h"

@implementation EFSIndividualFilter
- (void)free {
	db_cursor_close(step_target); step_target = NULL;
	db_cursor_close(step_files); step_files = NULL;
	db_cursor_close(age_uris); age_uris = NULL;
	db_cursor_close(age_metafiles); age_metafiles = NULL;
	[super free];
}

- (EFSFilter *)unwrap {
	return self;
}

- (err_t)prepare:(DB_txn *const)txn :(EFSConnection const *const)conn {
	if([super prepare:txn :conn] < 0) return -1;
	db_cursor_renew(txn, &step_target); // EFSMetaFileByID
	db_cursor_renew(txn, &step_files); // EFSURIAndFileID
	db_cursor_renew(txn, &age_uris); // EFSFileIDAndURI
	db_cursor_renew(txn, &age_metafiles); // EFSTargetURIAndMetaFileID
	curtxn = txn;
	return 0;
}
- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	int rc;
	uint64_t const actualSortID = [self seekMeta:dir :sortID];
	if(!valid(actualSortID)) return;
	DB_VAL(metaFileID_key, DB_VARINT_MAX + DB_VARINT_MAX);
	db_bind_uint64(metaFileID_key, EFSMetaFileByID);
	db_bind_uint64(metaFileID_key, actualSortID);
	DB_val metaFile_val[1];
	rc = db_cursor_seek(step_target, metaFileID_key, metaFile_val, 0);
	assertf(DB_SUCCESS == rc, "Database error %s", db_strerror(rc));
	uint64_t const metaFileFileID = db_read_uint64(metaFile_val);
	strarg_t const targetURI = db_read_string(curtxn, metaFile_val);
	DB_VAL(targetURI_val, DB_VARINT_MAX);
	db_bind_string(curtxn, targetURI_val, targetURI);

	DB_RANGE(fileIDs, DB_VARINT_MAX + DB_INLINE_MAX);
	db_bind_uint64(fileIDs->min, EFSURIAndFileID);
	db_bind_string(curtxn, fileIDs->min, targetURI);
	db_range_genmax(fileIDs);
	if(sortID == actualSortID) {
		DB_VAL(fileID_key, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1);
		db_bind_uint64(fileID_key, EFSURIAndFileID);
		db_bind_string(curtxn, fileID_key, targetURI);
		db_bind_uint64(fileID_key, fileID);
		rc = db_cursor_seekr(step_files, fileIDs, fileID_key, NULL, dir);
	} else {
		DB_val fileID_key[1];
		rc = db_cursor_firstr(step_files, fileIDs, fileID_key, NULL, dir);
	}
	assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));
}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	DB_val fileID_key[1];
	int rc = db_cursor_current(step_files, fileID_key, NULL);
	if(DB_SUCCESS == rc) {
		uint64_t const table = db_read_uint64(fileID_key);
		assert(EFSURIAndFileID == table);
		strarg_t const targetURI = db_read_string(curtxn, fileID_key); // TODO: Unused read.
		if(sortID) *sortID = [self currentMeta:dir];
		if(fileID) *fileID = db_read_uint64(fileID_key);
	} else {
		if(sortID) *sortID = invalid(dir);
		if(fileID) *fileID = invalid(dir);
	}
}
- (void)step:(int const)dir {
	int rc;
	DB_val fileID_key[1];
	rc = db_cursor_current(step_files, fileID_key, NULL);
	if(DB_SUCCESS == rc) {
		uint64_t const table = db_read_uint64(fileID_key);
		assert(EFSURIAndFileID == table);
		strarg_t const targetURI = db_read_string(curtxn, fileID_key);
		DB_RANGE(fileIDs, DB_VARINT_MAX + DB_INLINE_MAX);
		db_bind_uint64(fileIDs->min, EFSURIAndFileID);
		db_bind_string(curtxn, fileIDs->min, targetURI);
		db_range_genmax(fileIDs);
		rc = db_cursor_nextr(step_files, fileIDs, fileID_key, NULL, dir);
		if(DB_SUCCESS == rc) return;
	}

	for(uint64_t sortID = [self stepMeta:dir]; valid(sortID); sortID = [self stepMeta:dir]) {
		DB_VAL(metaFileID_key, DB_VARINT_MAX + DB_VARINT_MAX);
		db_bind_uint64(metaFileID_key, EFSMetaFileByID);
		db_bind_uint64(metaFileID_key, sortID);
		DB_val metaFile_val[1];
		rc = db_cursor_seek(step_target, metaFileID_key, metaFile_val, 0);
		assertf(DB_SUCCESS == rc, "Database error %s", db_strerror(rc));
		uint64_t const f = db_read_uint64(metaFile_val);
		strarg_t const targetURI = db_read_string(curtxn, metaFile_val);

		DB_RANGE(fileIDs, DB_VARINT_MAX + DB_INLINE_MAX);
		db_bind_uint64(fileIDs->min, EFSURIAndFileID);
		db_bind_string(curtxn, fileIDs->min, targetURI);
		db_range_genmax(fileIDs);
		DB_val fileID_key[1];
		rc = db_cursor_firstr(step_files, fileIDs, fileID_key, NULL, +1);
		if(DB_SUCCESS != rc) continue;
		return;
	}
}
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID {
	uint64_t earliest = UINT64_MAX;
	int rc;

	DB_RANGE(URIs, DB_VARINT_MAX + DB_VARINT_MAX);
	db_bind_uint64(URIs->min, EFSFileIDAndURI);
	db_bind_uint64(URIs->min, fileID);
	db_range_genmax(URIs);
	DB_val URI_val[1];
	rc = db_cursor_firstr(age_uris, URIs, URI_val, NULL, +1);
	assert(DB_SUCCESS == rc || DB_NOTFOUND == rc);

	for(; DB_SUCCESS == rc; rc = db_cursor_nextr(age_uris, URIs, URI_val, NULL, +1)) {
		uint64_t const table = db_read_uint64(URI_val);
		assert(EFSFileIDAndURI == table);
		uint64_t const f = db_read_uint64(URI_val);
		assert(fileID == f);
		strarg_t const targetURI = db_read_string(curtxn, URI_val);

		DB_RANGE(metafiles, DB_VARINT_MAX + DB_INLINE_MAX);
		db_bind_uint64(metafiles->min, EFSTargetURIAndMetaFileID);
		db_bind_string(curtxn, metafiles->min, targetURI);
		db_range_genmax(metafiles);
		DB_val metaFileID_key[1];
		rc = db_cursor_firstr(age_metafiles, metafiles, metaFileID_key, NULL, +1);
		assert(DB_SUCCESS == rc || DB_NOTFOUND == rc);
		for(; DB_SUCCESS == rc; rc = db_cursor_nextr(age_metafiles, metafiles, metaFileID_key, NULL, +1)) {
			uint64_t const table = db_read_uint64(metaFileID_key);
			assert(EFSTargetURIAndMetaFileID == table);
			strarg_t const u = db_read_string(curtxn, metaFileID_key);
			assert(0 == strcmp(targetURI, u));
			uint64_t const metaFileID = db_read_uint64(metaFileID_key);
			if(metaFileID > sortID) break;
			if(![self match:metaFileID]) continue;
			if(metaFileID < earliest) earliest = metaFileID;
			break;
		}
	}
	return earliest;
}
@end

@implementation EFSAllFilter
- (void)free {
	db_cursor_close(metafiles); metafiles = NULL;
	[super free];
}

- (EFSFilterType)type {
	return EFSAllFilterType;
}
- (void)print:(count_t const)depth {
	indent(depth);
	fprintf(stderr, "(all)\n");
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth {
	if(depth) return wr(data, size, "*");
	return wr(data, size, "");
}

- (err_t)prepare:(DB_txn *const)txn :(EFSConnection const *const)conn {
	if([super prepare:txn :conn] < 0) return -1;
	db_cursor_renew(txn, &metafiles); // EFSMetaFileByID
	return 0;
}

- (uint64_t)seekMeta:(int const)dir :(uint64_t const)sortID {
	DB_RANGE(range, DB_VARINT_MAX);
	db_bind_uint64(range->min, EFSMetaFileByID);
	db_range_genmax(range);
	DB_VAL(sortID_key, DB_VARINT_MAX + DB_VARINT_MAX);
	db_bind_uint64(sortID_key, EFSMetaFileByID);
	db_bind_uint64(sortID_key, sortID);
	int rc = db_cursor_seekr(metafiles, range, sortID_key, NULL, dir);
	if(DB_SUCCESS != rc) return invalid(dir);
	uint64_t const table = db_read_uint64(sortID_key);
	assert(EFSMetaFileByID == table);
	return db_read_uint64(sortID_key);
}
- (uint64_t)currentMeta:(int const)dir {
	DB_val sortID_key[1];
	int rc = db_cursor_current(metafiles, sortID_key, NULL);
	if(DB_SUCCESS != rc) return invalid(dir);
	uint64_t const table = db_read_uint64(sortID_key);
	assert(EFSMetaFileByID == table);
	return db_read_uint64(sortID_key);
}
- (uint64_t)stepMeta:(int const)dir {
	DB_RANGE(range, DB_VARINT_MAX);
	db_bind_uint64(range->min, EFSMetaFileByID);
	db_range_genmax(range);
	DB_val sortID_key[1];
	int rc = db_cursor_nextr(metafiles, range, sortID_key, NULL, dir);
	if(DB_SUCCESS != rc) return invalid(dir);
	uint64_t const table = db_read_uint64(sortID_key);
	assert(EFSMetaFileByID == table);
	return db_read_uint64(sortID_key);
}
- (bool_t)match:(uint64_t const)metaFileID {
	return true;
}
@end

@implementation EFSFulltextFilter
- (void)free {
	FREE(&term);
	for(index_t i = 0; i < count; ++i) {
		FREE(&tokens[i].str);
	}
	FREE(&tokens);
	db_cursor_close(metafiles); metafiles = NULL;
	db_cursor_close(match); match = NULL;
	[super free];
}

- (EFSFilterType)type {
	return EFSFulltextFilterType;
}
- (strarg_t)stringArg:(index_t const)i {
	if(0 != i) return NULL;
	return term;
}
- (err_t)addStringArg:(strarg_t const)str :(size_t const)len {
	if(!str) return -1;
	if(0 == len) return -1;
	if(term) return -1;
	term = strndup(str, len);
	return 0;
}
- (void)print:(count_t const)depth {
	indent(depth);
	fprintf(stderr, "(fulltext %s)\n", term);
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth {
	return wr_quoted(data, size, term);
}

- (err_t)prepare:(DB_txn *const)txn :(EFSConnection const *const)conn {
	if([super prepare:txn :conn] < 0) return -1;
	db_cursor_renew(txn, &metafiles);
	db_cursor_renew(txn, &match);

	count = 0;

	// TODO: libstemmer?
	sqlite3_tokenizer_module const *fts = NULL;
	sqlite3_tokenizer *tokenizer = NULL;
	fts_get(&fts, &tokenizer);

	sqlite3_tokenizer_cursor *tcur = NULL;
	int rc = fts->xOpen(tokenizer, term, strlen(term), &tcur);
	assert(SQLITE_OK == rc);

	for(;;) {
		strarg_t token;
		int tlen;
		int ignored1, ignored2, ignored3;
		rc = fts->xNext(tcur, &token, &tlen, &ignored1, &ignored2, &ignored3);
		if(SQLITE_OK != rc) break;
		if(count+1 > asize) {
			asize = MAX(8, asize*2);
			tokens = realloc(tokens, sizeof(tokens[0]) * asize);
			assert(tokens); // TODO
		}
		tokens[count++].str = strndup(token, tlen);
	}

	fts->xClose(tcur);
	return 0;
}

- (uint64_t)seekMeta:(int const)dir :(uint64_t const)sortID {
	DB_RANGE(range, DB_VARINT_MAX + DB_INLINE_MAX);
	db_bind_uint64(range->min, EFSTermMetaFileIDAndPosition);
	db_bind_string(curtxn, range->min, tokens[0].str);
	db_range_genmax(range);
	DB_VAL(sortID_key, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1);
	db_bind_uint64(sortID_key, EFSTermMetaFileIDAndPosition);
	db_bind_string(curtxn, sortID_key, tokens[0].str);
	db_bind_uint64(sortID_key, sortID); // TODO: In order to handle seeking backwards over document with several matching positions, we need to use sortID+1... But sortID might be UINT64_MAX, so be careful.
	int rc = db_cursor_seekr(metafiles, range, sortID_key, NULL, dir);
	if(DB_SUCCESS != rc) return invalid(dir);
	uint64_t const table = db_read_uint64(sortID_key);
	assert(EFSTermMetaFileIDAndPosition == table);
	strarg_t const token = db_read_string(curtxn, sortID_key);
	assert(0 == strcmp(tokens[0].str, token));
	return db_read_uint64(sortID_key);
}
- (uint64_t)currentMeta:(int const)dir {
	DB_val sortID_key[1];
	int rc = db_cursor_current(metafiles, sortID_key, NULL);
	if(DB_SUCCESS != rc) return invalid(dir);
	uint64_t const table = db_read_uint64(sortID_key);
	assert(EFSTermMetaFileIDAndPosition == table);
	strarg_t const token = db_read_string(curtxn, sortID_key);
	assert(0 == strcmp(tokens[0].str, token));
	return db_read_uint64(sortID_key);
}
- (uint64_t)stepMeta:(int const)dir {
	DB_RANGE(range, DB_VARINT_MAX + DB_INLINE_MAX);
	db_bind_uint64(range->min, EFSTermMetaFileIDAndPosition);
	db_bind_string(curtxn, range->min, tokens[0].str);
	db_range_genmax(range);
	DB_val sortID_key[1];
	int rc = db_cursor_nextr(metafiles, range, sortID_key, NULL, dir);
	if(DB_SUCCESS != rc) return invalid(dir);
	uint64_t const table = db_read_uint64(sortID_key);
	assert(EFSTermMetaFileIDAndPosition == table);
	strarg_t const token = db_read_string(curtxn, sortID_key);
	assert(0 == strcmp(tokens[0].str, token));
	return db_read_uint64(sortID_key);
}
- (bool_t)match:(uint64_t const)metaFileID {
	DB_RANGE(range, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1);
	db_bind_uint64(range->min, EFSTermMetaFileIDAndPosition);
	db_bind_string(curtxn, range->min, tokens[0].str);
	db_bind_uint64(range->min, metaFileID);
	db_range_genmax(range);
	DB_val sortID_key[1];
	int rc = db_cursor_firstr(match, range, sortID_key, NULL, +1);
	if(DB_SUCCESS == rc) return true;
	if(DB_NOTFOUND == rc) return false;
	assertf(0, "Database error %s", db_strerror(rc));
}
@end

@implementation EFSMetadataFilter
- (void)free {
	FREE(&field);
	FREE(&value);
	db_cursor_close(metafiles); metafiles = NULL;
	db_cursor_close(match); match = NULL;
	[super free];
}

- (EFSFilterType)type {
	return EFSMetadataFilterType;
}
- (strarg_t)stringArg:(index_t const)i {
	switch(i) {
		case 0: return field;
		case 1: return value;
		default: return NULL;
	}
}
- (err_t)addStringArg:(strarg_t const)str :(size_t const)len {
	if(!field) {
		field = strndup(str, len);
		return 0;
	}
	if(!value) {
		value = strndup(str, len);
		return 0;
	}
	return -1;
}
- (void)print:(count_t const)depth {
	indent(depth);
	fprintf(stderr, "(metadata \"%s\" \"%s\")\n", field, value);
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth {
	size_t len = 0;
	len += wr_quoted(data+len, size-len, field);
	len += wr(data+len, size-len, "=");
	len += wr_quoted(data+len, size-len, value);
	return len;
}

- (err_t)prepare:(DB_txn *const)txn :(EFSConnection const *const)conn {
	if([super prepare:txn :conn] < 0) return -1;
	if(!field || !value) return -1;
	db_cursor_renew(txn, &metafiles); // EFSFieldValueAndMetaFileID
	db_cursor_renew(txn, &match); // EFSFieldValueAndMetaFileID
	curtxn = txn;
	return 0;
}

- (uint64_t)seekMeta:(int const)dir :(uint64_t const)sortID {
	DB_RANGE(range, DB_VARINT_MAX * 1 + DB_INLINE_MAX * 2);
	db_bind_uint64(range->min, EFSFieldValueAndMetaFileID);
	db_bind_string(curtxn, range->min, field);
	db_bind_string(curtxn, range->min, value);
	db_range_genmax(range);
	DB_VAL(metadata_key, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 2);
	db_bind_uint64(metadata_key, EFSFieldValueAndMetaFileID);
	db_bind_string(curtxn, metadata_key, field);
	db_bind_string(curtxn, metadata_key, value);
	db_bind_uint64(metadata_key, sortID);
	int rc = db_cursor_seekr(metafiles, range, metadata_key, NULL, dir);
	if(DB_SUCCESS != rc) return invalid(dir);
	uint64_t const table = db_read_uint64(metadata_key);
	assert(EFSFieldValueAndMetaFileID == table);
	strarg_t const f = db_read_string(curtxn, metadata_key);
	assert(field == f);
	strarg_t const v = db_read_string(curtxn, metadata_key);
	assert(value == v);
	return db_read_uint64(metadata_key);
}
- (uint64_t)currentMeta:(int const)dir {
	DB_val metadata_key[1];
	int rc = db_cursor_current(metafiles, metadata_key, NULL);
	if(DB_SUCCESS != rc) return invalid(dir);
	uint64_t const table = db_read_uint64(metadata_key);
	assert(EFSFieldValueAndMetaFileID == table);
	strarg_t const f = db_read_string(curtxn, metadata_key);
	assert(field == f);
	strarg_t const v = db_read_string(curtxn, metadata_key);
	assert(value == v);
	return db_read_uint64(metadata_key);
}
- (uint64_t)stepMeta:(int const)dir {
	DB_RANGE(range, DB_VARINT_MAX * 1 + DB_INLINE_MAX * 2);
	db_bind_uint64(range->min, EFSFieldValueAndMetaFileID);
	db_bind_string(curtxn, range->min, field);
	db_bind_string(curtxn, range->min, value);
	db_range_genmax(range);
	DB_val metadata_key[1];
	int rc = db_cursor_nextr(metafiles, range, metadata_key, NULL, dir);
	if(DB_SUCCESS != rc) return invalid(dir);
	uint64_t const table = db_read_uint64(metadata_key);
	assert(EFSFieldValueAndMetaFileID == table);
	strarg_t const f = db_read_string(curtxn, metadata_key);
	assert(field == f);
	strarg_t const v = db_read_string(curtxn, metadata_key);
	assert(value == v);
	return db_read_uint64(metadata_key);
}
- (bool_t)match:(uint64_t const)metaFileID {
	DB_VAL(metadata_key, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 2);
	db_bind_uint64(metadata_key, EFSFieldValueAndMetaFileID);
	db_bind_string(curtxn, metadata_key, field);
	db_bind_string(curtxn, metadata_key, value);
	db_bind_uint64(metadata_key, metaFileID);
	int rc = db_cursor_seek(match, metadata_key, NULL, 0);
	if(DB_SUCCESS == rc) return true;
	if(DB_NOTFOUND == rc) return false;
	assertf(0, "Database error %s", db_strerror(rc));
}
@end

