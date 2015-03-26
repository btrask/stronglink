

#define DB_OK MDB_SUCCESS
#define DB_KEYEXIST MDB_KEYEXIST
#define DB_NOTFOUND MDB_NOTFOUND


typedef MDB_env DB_env;
typedef MDB_val DB_val;
typedef MDB_txn DB_txn;
typedef MDB_cursor DB_cursor;


int db_open(char const *const path, DB_env **const env);
void db_close(DB_env *const env);


// how to create a txn, and what does it map to in leveldb?



int db_get(DB_txn *const txn, DB_val *const key, DB_val *const val);
int db_put(DB_txn *const txn, DB_val const *const key, DB_val const *const val);


int db_cursor_open(DB_txn *const txn, DB_cursor **const cursor);
void db_cursor_close(DB_cursor *const cursor);

int db_cursor_current(DB_cursor *const cursor, DB_val *const key, DB_val *const val);

int db_cursor_seek(DB_cursor *const cursor, DB_val const *const key, DB_val *const val, int const dir);
int db_cursor_seek_pfx(DB_cursor *const cursor, DB_val const *const key, DB_val *const val);

int db_cursor_first(DB_cursor *const cursor, DB_val *const key, DB_val *const val, int const dir);
int db_cursor_next(DB_cursor *const cursor, DB_val *const key, DB_val *const val, int const dir);




void db_free(DB_val *const obj);





#define MDB_MAIN_DBI 1



int db_open(char const *const path, DB_env **const env) {
	int rc = mdb_env_create(env);
	if(MDB_SUCCESS != rc) return rc;
	rc = mdb_env_set_mapsize(*env, 1024 * 1024 * 256);
	assert(MDB_SUCCESS == rc);
	rc = mdb_env_set_maxreaders(*env, 126); // Default
	assert(MDB_SUCCESS == rc);
	rc = mdb_env_open(*env, path, MDB_NOSUBDIR, 0600);
	if(MDB_SUCCESS != rc) {
		mdb_env_close(*env);
		return rc;
	}
	MDB_txn *txn = NULL;
	rc = mdb_txn_begin(*env, NULL, 0, &txn);
	assert(MDB_SUCCESS == rc);
	MDB_dbi dbi;
	rc = mdb_dbi_open(txn, NULL, MDB_CREATE, &dbi);
	assert(MDB_SUCCESS == rc);
	assert(MDB_MAIN_DBI == dbi);
	rc = mdb_txn_commit(txn);
	assert(MDB_SUCCESS == rc);
	return DB_OK;
}
void db_close(DB_env *const env) {
	mdb_env_close(env);
}





int db_get(DB_txn *const txn, DB_val *const key, DB_val *const val) {
	MDB_dbi 
}
int db_put(DB_txn *const txn, DB_val const *const key, DB_val const *const val) {

}






