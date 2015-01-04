#include "EFSFilter.h"

@interface EFSMetaFileFilterInternal : EFSFilter
{
	DB_txn *curtxn;
	EFSFilter *subfilter; // weak ref
	DB_cursor *metafiles;
	DB_cursor *age_metafile;
	DB_cursor *age_uri;
	DB_cursor *age_files;
}
@end

@implementation EFSMetaFileFilter
- (id)init {
	if(!(self = [super init])) return nil;
	main = [[EFSUnionFilter alloc] init];
	internal = [[EFSMetaFileFilterInternal alloc] init];
	if(!main || !internal) {
		[internal free]; internal = nil;
		[self free];
		return nil;
	}
	err_t rc = [main addFilterArg:internal];
	assert(rc >= 0);
	UNUSED(rc);
	return self;
}
- (void)free {
	[main free]; main = nil;
	internal = nil;
	subfilter = nil;
	[super free];
}

- (EFSFilterType)type {
	return EFSMetaFileFilterType;
}
- (EFSFilter *)unwrap {
	return [subfilter unwrap];
}
- (err_t)addFilterArg:(EFSFilter *const)filter {
	if([internal addFilterArg:filter] < 0) return -1;
	if([main addFilterArg:filter] < 0) return -1;
	subfilter = filter;
	return 0;
}
- (void)print:(count_t const)depth {
	indent(depth);
	fprintf(stderr, "(metafiles\n");
	[subfilter print:depth+1];
	indent(depth);
	fprintf(stderr, ")\n");
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth {
	assert(0 && "Meta-file filter has no user representation");
	return wr(data, size, "");
}

- (err_t)prepare:(DB_txn *const)txn :(EFSConnection const *const)conn {
	assert(subfilter);
	if([super prepare:txn :conn] < 0) return -1;
	if([main prepare:txn :conn] < 0) return -1;
	return 0;
}

- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	return [main seek:dir :sortID :fileID];
}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	return [main current:dir :sortID :fileID];
}
- (void)step:(int const)dir {
	return [main step:dir];
}
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID {
	return [main age:sortID :fileID];
}
@end

@implementation EFSMetaFileFilterInternal
- (void)free {
	subfilter = nil;
	db_cursor_close(metafiles); metafiles = NULL;
	db_cursor_close(age_metafile); age_metafile = NULL;
	db_cursor_close(age_uri); age_uri = NULL;
	db_cursor_close(age_files); age_files = NULL;
	[super free];
}

- (err_t)addFilterArg:(EFSFilter *const)filter {
	if(subfilter) return -1;
	subfilter = filter;
	return 0;
}

- (err_t)prepare:(DB_txn *const)txn :(EFSConnection const *const)conn {
	assert(subfilter);
	if([super prepare:txn :conn] < 0) return -1;
	db_cursor_renew(txn, &metafiles); // EFSMetaFileByID
	db_cursor_renew(txn, &age_metafile); // EFSFileIDAndMetaFileID
	db_cursor_renew(txn, &age_uri); // EFSMetaFileByID
	db_cursor_renew(txn, &age_files);
	curtxn = txn;
	return 0;
}

- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	DB_RANGE(range, DB_VARINT_MAX);
	db_bind_uint64(range->min, EFSMetaFileByID);
	db_range_genmax(range);
	DB_VAL(metaFileID_key, DB_VARINT_MAX + DB_VARINT_MAX);
	db_bind_uint64(metaFileID_key, EFSMetaFileByID);
	db_bind_uint64(metaFileID_key, sortID);
	DB_val metaFile_val[1];
	int rc = db_cursor_seekr(metafiles, range, metaFileID_key, metaFile_val, dir);
	assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));
}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	DB_val metaFileID_key[1], metaFile_val[1];
	int rc = db_cursor_current(metafiles, metaFileID_key, metaFile_val);
	if(DB_SUCCESS == rc) {
		uint64_t const table = db_read_uint64(metaFileID_key);
		assert(EFSMetaFileByID == table);
		if(sortID) *sortID = db_read_uint64(metaFileID_key);
		if(fileID) *fileID = db_read_uint64(metaFile_val);
	} else {
		if(sortID) *sortID = invalid(dir);
		if(fileID) *fileID = invalid(dir);
	}
}
- (void)step:(int const)dir {
	DB_RANGE(range, DB_VARINT_MAX);
	db_bind_uint64(range->min, EFSMetaFileByID);
	db_range_genmax(range);
	DB_val ignore[1];
	int rc = db_cursor_nextr(metafiles, range, ignore, NULL, dir);
	assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));
}
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID {
	assert(subfilter);

	DB_RANGE(metaFileIDs, DB_VARINT_MAX + DB_VARINT_MAX);
	db_bind_uint64(metaFileIDs->min, EFSFileIDAndMetaFileID);
	db_bind_uint64(metaFileIDs->min, fileID);
	db_range_genmax(metaFileIDs);
	DB_val fileIDAndMetaFileID_key[1];
	int rc = db_cursor_firstr(age_metafile, metaFileIDs, fileIDAndMetaFileID_key, NULL, +1);
	if(DB_SUCCESS != rc) return UINT64_MAX;
	uint64_t const table = db_read_uint64(fileIDAndMetaFileID_key);
	assert(EFSFileIDAndMetaFileID == table);
	uint64_t const f = db_read_uint64(fileIDAndMetaFileID_key);
	assert(fileID == f);
	uint64_t const metaFileID = db_read_uint64(fileIDAndMetaFileID_key);

	DB_VAL(metaFileID_key, DB_VARINT_MAX + DB_VARINT_MAX);
	db_bind_uint64(metaFileID_key, EFSMetaFileByID);
	db_bind_uint64(metaFileID_key, metaFileID);
	DB_val metaFile_val[1];
	rc = db_cursor_seek(age_uri, metaFileID_key, metaFile_val, 0);
	if(DB_SUCCESS != rc) return UINT64_MAX;
	uint64_t const f2 = db_read_uint64(metaFile_val);
	assert(fileID == f2);
	strarg_t const targetURI = db_read_string(curtxn, metaFile_val);

	DB_RANGE(targetFileIDs, DB_VARINT_MAX + DB_INLINE_MAX);
	db_bind_uint64(targetFileIDs->min, EFSURIAndFileID);
	db_bind_string(curtxn, targetFileIDs->min, targetURI);
	db_range_genmax(targetFileIDs);
	DB_val targetFileID_key[1];
	rc = db_cursor_firstr(age_files, targetFileIDs, targetFileID_key, NULL, +1);
	assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));
	for(; DB_SUCCESS == rc; rc = db_cursor_nextr(age_files, targetFileIDs, targetFileID_key, NULL, +1)) {
		uint64_t const table3 = db_read_uint64(targetFileID_key);
		assert(EFSURIAndFileID == table3);
		strarg_t const u = db_read_string(curtxn, targetFileID_key);
		assert(0 == strcmp(targetURI, u));
		uint64_t const targetFileID = db_read_uint64(targetFileID_key);
		if([subfilter age:UINT64_MAX :targetFileID] < UINT64_MAX) return metaFileID;
	}
	return UINT64_MAX;
}
@end

