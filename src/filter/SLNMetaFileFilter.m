#include "SLNFilter.h"

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
	int rc = [main addFilterArg:internal];
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
- (int)addFilterArg:(EFSFilter *const)filter {
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

- (int)prepare:(DB_txn *const)txn {
	assert(subfilter);
	if([super prepare:txn] < 0) return -1;
	if([main prepare:txn] < 0) return -1;
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

- (int)addFilterArg:(EFSFilter *const)filter {
	if(subfilter) return -1;
	subfilter = filter;
	return 0;
}

- (int)prepare:(DB_txn *const)txn {
	assert(subfilter);
	if([super prepare:txn] < 0) return -1;
	db_cursor_renew(txn, &metafiles); // EFSMetaFileByID
	db_cursor_renew(txn, &age_metafile); // EFSFileIDAndMetaFileID
	db_cursor_renew(txn, &age_uri); // EFSMetaFileByID
	db_cursor_renew(txn, &age_files);
	curtxn = txn;
	return 0;
}

- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	DB_range range[1];
	EFSMetaFileByIDRange0(range, curtxn);
	DB_val metaFileID_key[1];
	EFSMetaFileByIDKeyPack(metaFileID_key, curtxn, sortID);
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
	DB_range range[1];
	EFSMetaFileByIDRange0(range, curtxn);
	DB_val ignore[1];
	int rc = db_cursor_nextr(metafiles, range, ignore, NULL, dir);
	assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));
}
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID {
	assert(subfilter);

	DB_range metaFileIDs[1];
	EFSFileIDAndMetaFileIDRange1(metaFileIDs, curtxn, fileID);
	DB_val fileIDAndMetaFileID_key[1];
	int rc = db_cursor_firstr(age_metafile, metaFileIDs, fileIDAndMetaFileID_key, NULL, +1);
	if(DB_SUCCESS != rc) return UINT64_MAX;
	uint64_t f;
	uint64_t metaFileID;
	EFSFileIDAndMetaFileIDKeyUnpack(fileIDAndMetaFileID_key, curtxn, &f, &metaFileID);
	assert(fileID == f);

	DB_val metaFileID_key[1];
	EFSMetaFileByIDKeyPack(metaFileID_key, curtxn, metaFileID);
	DB_val metaFile_val[1];
	rc = db_cursor_seek(age_uri, metaFileID_key, metaFile_val, 0);
	if(DB_SUCCESS != rc) return UINT64_MAX;
	uint64_t const f2 = db_read_uint64(metaFile_val);
	assert(fileID == f2);
	strarg_t const targetURI = db_read_string(metaFile_val, curtxn);

	DB_range targetFileIDs[1];
	EFSURIAndFileIDRange1(targetFileIDs, curtxn, targetURI);
	DB_val targetFileID_key[1];
	rc = db_cursor_firstr(age_files, targetFileIDs, targetFileID_key, NULL, +1);
	assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));
	for(; DB_SUCCESS == rc; rc = db_cursor_nextr(age_files, targetFileIDs, targetFileID_key, NULL, +1)) {
		strarg_t u;
		uint64_t targetFileID;
		EFSURIAndFileIDKeyUnpack(targetFileID_key, curtxn, &u, &targetFileID);
		assert(0 == strcmp(targetURI, u));
		if([subfilter age:UINT64_MAX :targetFileID] < UINT64_MAX) return metaFileID;
	}
	return UINT64_MAX;
}
@end

