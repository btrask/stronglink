#include "SLNFilter.h"

@implementation SLNMetaFileFilter
- (void)free {
	db_cursor_close(metafiles); metafiles = NULL;
	db_cursor_close(age); age = NULL;
	[super free];
}

- (SLNFilterType)type {
	return SLNMetaFileFilterType;
}
- (SLNFilter *)unwrap {
	return self;
}
- (void)print:(size_t const)depth {
	indent(depth);
	fprintf(stderr, "(meta)\n");
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(size_t const)depth {
	assert(0);
	return wr(data, size, "");
}

- (int)prepare:(DB_txn *const)txn {
	int rc = [super prepare:txn];
	if(DB_SUCCESS != rc) return rc;
	db_cursor_renew(txn, &metafiles); // SLNMetaFileByID
	db_cursor_renew(txn, &age); // SLNFileIDAndMetaFileID
	return DB_SUCCESS;
}

- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	DB_range range[1];
	DB_val key[1], val[1];
	SLNMetaFileByIDRange0(range, curtxn);
	SLNMetaFileByIDKeyPack(key, curtxn, sortID);
	int rc = db_cursor_seekr(metafiles, range, key, val, dir);
	if(DB_NOTFOUND == rc) return;
	db_assertf(DB_SUCCESS == rc, "Database error %s", db_strerror(rc));

	uint64_t actualSortID, actualFileID;
	SLNMetaFileByIDKeyUnpack(key, curtxn, &actualSortID);
	//SLNMetaFileByIDValUnpack(val, curtxn, &actualFileID, &ignore);
	actualFileID = db_read_uint64(val);
	if(sortID != actualSortID) return;
	if(dir > 0 && actualFileID >= fileID) return;
	if(dir < 0 && actualFileID <= fileID) return;
	[self step:dir];
}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	DB_val key[1], val[1];
	int rc = db_cursor_current(metafiles, key, val);
	if(DB_SUCCESS == rc) {
		uint64_t s;
		SLNMetaFileByIDKeyUnpack(key, curtxn, &s);
		uint64_t f;
		strarg_t ignore;
		f = db_read_uint64(val);
		//SLNMetaFileByIDValUnpack(val, curtxn, &f, &ignore);
		if(sortID) *sortID = s;
		if(fileID) *fileID = f;
	} else {
		if(sortID) *sortID = invalid(dir);
		if(fileID) *fileID = invalid(dir);
	}
}
- (void)step:(int const)dir {
	DB_range range[1];
	SLNMetaFileByIDRange0(range, curtxn);
	int rc = db_cursor_nextr(metafiles, range, NULL, NULL, dir);
	db_assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));
}
- (SLNAgeRange)fullAge:(uint64_t const)fileID {
	DB_range range[1];
	DB_val key[1];
	SLNFileIDAndMetaFileIDRange1(range, curtxn, fileID);
	int rc = db_cursor_firstr(age, range, key, NULL, +1);
	if(DB_NOTFOUND == rc) return (SLNAgeRange){UINT64_MAX, UINT64_MAX};
	db_assertf(DB_SUCCESS == rc, "Database error %s", db_strerror(rc));
	uint64_t f, sortID;
	SLNFileIDAndMetaFileIDKeyUnpack(key, curtxn, &f, &sortID);
	return (SLNAgeRange){sortID, UINT64_MAX};
}
- (uint64_t)fastAge:(uint64_t const)fileID :(uint64_t const)sortID {
	return [self fullAge:fileID].min;
}
@end

