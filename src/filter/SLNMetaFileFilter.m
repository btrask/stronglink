// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include "SLNFilter.h"

@implementation SLNMetaFileFilter
- (void)free {
	kvs_cursor_close(metafiles); metafiles = NULL;
	[super free];
}

- (SLNFilterType)type {
	return SLNMetaFileFilterType;
}
- (SLNFilter *)unwrap {
	return self;
}
- (void)printSexp:(FILE *const)file :(size_t const)depth {
	indent(file, depth);
	fprintf(file, "(meta)\n");
}
- (void)printUser:(FILE *const)file :(size_t const)depth {
	assert(!"print meta-file filter");
}

- (int)prepare:(KVS_txn *const)txn {
	int rc = [super prepare:txn];
	if(rc < 0) return rc;
	kvs_cursor_open(txn, &metafiles); // SLNMetaFileByID
	return 0;
}
- (void)reset {
	kvs_cursor_close(metafiles); metafiles = NULL;
	[super reset];
}

- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	KVS_range range[1];
	KVS_val key[1], val[1];
	SLNMetaFileByIDRange0(range, NULL);
	SLNMetaFileByIDKeyPack(key, NULL, sortID);
	int rc = kvs_cursor_seekr(metafiles, range, key, val, dir);
	if(KVS_NOTFOUND == rc) return;
	kvs_assertf(rc >= 0, "Database error %s", sln_strerror(rc));

	uint64_t actualSortID, actualFileID;
	SLNMetaFileByIDKeyUnpack(key, NULL, &actualSortID);
	actualFileID = actualSortID;
	if(sortID != actualSortID) return;
	if(dir > 0 && actualFileID >= fileID) return;
	if(dir < 0 && actualFileID <= fileID) return;
	[self step:dir];
}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	KVS_val key[1], val[1];
	int rc = kvs_cursor_current(metafiles, key, val);
	if(rc >= 0) {
		uint64_t x;
		SLNMetaFileByIDKeyUnpack(key, NULL, &x);
		if(sortID) *sortID = x;
		if(fileID) *fileID = x;
	} else {
		if(sortID) *sortID = invalid(dir);
		if(fileID) *fileID = invalid(dir);
	}
}
- (void)step:(int const)dir {
	KVS_range range[1];
	SLNMetaFileByIDRange0(range, NULL);
	int rc = kvs_cursor_nextr(metafiles, range, NULL, NULL, dir);
	kvs_assertf(rc >= 0 || KVS_NOTFOUND == rc, "Database error %s", sln_strerror(rc));
}
- (SLNAgeRange)fullAge:(uint64_t const)fileID {
	// TODO: Check that fileID is a meta-file.
	return (SLNAgeRange){fileID, UINT64_MAX};
}
- (uint64_t)fastAge:(uint64_t const)fileID :(uint64_t const)sortID {
	return [self fullAge:fileID].min;
}
@end

