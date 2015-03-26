
typedef enum {
	DB_INVALID = 0,
	DB_UINT64 = 1,
	DB_STRING = 2,
	DB_STRING_RAW = 3,
} DB_type;
typedef struct {
	strarg_t name;
	DB_type type;
} DB_column;


int db_schema_open(MDB_txn *const txn, DB_schema *const schema);
int db_dbi_open(MDB_txn *const txn, DB_schema *const schema, unsigned int opts, DB_column const *const cols, count_t const ncols, strarg_t const name, MDB_dbi *const dbi);
#define DB_DBI_OPEN(txn, schema, opts, cols, name, dbi) ({ \
	DB_column __cols[] = (cols);\
	db_dbi_open((txn), (schema), (opts), __cols, numberof(__cols), (name), (dbi)); \
})


int db_schema_open(MDB_txn *const txn, DB_schema *const schema) {
	mdb_dbi_open(txn, "schema", MDB_CREATE | MDB_DUPSORT, &schema->schema);
	mdb_dbi_open(txn, "stringByID", MDB_CREATE, &schema->stringByID);
	mdb_dbi_open(txn, "stringIDByValue", MDB_CREATE, &schema->stringIDByValue);
	mdb_dbi_open(txn, "stringIDByHash", MDB_CREATE, &schema->stringIDByHash);
	return 0;
}
int db_dbi_open(MDB_txn *const txn, DB_schema *const schema, unsigned int opts, DB_column const *const cols, count_t const ncols, strarg_t const name, MDB_dbi *const dbi) {
	int rc;

	uint64_t const dbname_id = db_string_id(txn, schema, name);
	if(!dbname_id) return -1;

	DB_VAL(dbinfo_val, 2);
	db_bind(dbinfo_val, 0);
	db_bind(dbinfo_val, dbname_id);
	DB_VAL(info_val, 1);
	db_bind(info_val, 0xff & opts);
	mdb_put(txn, schema->schema, dbinfo_val, info_val, MDB_NOOVERWRITE);
	// TODO: Check opts

	MDB_cursor *cur = NULL;
	mdb_cursor_open(txn, schema->schema, &cur);

	DB_VAL(dbcols_val, 2);
	db_bind(dbcols_val, 1);
	db_bind(dbcols_val, dbname_id);
	MDB_val col_val;
	mdb_cursor_get(cur, &dbcols_val, &col_val, MDB_GET);
	for(; MDB_SUCCESS == rc; rc = mdb_cursor_get(cur, &dbcols_val, &col_val, MDB_NEXT_DUP)) {
		uint64_t const col = db_column(col_val, 0);
		uint64_t const type = db_column(col_val, 1);
		strarg_t const colname = db_column_text(txn, schema, col_val, 2);

		if(col >= ncols) break; // Extra columns are not an error.
		if(type != cols[i].type || 0 != strcmp(colname, cols[i].name)) {
			mdb_cursor_close(cur); cur = NULL;
			return -1;
		}
	}

	mdb_cursor_close(cur); cur = NULL;

	for(index_t i = 0; i < ncols; ++i) {
		uint64_t const colname_id = db_string_id(txn, schema, cols[i].name);
		if(!colname_id) return -1;

		DB_VAL(col_val, 3);
		db_bind(col_val, i);
		db_bind(col_val, cols[i].type);
		db_bind(col_val, colname_id);
		rc = mdb_put(txn, schema->schema, dbcols_val, col_val, MDB_NODUPDATA);
		if(MDB_SUCCESS != rc && MDB_KEYEXIST != rc) return -1;
	}

	mdb_dbi_open(txn, name, MDB_CREATE | opts, dbi);
	return 0;
}


