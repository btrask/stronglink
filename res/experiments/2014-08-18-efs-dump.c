#include "../deps/liblmdb/lmdb.h"
#include "../src/db.h"

int main(int const argc, char const *const *const argv) {

	MDB_env *env = NULL;
	mdb_env_create(&env);
	mdb_env_set_maxdbs(conn->env, 32);
	mdb_env_open(env, "/home/ben/Documents/testrepo/efs.db", MDB_NOSUBDIR, 0600);

	MDB_dbi userByID, userIDByName;
	mdb_dbi_open(env, "userByID", 0, &userByID);
	mdb_dbi_open(env, "userIDByName", 0, &userIDByName);

	MDB_txn *txn = NULL;
	mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);

	MDB_cursor *cur = NULL;
	mdb_cursor_open(txn, userByID, &cur);

	MDB_val userID_val;
	MDB_val user_val;
	int rc = mdb_cursor_get(cur, &userID_val, &user_val, MDB_FIRST);
	for(; MDB_SUCCESS == rc; rc = mdb_cursor_get(cur, &userID_val, &user_val, MDB_NEXT)) {


		int64_t const userID = db_read_int64(&userID_val);
		strarg_t const username = db_read_text(&user_val);
		strarg_t const passhash = db_read_text(&user_val);
		strarg_t const token = db_read_text(&user_val);


		fprintf(stdout, "%lld: %s, %s, %s\n", userID, username, passhash, token);


	}

	return EXIT_SUCCESS;
}


