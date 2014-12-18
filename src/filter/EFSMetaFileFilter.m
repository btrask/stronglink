#include "EFSFilter.h"

@interface EFSMetaFileFilterInternal : EFSFilter
{
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
	return 0;
}

- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	DB_RANGE(range, 1);
	db_bind(range->min, EFSMetaFileByID);
	db_bind(range->max, EFSMetaFileByID+1);
	DB_VAL(metaFileID_key, 2);
	db_bind(metaFileID_key, EFSMetaFileByID);
	db_bind(metaFileID_key, sortID);
	DB_val metaFile_val[1];
	int rc = db_cursor_seekr(metafiles, range, metaFileID_key, metaFile_val, dir);
	assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));
}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	DB_val metaFileID_key[1], metaFile_val[1];
	int rc = db_cursor_current(metafiles, metaFileID_key, metaFile_val);
	if(DB_SUCCESS == rc) {
		if(sortID) *sortID = db_column(metaFileID_key, 1);
		if(fileID) *fileID = db_column(metaFile_val, 0);
	} else {
		if(sortID) *sortID = invalid(dir);
		if(fileID) *fileID = invalid(dir);
	}
}
- (void)step:(int const)dir {
	DB_RANGE(range, 1);
	db_bind(range->min, EFSMetaFileByID);
	db_bind(range->max, EFSMetaFileByID+1);
	DB_val ignore[1];
	int rc = db_cursor_nextr(metafiles, range, ignore, NULL, dir);
	assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));
}
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID {
	assert(subfilter);

	fprintf(stderr, "meta-file age of %lu\n", fileID); // TODO: We're still missing the actual meta-files we're trying to include.

	DB_RANGE(metaFileIDs, 2);
	db_bind(metaFileIDs->min, EFSFileIDAndMetaFileID);
	db_bind(metaFileIDs->max, EFSFileIDAndMetaFileID);
	db_bind(metaFileIDs->min, fileID+0);
	db_bind(metaFileIDs->max, fileID+1);
	DB_val fileIDAndMetaFileID_key[1];
	int rc = db_cursor_firstr(age_metafile, metaFileIDs, fileIDAndMetaFileID_key, NULL, +1);
	if(DB_SUCCESS != rc) return UINT64_MAX;
	uint64_t const metaFileID = db_column(fileIDAndMetaFileID_key, 2);

	DB_VAL(metaFileID_key, 2);
	db_bind(metaFileID_key, EFSMetaFileByID);
	db_bind(metaFileID_key, metaFileID);
	DB_val metaFile_val[1];
	rc = db_cursor_seek(age_uri, metaFileID_key, metaFile_val, 0);
	if(DB_SUCCESS != rc) return UINT64_MAX;
	uint64_t const targetURI_id = db_column(metaFile_val, 1);

	DB_RANGE(targetFileIDs, 2);
	db_bind(targetFileIDs->min, EFSURIAndFileID);
	db_bind(targetFileIDs->max, EFSURIAndFileID);
	db_bind(targetFileIDs->min, targetURI_id+0);
	db_bind(targetFileIDs->max, targetURI_id+1);
	DB_val targetFileID_key[1];
	rc = db_cursor_firstr(age_files, targetFileIDs, targetFileID_key, NULL, +1);
	assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));
	for(; DB_SUCCESS == rc; rc = db_cursor_nextr(age_files, targetFileIDs, targetFileID_key, NULL, +1)) {
		uint64_t const targetFileID = db_column(targetFileID_key, 2);
		if([subfilter age:UINT64_MAX :targetFileID] < UINT64_MAX) return metaFileID;
	}
	return UINT64_MAX;
}
@end

