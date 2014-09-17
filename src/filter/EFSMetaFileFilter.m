#include "EFSFilter.h"

@interface EFSMetaFileFilterInternal : EFSFilter
{
	EFSFilter *subfilter; // weak ref
	MDB_cursor *metafiles;
	MDB_cursor *age_metafile;
	MDB_cursor *age_uri;
	MDB_cursor *age_files;
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

- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn {
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
	mdb_cursor_close(metafiles); metafiles = NULL;
	mdb_cursor_close(age_metafile); age_metafile = NULL;
	mdb_cursor_close(age_uri); age_uri = NULL;
	mdb_cursor_close(age_files); age_files = NULL;
	[super free];
}

- (err_t)addFilterArg:(EFSFilter *const)filter {
	if(subfilter) return -1;
	subfilter = filter;
	return 0;
}

- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn {
	assert(subfilter);
	if([super prepare:txn :conn] < 0) return -1;
	db_cursor(txn, conn->metaFileByID, &metafiles);
	db_cursor(txn, conn->metaFileIDByFileID, &age_metafile);
	db_cursor(txn, conn->metaFileByID, &age_uri);
	db_cursor(txn, conn->fileIDByURI, &age_files);
	return 0;
}

- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	DB_VAL(metaFileID_val, 1);
	db_bind(metaFileID_val, sortID);
	MDB_val metaFile_val[1];
	int rc = db_cursor_get(metafiles, metaFileID_val, metaFile_val, MDB_SET_RANGE);
	uint64_t actualSortID, actualFileID;
	if(MDB_SUCCESS == rc) {
		actualSortID = db_column(metaFileID_val, 0);
		actualFileID = db_column(metaFile_val, 0);
	} else {
		actualSortID = UINT64_MAX;
		actualFileID = UINT64_MAX;
	}
	if(sortID == actualSortID && fileID == actualFileID) {
		// Do nothing
	} else {
		if(dir > 0) [self step:-1];
	}
}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	MDB_val metaFileID_val[1], metaFile_val[1];
	int rc = db_cursor_get(metafiles, metaFileID_val, metaFile_val, MDB_GET_CURRENT);
	if(MDB_SUCCESS == rc) {
		if(sortID) *sortID = db_column(metaFileID_val, 0);
		if(fileID) *fileID = db_column(metaFile_val, 0);
	} else {
		if(sortID) *sortID = invalid(dir);
		if(fileID) *fileID = invalid(dir);
	}
}
- (void)step:(int const)dir {
	int rc = db_cursor_get(metafiles, NULL, NULL, op(dir, MDB_NEXT));
	assert(MDB_SUCCESS == rc || MDB_NOTFOUND == rc);
	UNUSED(rc);
}
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID {
	assert(subfilter);
	DB_VAL(fileID_val, 1);
	db_bind(fileID_val, fileID);
	MDB_val metaFileID_val[1];
	int rc = mdb_cursor_get(age_metafile, fileID_val, metaFileID_val, MDB_SET);
	if(MDB_SUCCESS != rc) return UINT64_MAX;
	uint64_t const metaFileID = db_column(metaFileID_val, 0);
	MDB_val metaFile_val[1];
	rc = mdb_cursor_get(age_uri, metaFileID_val, metaFile_val, MDB_SET);
	if(MDB_SUCCESS != rc) return UINT64_MAX;
	uint64_t const targetURI_id = db_column(metaFile_val, 1);

	DB_VAL(targetURI_val, 1);
	db_bind(targetURI_val, targetURI_id);
	MDB_val targetFileID_val[1];
	rc = mdb_cursor_get(age_files, targetURI_val, targetFileID_val, MDB_SET);
	for(; MDB_SUCCESS == rc; rc = mdb_cursor_get(age_files, targetURI_val, targetFileID_val, MDB_NEXT_DUP)) {
		uint64_t const targetFileID = db_column(targetFileID_val, 0);
		if([subfilter age:UINT64_MAX :targetFileID] < UINT64_MAX) return metaFileID;
	}
	return UINT64_MAX;
}
@end

