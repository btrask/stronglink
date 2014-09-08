#include "EFSFilter.h"

@implementation EFSMetaFileFilter
- (void)free {
	[subfilter free];
	mdb_cursor_close(metafiles); metafiles = NULL;
	mdb_cursor_close(age_metafile); age_metafile = NULL;
	mdb_cursor_close(age_uri); age_uri = NULL;
	mdb_cursor_close(age_files); age_files = NULL;
	[super free];
}

- (EFSFilterType)type {
	return EFSMetaFileFilterType;
}
- (EFSFilter *)unwrap {
	return [subfilter unwrap];
}
- (err_t)addFilterArg:(EFSFilter *const)filter {
	if(subfilter) return -1;
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
	if([subfilter prepare:txn :conn] < 0) return -1;
	db_cursor(txn, conn->metaFileByID, &metafiles);
	db_cursor(txn, conn->metaFileIDByFileID, &age_metafile);
	db_cursor(txn, conn->metaFileByID, &age_uri);
	db_cursor(txn, conn->fileIDByURI, &age_files);
	return 0;
}

- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	[self _seek:dir :sortID :fileID];
	[subfilter seek:dir :sortID :fileID];
}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	uint64_t s1, f1, s2, f2;
	[self _current:dir :&s1 :&f1];
	[subfilter current:dir :&s2 :&f2];
	if(dir < 0) {
		if(sortID) *sortID = MAX(s1, s2);
		if(fileID) *fileID = MAX(f1, f2);
	} else {
		if(sortID) *sortID = MIN(s1, s2);
		if(fileID) *fileID = MIN(f1, f2);
	}
}
- (bool_t)step:(int const)dir {
	uint64_t s1, f1, s2, f2;
	[self _current:dir :&s1 :&f1];
	[subfilter current:dir :&s2 :&f2];
	if(s1 == s2 && f1 == f2) {
		bool_t const x1 = [self _step:dir];
		bool_t const x2 = [subfilter step:dir];
		return x1 || x2;
	} else if((s1 < s2) || (s1 == s2 && f1 < f2)) {
		if(dir > 0) return [self _step:dir];
		else return [subfilter step:dir];
	} else if((s1 > s2) || (s1 == s2 && f1 > f2)) {
		if(dir < 0) return [self _step:dir];
		else return [subfilter step:dir];
	}
	assert(0);
	return false;
}
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID {
	return MIN([self _age:sortID :fileID], [subfilter age:sortID :fileID]);
}

- (void)_seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
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
		[self _step:-dir];
	} else {
		if(dir > 0) [self _step:-1];
	}
}
- (void)_current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
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
- (bool_t)_step:(int const)dir {
	int rc = db_cursor_get(metafiles, NULL, NULL, op(dir, MDB_NEXT));
	return MDB_SUCCESS == rc;
}
- (uint64_t)_age:(uint64_t const)sortID :(uint64_t const)fileID {
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

