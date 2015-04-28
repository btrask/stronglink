#include "SLNFilter.h"

@implementation SLNURIFilter
- (void)free {
	curtxn = NULL;
	FREE(&URI);
	db_cursor_close(files); files = NULL;
	[super free];
}

- (SLNFilter *)unwrap {
	return self;
}

- (SLNFilterType)type {
	return SLNURIFilterType;
}
- (strarg_t)stringArg:(index_t const)i {
	if(0 == i) return URI;
	return NULL;
}
- (int)addStringArg:(strarg_t const)str :(size_t const)len {
	if(!URI) {
		URI = strndup(str, len);
		return DB_SUCCESS;
	}
	return DB_EINVAL;
}
- (void)print:(count_t const)depth {
	indent(depth);
	fprintf(stderr, "(uri \"%s\")\n", URI);
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth {
	return wr(data, size, URI);
}

- (int)prepare:(DB_txn *const)txn {
	int rc = [super prepare:txn];
	if(DB_SUCCESS != rc) return rc;
	db_cursor_renew(txn, &files); // SLNURIAndFileID
	curtxn = txn;
	return DB_SUCCESS;
}
- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	uint64_t x = sortID;
	if(valid(x) && dir > 0 && fileID > sortID) x++;
	if(valid(x) && dir < 0 && fileID < sortID) x--;

	DB_range range[1];
	DB_val key[1];
	SLNURIAndFileIDRange1(range, curtxn, URI);
	SLNURIAndFileIDKeyPack(key, curtxn, URI, x);
	int rc = db_cursor_seekr(files, range, key, NULL, dir);
	db_assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));

	// TODO: Skip files without any meta-files. The content of the
	// meta-file doesn't matter.
}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	DB_val key[1];
	int rc = db_cursor_current(files, key, NULL);
	if(DB_SUCCESS == rc) {
		strarg_t u;
		uint64_t x;
		SLNURIAndFileIDKeyUnpack(key, curtxn, &u, &x);
		if(sortID) *sortID = x;
		if(fileID) *fileID = x;
	} else {
		if(sortID) *sortID = invalid(dir);
		if(fileID) *fileID = invalid(dir);
	}
}
- (void)step:(int const)dir {
	DB_range range[1];
	SLNURIAndFileIDRange1(range, curtxn, URI);
	int rc = db_cursor_nextr(files, range, NULL, NULL, dir);
	db_assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));

	// TODO: Skip files without meta-files.
}
- (SLNAgeRange)fullAge:(uint64_t const)fileID {
	// TODO: We do actually have to check that the file matches our URI.
	// We shouldn't have to check that it has a meta-file, since that's
	// already determined.
	return (SLNAgeRange){fileID, UINT64_MAX};
}
- (uint64_t)fastAge:(uint64_t const)fileID :(uint64_t const)sortID {
	return [self fullAge:fileID].min;
}
@end

@implementation SLNAllFilter
- (void)free {
	db_cursor_close(files); files = NULL;
	[super free];
}

- (SLNFilter *)unwrap {
	return self;
}

- (SLNFilterType)type {
	return SLNAllFilterType;
}
- (void)print:(count_t const)depth {
	indent(depth);
	fprintf(stderr, "(all)\n");
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth {
	return wr(data, size, "*");
}

- (int)prepare:(DB_txn *const)txn {
	int rc = [super prepare:txn];
	if(DB_SUCCESS != rc) return rc;
	db_cursor_renew(txn, &files); // SLNFileByID
	return DB_SUCCESS;
}
- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	// TODO: Copy and paste from SLNURIFilter.
	uint64_t x = sortID;
	if(valid(x) && dir > 0 && fileID > sortID) x++;
	if(valid(x) && dir < 0 && fileID < sortID) x--;

	DB_range range[1];
	DB_val key[1];
	SLNFileByIDRange0(range, curtxn);
	SLNFileByIDKeyPack(key, curtxn, x);
	int rc = db_cursor_seekr(files, range, key, NULL, dir);
	db_assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));
}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	DB_val key[1];
	int rc = db_cursor_current(files, key, NULL);
	if(DB_SUCCESS == rc) {
		dbid_t const table = db_read_uint64(key);
		assert(SLNFileByID == table);
		uint64_t const x = db_read_uint64(key);
		if(sortID) *sortID = x;
		if(fileID) *fileID = x;
	} else {
		if(sortID) *sortID = invalid(dir);
		if(fileID) *fileID = invalid(dir);
	}
}
- (void)step:(int const)dir {
	DB_range range[1];
	SLNFileByIDRange0(range, curtxn);
	int rc = db_cursor_nextr(files, range, NULL, NULL, dir);
	db_assertf(DB_SUCCESS == rc || DB_NOTFOUND == rc, "Database error %s", db_strerror(rc));
}
- (SLNAgeRange)fullAge:(uint64_t const)fileID {
	return (SLNAgeRange){fileID, UINT64_MAX};
}
- (uint64_t)fastAge:(uint64_t const)fileID :(uint64_t const)sortID {
	return [self fullAge:fileID].min;
}
@end

