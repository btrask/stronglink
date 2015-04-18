#include "SLNFilter.h"

// TODO: This file is deprecated.
// We want to get rid of it just as soon as we rewrite our sync system.

@interface SLNBadMetaFileFilterInternal : SLNFilter
{
	DB_txn *curtxn;
	SLNFilter *subfilter; // weak ref
	DB_cursor *metafiles;
	DB_cursor *age_metafile;
	DB_cursor *age_uri;
	DB_cursor *age_files;
}
@end

@implementation SLNBadMetaFileFilter
- (id)init {
	if(!(self = [super init])) return nil;
	main = [[SLNUnionFilter alloc] init];
	internal = [[SLNBadMetaFileFilterInternal alloc] init];
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

- (SLNFilterType)type {
	return SLNBadMetaFileFilterType;
}
- (SLNFilter *)unwrap {
	return [subfilter unwrap];
}
- (int)addFilterArg:(SLNFilter *const)filter {
	if([internal addFilterArg:filter] < 0) return -1;
	if([main addFilterArg:filter] < 0) return -1;
	subfilter = filter;
	return 0;
}
- (void)print:(count_t const)depth {
	indent(depth);
	fprintf(stderr, "(badmetafiles\n");
	[subfilter print:depth+1];
	indent(depth);
	fprintf(stderr, ")\n");
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth {
	assert(0);
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
- (uint64_t)fullAge:(uint64_t const)fileID {
	return [main fullAge:fileID];
}
- (uint64_t)fastAge:(uint64_t const)fileID :(uint64_t const)sortID {
	return [main fastAge:fileID :sortID];
}
@end

@implementation SLNBadMetaFileFilterInternal
- (void)free {
	subfilter = nil;
	db_cursor_close(metafiles); metafiles = NULL;
	db_cursor_close(age_metafile); age_metafile = NULL;
	db_cursor_close(age_uri); age_uri = NULL;
	db_cursor_close(age_files); age_files = NULL;
	[super free];
}

- (int)addFilterArg:(SLNFilter *const)filter {
	if(subfilter) return -1;
	subfilter = filter;
	return 0;
}

- (int)prepare:(DB_txn *const)txn {
	assert(subfilter);
	if([super prepare:txn] < 0) return -1;
	db_cursor_renew(txn, &metafiles); // SLNMetaFileByID
	db_cursor_renew(txn, &age_metafile); // SLNFileIDAndMetaFileID
	db_cursor_renew(txn, &age_uri); // SLNMetaFileByID
	db_cursor_renew(txn, &age_files);
	curtxn = txn;
	return 0;
}

- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	DB_range range[1];
	SLNMetaFileByIDRange0(range, curtxn);
	DB_val metaFileID_key[1];
	SLNMetaFileByIDKeyPack(metaFileID_key, curtxn, sortID);
	DB_val metaFile_val[1];
	int rc = db_cursor_seekr(metafiles, range, metaFileID_key, metaFile_val, dir);
	assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));
}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	DB_val metaFileID_key[1], metaFile_val[1];
	int rc = db_cursor_current(metafiles, metaFileID_key, metaFile_val);
	if(DB_SUCCESS == rc) {
		uint64_t const table = db_read_uint64(metaFileID_key);
		assert(SLNMetaFileByID == table);
		if(sortID) *sortID = db_read_uint64(metaFileID_key);
		if(fileID) *fileID = db_read_uint64(metaFile_val);
	} else {
		if(sortID) *sortID = invalid(dir);
		if(fileID) *fileID = invalid(dir);
	}
}
- (void)step:(int const)dir {
	DB_range range[1];
	SLNMetaFileByIDRange0(range, curtxn);
	DB_val ignore[1];
	int rc = db_cursor_nextr(metafiles, range, ignore, NULL, dir);
	assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));
}
- (uint64_t)fullAge:(uint64_t const)fileID {
	assert(subfilter);

	DB_range metaFileIDs[1];
	SLNFileIDAndMetaFileIDRange1(metaFileIDs, curtxn, fileID);
	DB_val fileIDAndMetaFileID_key[1];
	int rc = db_cursor_firstr(age_metafile, metaFileIDs, fileIDAndMetaFileID_key, NULL, +1);
	if(DB_SUCCESS != rc) return UINT64_MAX;
	uint64_t f;
	uint64_t metaFileID;
	SLNFileIDAndMetaFileIDKeyUnpack(fileIDAndMetaFileID_key, curtxn, &f, &metaFileID);
	assert(fileID == f);

	DB_val metaFileID_key[1];
	SLNMetaFileByIDKeyPack(metaFileID_key, curtxn, metaFileID);
	DB_val metaFile_val[1];
	rc = db_cursor_seek(age_uri, metaFileID_key, metaFile_val, 0);
	if(DB_SUCCESS != rc) return UINT64_MAX;
	uint64_t const f2 = db_read_uint64(metaFile_val);
	assert(fileID == f2);
	strarg_t const targetURI = db_read_string(metaFile_val, curtxn);

	DB_range targetFileIDs[1];
	SLNURIAndFileIDRange1(targetFileIDs, curtxn, targetURI);
	DB_val targetFileID_key[1];
	rc = db_cursor_firstr(age_files, targetFileIDs, targetFileID_key, NULL, +1);
	assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));
	for(; DB_SUCCESS == rc; rc = db_cursor_nextr(age_files, targetFileIDs, targetFileID_key, NULL, +1)) {
		strarg_t u;
		uint64_t targetFileID;
		SLNURIAndFileIDKeyUnpack(targetFileID_key, curtxn, &u, &targetFileID);
		assert(0 == strcmp(targetURI, u));
		if([subfilter fullAge:targetFileID] < UINT64_MAX) return metaFileID;
	}
	return UINT64_MAX;
}
- (uint64_t)fastAge:(uint64_t const)fileID :(uint64_t const)sortID {
	return [self fullAge:fileID];
}
@end

