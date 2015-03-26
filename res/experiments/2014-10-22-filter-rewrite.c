
int db_cursor_seek_dup(MDB_cursor *const cursor, dbid_t const table, uint64_t const key, MDB_val *const val, int const dir);
int db_cursor_first_dup(MDB_cursor *const cursor, dbid_t const table, uint64_t const key, MDB_val *const val, int const dir);


- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	int rc;
	uint64_t const actualSortID = [self seekMeta:dir :sortID];
	if(!valid(actualSortID)) return;
	DB_VAL(metaFileID_key, 2);
	db_bind(metaFileID_key, EFSMetaFileByID);
	db_bind(metaFileID_key, actualSortID);
	MDB_val metaFile_val[1];
	rc = db_cursor_seek(step_target, metaFileID_val, metaFile_val, 0);
	assertf(MDB_SUCCESS == rc, "Database error %s", mdb_strerror(rc));
	uint64_t const targetURI_id = db_column(metaFile_val, 1);
	DB_VAL(targetURI_val, 1);
	db_bind(targetURI_val, targetURI_id);
	if(sortID == actualSortID) {
		uint64_t actualFileID = fileID;
		rc = db_cursor_seek_dup(step_files, EFSURIAndFileID, targetURI_id, &actualFileID, +1);
//		DB_VAL(fileID_val, 1);
//		db_bind(fileID_val, fileID);
//		rc = mdb_cursor_get(step_files, targetURI_val, fileID_val, MDB_GET_BOTH_RANGE);
		if(MDB_SUCCESS != rc) return;
//		uint64_t const actualFileID = db_column(fileID_val, 0);
		if(fileID == actualFileID) return;
		if(dir > 0) return (void)[self step:-1];
	} else {
		rc = db_cursor_get(step_files, targetURI_val, NULL, MDB_SET);
		assertf(MDB_SUCCESS == rc || MDB_NOTFOUND == rc, "Database error %s", mdb_strerror(rc));
	}
}













- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	int rc;
	uint64_t const actualSortID = [self seekMeta:dir :sortID];
	if(!valid(actualSortID)) return;
	DB_VAL(metaFileID_key, 2);
	db_bind(metaFileID_key, EFSMetaFileByID);
	db_bind(metaFileID_key, actualSortID);
	MDB_val metaFile_val[1];
	rc = db_cursor_seek(step_target, metaFileID_key, metaFile_val, 0);
	assertf(MDB_SUCCESS == rc, "Database error %s", mdb_strerror(rc));
	uint64_t const targetURI_id = db_column(metaFile_val, 1);
	DB_VAL(targetURI_val, 1);
	db_bind(targetURI_val, targetURI_id);

	DB_RANGE(fileIDs, 2);
	db_bind(fileIDs->min, EFSURIAndFileID);
	db_bind(fileIDs->max, EFSURIAndFileID);
	db_bind(fileIDs->min, targetURI_id+0);
	db_bind(fileIDs->max, targetURI_id+1);
	if(sortID == actualSortID) {
		DB_VAL(fileID_key, 3);
		db_bind(fileID_key, EFSURIAndFileID);
		db_bind(fileID_key, targetURI_id+0);
		db_bind(fileID_key, fileID);
		rc = db_cursor_seekr(step_files, fileIDs, fileID_key, NULL, dir);
	} else {
		MDB_val fileID_key[1];
		rc = db_cursor_firstr(step_files, fileIDs, fileID_key, NULL, dir);
	}
	assertf(MDB_SUCCESS == rc || MDB_NOTFOUND == rc, "Database error %s", mdb_strerror(rc));
}






- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	MDB_val fileID_key[1];
	int rc = db_cursor_get(step_files, fileID_key, NULL, MDB_GET_CURRENT);
	if(MDB_SUCCESS == rc) {
		if(sortID) *sortID = [self currentMeta:dir];
		if(fileID) *fileID = db_column(fileID_key, 2);
	} else {
		if(sortID) *sortID = invalid(dir);
		if(fileID) *fileID = invalid(dir);
	}
}
- (void)step:(int const)dir {
	int rc;
	MDB_val fileID_val[1];

	rc = db_cursor_get(step_files, fileID_key, NULL, MDB_GET_CURRENT);
	if(MDB_SUCCESS == rc) {
		uint64_t const targetURI_id = db_column(fileID_key, 1);
		DB_RANGE(fileIDs, 2);
		db_bind(fileIDs->min, EFSURIAndFileID);
		db_bind(fileIDs->max, EFSURIAndFileID);
		db_bind(fileIDs->min, targetURI_id+0);
		db_bind(fileIDs->max, targetURI_id+1);
		rc = db_cursor_nextr(step_files, fileIDs, fileID_key, dir);
	}
	if(MDB_SUCCESS == rc) return;

	for(uint64_t sortID = [self stepMeta:dir]; valid(sortID); sortID = [self stepMeta:dir]) {
		DB_VAL(metaFileID_key, 2);
		db_bind(metaFileID_key, EFSMetaFileByID);
		db_bind(metaFileID_key, sortID);
		MDB_val metaFile_val[1];
		rc = db_cursor_seek(step_target, metaFileID_key, metaFile_val, 0);
		assertf(MDB_SUCCESS == rc, "Database error %s", mdb_strerror(rc));
		uint64_t const targetURI_id = db_column(metaFile_val, 1);
		DB_RANGE(fileIDs, 2);
		db_bind(fileIDs->min, EFSURIAndFileID);
		db_bind(fileIDs->max, EFSURIAndFileID);
		db_bind(fileIDs->min, targetURI_id+0);
		db_bind(fileIDs->max, targetURI_id+1);
		MDB_val fileID_key[1];
		rc = db_cursor_firstr(step_files, fileIDs, fileID_key, NULL, +1);
		if(MDB_SUCCESS != rc) continue;
		return;
	}
}




struct {
	MDB_cursor *cursor;
	uint8_t min_buf[40];
	size_t min_len;
	uint8_t max_buf[40];
	size_t max_len;
};




#define DB_RANGE(name, cols) \
	DB_VAL(__min_##name, cols); \
	DB_VAL(__max_##name, cols); \
	DB_range name[1] = {{ __min_##name, __max_##name }}





















