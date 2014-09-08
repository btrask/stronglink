#include "EFSFilter.h"
#include "../fts.h"

@implementation EFSIndividualFilter
- (void)free {
	mdb_cursor_close(step_target); step_target = NULL;
	mdb_cursor_close(step_files); step_files = NULL;
	mdb_cursor_close(age_uris); age_uris = NULL;
	mdb_cursor_close(age_metafiles); age_metafiles = NULL;
	[super free];
}

- (EFSFilter *)unwrap {
	return self;
}

- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn {
	if([super prepare:txn :conn] < 0) return -1;
	db_cursor(txn, conn->metaFileByID, &step_target);
	db_cursor(txn, conn->fileIDByURI, &step_files);
	db_cursor(txn, conn->URIByFileID, &age_uris);
	db_cursor(txn, conn->metaFileIDByTargetURI, &age_metafiles);
	return 0;
}
- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	int rc;
	uint64_t const actualSortID = [self seekMeta:dir :sortID];
	if(!valid(actualSortID)) return;
	DB_VAL(metaFileID_val, 1);
	db_bind(metaFileID_val, actualSortID);
	MDB_val metaFile_val[1];
	rc = mdb_cursor_get(step_target, metaFileID_val, metaFile_val, MDB_SET);
	assertf(MDB_SUCCESS == rc, "Database error %s", mdb_strerror(rc));
	uint64_t const targetURI_id = db_column(metaFile_val, 1);
	DB_VAL(targetURI_val, 1);
	db_bind(targetURI_val, targetURI_id);
	if(sortID == actualSortID) {
		DB_VAL(fileID_val, 1);
		db_bind(fileID_val, fileID);
		rc = mdb_cursor_get(step_files, targetURI_val, fileID_val, MDB_GET_BOTH_RANGE);
		if(MDB_SUCCESS != rc) return;
		uint64_t const actualFileID = db_column(fileID_val, 0);
		if(fileID == actualFileID) return (void)[self step:-dir];
		if(dir > 0) return (void)[self step:-1];
	} else {
		rc = db_cursor_get(step_files, targetURI_val, NULL, MDB_SET);
		assertf(MDB_SUCCESS == rc || MDB_NOTFOUND == rc, "Database error %s", mdb_strerror(rc));
	}
}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	MDB_val fileID_val[1];
	int rc = db_cursor_get(step_files, NULL, fileID_val, MDB_GET_CURRENT);
	if(MDB_SUCCESS == rc) {
		if(sortID) *sortID = [self currentMeta:dir];
		if(fileID) *fileID = db_column(fileID_val, 0);
	} else {
		if(sortID) *sortID = invalid(dir);
		if(fileID) *fileID = invalid(dir);
	}
}
- (bool_t)step:(int const)dir {
	int rc;
	MDB_val fileID_val[1];

	rc = db_cursor_get(step_files, NULL, fileID_val, op(dir, MDB_NEXT_DUP));
	if(MDB_SUCCESS == rc) return true;

	for(uint64_t sortID = [self stepMeta:dir]; valid(sortID); sortID = [self stepMeta:dir]) {
		DB_VAL(metaFileID_val, 1);
		db_bind(metaFileID_val, sortID);
		MDB_val metaFile_val[1];
		rc = mdb_cursor_get(step_target, metaFileID_val, metaFile_val, MDB_SET);
		assertf(MDB_SUCCESS == rc, "Database error %s", mdb_strerror(rc));
		uint64_t const targetURI_id = db_column(metaFile_val, 1);
		DB_VAL(targetURI_val, 1);
		db_bind(targetURI_val, targetURI_id);
		rc = mdb_cursor_get(step_files, targetURI_val, NULL, MDB_SET);
		if(MDB_SUCCESS != rc) continue;
		rc = db_cursor_get(step_files, NULL, fileID_val, op(dir, MDB_FIRST_DUP));
		if(MDB_SUCCESS != rc) continue;
		return true;
	}
	return false;
}
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID {
	uint64_t earliest = UINT64_MAX;
	int rc;

	DB_VAL(fileID_val, 1);
	db_bind(fileID_val, fileID);
	MDB_val URI_val[1];
	rc = mdb_cursor_get(age_uris, fileID_val, URI_val, MDB_SET);
	assert(MDB_SUCCESS == rc || MDB_NOTFOUND == rc);
	for(; MDB_SUCCESS == rc; rc = mdb_cursor_get(age_uris, fileID_val, URI_val, MDB_NEXT_DUP)) {
		uint64_t const targetURI_id = db_column(URI_val, 0);

		DB_VAL(targetURI_val, 1);
		db_bind(targetURI_val, targetURI_id);
		MDB_val metaFileID_val[1];
		rc = mdb_cursor_get(age_metafiles, targetURI_val, metaFileID_val, MDB_SET);
		assert(MDB_SUCCESS == rc || MDB_NOTFOUND == rc);
		for(; MDB_SUCCESS == rc; rc = mdb_cursor_get(age_metafiles, targetURI_val, metaFileID_val, MDB_NEXT_DUP)) {
			uint64_t const metaFileID = db_column(metaFileID_val, 0);
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
	mdb_cursor_close(metafiles); metafiles = NULL;
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

- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn {
	if([super prepare:txn :conn] < 0) return -1;
	db_cursor(txn, conn->metaFileByID, &metafiles);
	return 0;
}

- (uint64_t)seekMeta:(int const)dir :(uint64_t const)sortID {
	DB_VAL(sortID_val, 1);
	db_bind(sortID_val, sortID);
	int rc = db_cursor_get(metafiles, sortID_val, NULL, MDB_SET_RANGE);
	if(MDB_SUCCESS != rc) return invalid(dir);
	uint64_t const actual = db_column(sortID_val, 0);
	if(sortID != actual && dir > 0) return [self stepMeta:-1];
	return actual;
}
- (uint64_t)currentMeta:(int const)dir {
	MDB_val metaFileID_val[1];
	int rc = mdb_cursor_get(metafiles, metaFileID_val, NULL, MDB_GET_CURRENT);
	if(MDB_SUCCESS != rc) return invalid(dir);
	return db_column(metaFileID_val, 0);
}
- (uint64_t)stepMeta:(int const)dir {
	MDB_val metaFileID_val[1];
	int rc = db_cursor_get(metafiles, metaFileID_val, NULL, op(dir, MDB_NEXT));
	if(MDB_SUCCESS == rc) return db_column(metaFileID_val, 0);
	return invalid(dir);
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
	mdb_cursor_close(metafiles); metafiles = NULL;
	mdb_cursor_close(match); match = NULL;
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
	assert(str);
	assert(len);

	if(term) return -1;
	term = strndup(str, len);

	// TODO: libstemmer?
	sqlite3_tokenizer_module const *fts = NULL;
	sqlite3_tokenizer *tokenizer = NULL;
	fts_get(&fts, &tokenizer);

	sqlite3_tokenizer_cursor *tcur = NULL;
	int rc = fts->xOpen(tokenizer, str, len, &tcur);
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
		tokens[count].str = strndup(token, tlen);
		tokens[count].len = tlen;
		count++;
	}

	fts->xClose(tcur);
	return 0;
}
- (void)print:(count_t const)depth {
	indent(depth);
	fprintf(stderr, "(fulltext %s)\n", term);
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth {
	return wr_quoted(data, size, term);
}

- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn {
	if([super prepare:txn :conn] < 0) return -1;
	db_cursor(txn, conn->metaFileIDByFulltext, &metafiles);
	db_cursor(txn, conn->metaFileIDByFulltext, &match);
	return 0;
}

- (uint64_t)seekMeta:(int const)dir :(uint64_t const)sortID {
	MDB_val token_val = { tokens[0].len, tokens[0].str };
	DB_VAL(sortID_val, 1);
	db_bind(sortID_val, sortID);
	int rc = db_cursor_get(metafiles, &token_val, sortID_val, MDB_GET_BOTH_RANGE);
	if(MDB_SUCCESS != rc) return invalid(dir);
	uint64_t const actual = db_column(sortID_val, 0);
	if(sortID != actual && dir > 0) return [self stepMeta:-1];
	return actual;
}
- (uint64_t)currentMeta:(int const)dir {
	MDB_val metaFileID_val[1];
	int rc = db_cursor_get(metafiles, NULL, metaFileID_val, MDB_GET_CURRENT);
	if(MDB_SUCCESS != rc) return invalid(dir);
	return db_column(metaFileID_val, 0);
}
- (uint64_t)stepMeta:(int const)dir {
	int rc;
	MDB_val token_val = { tokens[0].len, tokens[0].str };
	MDB_val metaFileID_val[1];
	rc = db_cursor_get(metafiles, &token_val, metaFileID_val, op(dir, MDB_NEXT_DUP));
	if(MDB_SUCCESS != rc) return invalid(dir);
	return db_column(metaFileID_val, 0);
}
- (bool_t)match:(uint64_t const)metaFileID {
	MDB_val token_val = { tokens[0].len, tokens[0].str };
	DB_VAL(metaFileID_val, 1);
	db_bind(metaFileID_val, metaFileID);
	int rc = mdb_cursor_get(match, &token_val, metaFileID_val, MDB_GET_BOTH);
	if(MDB_SUCCESS == rc) return true;
	if(MDB_NOTFOUND == rc) return false;
	assertf(0, "Database error %s", mdb_strerror(rc));
}
@end

@implementation EFSMetadataFilter
- (void)free {
	FREE(&field);
	FREE(&value);
	field_id = 0;
	value_id = 0;
	mdb_cursor_close(metafiles); metafiles = NULL;
	mdb_cursor_close(match); match = NULL;
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

- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn {
	if([super prepare:txn :conn] < 0) return -1;
	if(!field || !value) return -1;
	if(!field_id) field_id = db_string_id(txn, conn->schema, field);
	if(!value_id) value_id = db_string_id(txn, conn->schema, value);
	db_cursor(txn, conn->metaFileIDByMetadata, &metafiles);
	db_cursor(txn, conn->metaFileIDByMetadata, &match);
	return 0;
}

- (uint64_t)seekMeta:(int const)dir :(uint64_t const)sortID {
	DB_VAL(metadata_val, 2);
	db_bind(metadata_val, value_id);
	db_bind(metadata_val, field_id);
	DB_VAL(sortID_val, 1);
	db_bind(sortID_val, sortID);
	int rc = db_cursor_get(metafiles, metadata_val, sortID_val, MDB_GET_BOTH_RANGE);
	if(MDB_SUCCESS != rc) return invalid(dir);
	uint64_t const actual = db_column(sortID_val, 0);
	if(sortID != actual && dir > 0) return [self stepMeta:-1];
	return actual;
}
- (uint64_t)currentMeta:(int const)dir {
	MDB_val metaFileID_val[1];
	int rc = db_cursor_get(metafiles, NULL, metaFileID_val, MDB_GET_CURRENT);
	if(MDB_SUCCESS != rc) return invalid(dir);
	return db_column(metaFileID_val, 0);
}
- (uint64_t)stepMeta:(int const)dir {
	DB_VAL(metadata_val, 2);
	db_bind(metadata_val, value_id);
	db_bind(metadata_val, field_id);
	MDB_val metaFileID_val[1];
	int rc = db_cursor_get(metafiles, metadata_val, metaFileID_val, op(dir, MDB_NEXT_DUP));
	if(MDB_SUCCESS != rc) return invalid(dir);
	return db_column(metaFileID_val, 0);
}
- (bool_t)match:(uint64_t const)metaFileID {
	DB_VAL(metadata_val, 2);
	db_bind(metadata_val, value_id);
	db_bind(metadata_val, field_id);
	DB_VAL(metaFileID_val, 1);
	db_bind(metaFileID_val, metaFileID);
	int rc = mdb_cursor_get(match, metadata_val, metaFileID_val, MDB_GET_BOTH);
	if(MDB_SUCCESS == rc) return true;
	if(MDB_NOTFOUND == rc) return false;
	assertf(0, "Database error %s", mdb_strerror(rc));
}
@end

