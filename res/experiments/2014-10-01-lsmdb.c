

typedef struct LSMDB_env LSMDB_env;
typedef struct LSMDB_txn LSMDB_txn;
typedef struct LSMDB_cursor LSMDB_cursor;


#define DEPTH_MAX 16


struct LSMDB_env {
	MDB_env *env;
	MDB_dbi dbis[DEPTH_MAX*2];
};
struct LSMDB_txn {
	LSMDB_env *env;
	LSMDB_txn *parent;
	MDB_txn *txn;

	LSMDB_level depth;
	LSMDB_cursor *cursor;
	MDB_cursor *merge;
};
struct LSMDB_cursor {
	LSMDB_txn *txn;
	MDB_cursor *cursors[DEPTH_MAX];
	LSMDB_level sorted[DEPTH_MAX];
};

