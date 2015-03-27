

#define DB_FIRST_ERRCODE MDB_KEYEXIST
#define DB_LAST_ERRCODE MDB_LAST_ERRCODE

int efs_strerror(int const x) {
	if(x >= 0) return "no error";
	if(x >= DB_LAST_ERRCODE && x <= DB_FIRST_ERRCODE) return db_strerrror(x);
	return uv_strerror(x);
}


#define MDB_SUCCESS	 0
	/** key/data pair already exists */
#define MDB_KEYEXIST	(-30799)
	/** key/data pair not found (EOF) */
#define MDB_NOTFOUND	(-30798)
	/** Requested page not found - this usually indicates corruption */
#define MDB_PAGE_NOTFOUND	(-30797)
	/** Located page was wrong type */
#define MDB_CORRUPTED	(-30796)
	/** Update of meta page failed, probably I/O error */
#define MDB_PANIC		(-30795)
	/** Environment version mismatch */
#define MDB_VERSION_MISMATCH	(-30794)
	/** File is not a valid LMDB file */
#define MDB_INVALID	(-30793)
	/** Environment mapsize reached */
#define MDB_MAP_FULL	(-30792)
	/** Environment maxdbs reached */
#define MDB_DBS_FULL	(-30791)
	/** Environment maxreaders reached */
#define MDB_READERS_FULL	(-30790)
	/** Too many TLS keys in use - Windows only */
#define MDB_TLS_FULL	(-30789)
	/** Txn has too many dirty pages */
#define MDB_TXN_FULL	(-30788)
	/** Cursor stack too deep - internal error */
#define MDB_CURSOR_FULL	(-30787)
	/** Page has not enough space - internal error */
#define MDB_PAGE_FULL	(-30786)
	/** Database contents grew beyond environment mapsize */
#define MDB_MAP_RESIZED	(-30785)
	/** MDB_INCOMPATIBLE: Operation and DB incompatible, or DB flags changed */
#define MDB_INCOMPATIBLE	(-30784)
	/** Invalid reuse of reader locktable slot */
#define MDB_BAD_RSLOT		(-30783)
	/** Transaction cannot recover - it must be aborted */
#define MDB_BAD_TXN			(-30782)
	/** Unsupported size of key/DB name/data, or wrong DUPFIXED size */
#define MDB_BAD_VALSIZE		(-30781)
	/** The specified DBI was changed unexpectedly */
#define MDB_BAD_DBI		(-30780)
	/** The last defined error code */
#define MDB_LAST_ERRCODE	MDB_BAD_DBI






// okay, our error handling is pretty messed up
// it was fine as long as we didnt mix uv-errors with db-errors
// but because opening the database can return uv_canceled, we're screwed

// the simplest thing to do would be introduce DB_CANCELED
// can we just do that?
// would it be good enough?

// now that we're using more careful error handling
// are there too many cases where we need to handle both types in one function?

// obviously there is an ECANCELED we can use...
// or we can convert fairly easily...

// it's useful to know whether an error came from the db or not...

// now i understand why certain systems define "error domains"






























