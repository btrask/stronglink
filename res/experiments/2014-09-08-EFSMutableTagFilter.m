@interface EFSMutableTagFilter : EFSIndividualFilter
{
	str_t *tag;
	size_t len;
	uint64_t tag_id;
	MDB_txn *cur_txn;
	db_schema *cur_schema;
	MDB_cursor *metafiles;
	MDB_cursor *match;
}
- (uint64_t)tag_id:(unsigned const)depth;
@end

@implementation EFSMutableTagFilter
- (void)free {
	FREE(&tag);
	len = 0;
	tag_id = 0;
	cur_txn = NULL;
	cur_schema = NULL;
	mdb_cursor_close(metafiles); metafiles = NULL;
	mdb_cursor_close(recursion); recursion = NULL;
	mdb_cursor_close(match); match = NULL;
	[super free];
}

- (EFSFilterType)type {
	return EFSMutableTagFilterType;
}
- (strarg_t)stringArg:(index_t const)i {
	if(0 == i) return tag;
	return NULL;
}
- (err_t)addStringArg:(strarg_t const)str :(size_t const)slen {
	if(tag) return -1;
	tag = malloc(slen+8);
	if(!tag) return -1;
	len = slen;
	memcpy(tag, str, slen);
	tag[slen] = '\0';
	return 0;
}
- (void)print:(count_t const)depth {
	indent(depth);
	fprintf(stderr, "(tag %s)\n", tag);
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth {
	size_t len = 0;
	len += wr(data+len, size-len, "tag=");
	len += wr_quoted(data+len, size-len, tag);
	return len;
}

- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn {
	if(!tag) return -1;
	if([super prepare:txn :conn] < 0) return -1;
	cur_txn = txn;
	cur_schema = conn->schema;
	if(!tag_id) tag_id = [self tag_id:0];
	db_cursor(txn, conn->metaFileIDByMetadata, &metafiles);
	db_cursor(txn, conn->metaFileIDByMetadata, &match);
	return 0;
}

- (uint64_t)seekMeta:(int const)dir :(uint64_t const)sortID {
	DB_VAL(metadata_val, 2);
	db_bind(metadata_val, tag_id);
	db_bind(metadata_val, field_id);
	DB_VAL(sortID_val, 1);
	db_bind(sortID_val, sortID);
	int rc = db_cursor_get(metafiles, metadata_val, sortID_val, MDB_GET_BOTH_RANGE);
	if(MDB_SUCCESS != rc) return invalid(dir);
	uint64_t const actual = db_column(sortID_val, 0);
	if(sortID != actual && dir > 0) return [self stepMeta:-1];
	return actual;
}
- (uint64_t)currentMeta:(int const)dir;
- (uint64_t)stepMeta:(int const)dir;
- (bool_t)match:(uint64_t const)metaFileID;



- (uint64_t)tag_id:(unsigned const)depth {
	if(depth) snprintf(tag+len, 8, "~%u", depth);
	uint64_t const x = db_string_id(cur_txn, cur_schema, tag);
	tag[len] = '\0';
	return x;
}
- (bool_t)deleted:(uint64_t const)fileID {
	for(unsigned depth = 1; ; ++depth) {
		uint64_t const x = [self tag_id:depth];
		if(![self hasTag:fileID :x]) return depth % 2;
	}
}
- (bool_t)age d:(uint64_t const)fileID :(uint64_t const)tag_id {
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


