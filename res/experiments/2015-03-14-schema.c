



// so first
// can we even take anything for granted?

// thus, the DBSchema should have a key of 0 with a magic value

// we get to choose our own value, how fun
// it should be something suitably random to avoid collisions

// should it be a string or an integer?

// actually, it should be neither
// because our standard parsing has built-in error checking
// but if this value doesn't parse right, we want to handle it directly

// so it should be defined as a raw byte sequence

// on the other hand, its okay to use varints for strings because we're the ones encoding them
// if it doesnt exist, it doesnt exist, thats fine


#define DB_VERSION_MISMATCH (-30794)


#define DB_VAL_STORAGE(val, len) \
	uint8_t __buf_##val[(len)]; \
	*(val) = (DB_val){ 0, __buf_##val };


int db_schema_verify(DB_txn *const txn) {
	char const magic[] = "DBDB layer v1";
	size_t const len = sizeof(magic)-1;

	DB_val key[1];
	DB_VAL_STORAGE(key, DB_VARINT_MAX*2);
	db_bind_uint64(key, DBSchema);
	db_bind_uint64(key, 0);
	DB_val val[1];

	DB_cursor *cur;
	int rc = db_txn_cursor(txn, &cur);
	if(DB_SUCCESS != rc) return rc;
	rc = db_cursor_first(cur, NULL, NULL, +1);
	if(DB_SUCCESS != rc && DB_NOTFOUND != rc) return rc;

	// If the database is completely empty
	// we can assume it's ours to play with
	if(DB_NOTFOUND == rc) {
		*val = (DB_val){ len, (char *)magic };
		rc = db_txn_put(txn, key, val, 0);
		if(DB_SUCCESS != rc) return rc;
		return DB_SUCCESS;
	}

	rc = db_txn_get(txn, key, val);
	if(DB_SUCCESS != rc) return rc;
	if(len != val->size) return DB_VERSION_MISMATCH;
	if(0 != memcmp(val->data, magic, len)) return DB_VERSION_MISMATCH;
	return DB_SUCCESS;
}



// okay, that worked nicely

// now what about application-schema checking?

// at this point it's safe to use our various schema functions (i.e. strings)

enum {
	DBSchemaMagic = 0,
	DBSchemaMeta = 1,
};
enum {
	DBUnknownType = 0,
	DBUInt64Type = 1,
	DBStringType = 2,
};

int db_schema_table(txn, table, name);
int db_schema_column(txn, table, column, name, type);

// should we store column names or not?
// i guess we have to in order to properly verify that everything has the same meaning
// so the names arent even comments, they have to match


// note: we cant simply have a buffer type
// because the schema layer has to be able to determine the buffer length somehow
// either all buffers are length prefixed
// or else we have to record the length somewhere else


// we shouldn't put "EFS" or "SLN" in our table/column names, right?
// or maybe we should, in which case we should switch to "SLN" first



// obviously schema management is important for a finished product
// but do we have to have it for our first release?
// i dont want to burn out working on this when we have more important stuff to do

// for now we can just use the schema string
// increment it whenever the application schema changes
// then at some point we can introduce the schema description format


















