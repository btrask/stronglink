#include "assert.h"
#include "../deps/liblmdb/lmdb.h"
#include "../src/db.h"

#define assert_mdb(rc) (assertf(MDB_SUCCESS == (rc), "MDB error %s\n", mdb_strerror((rc))))

static void dump_users(MDB_env *env, MDB_txn *txn) {
	int rc;
	MDB_dbi userByID, userIDByName;
	rc = mdb_dbi_open(txn, "userByID", 0, &userByID);
	assert_mdb(rc);
	rc = mdb_dbi_open(txn, "userIDByName", 0, &userIDByName);
	assert_mdb(rc);

	MDB_cursor *cur = NULL;
	rc = mdb_cursor_open(txn, userByID, &cur);
	assert_mdb(rc);
	fprintf(stdout, "Users:\n");

	MDB_val userID_val;
	MDB_val user_val;
	rc = mdb_cursor_get(cur, &userID_val, &user_val, MDB_FIRST);
	for(; MDB_SUCCESS == rc; rc = mdb_cursor_get(cur, &userID_val, &user_val, MDB_NEXT)) {
		int64_t const userID = db_read_int64(&userID_val);
		strarg_t const username = db_read_text(&user_val);
		strarg_t const passhash = db_read_text(&user_val);
		strarg_t const token = db_read_text(&user_val);

		MDB_val username_val = { strlen(username)+1, (void *)username };
		MDB_val indexUserID_val;
		rc = mdb_get(txn, userIDByName, &username_val, &indexUserID_val);
		assert_mdb(rc);
		int64_t const indexUserID = db_read_int64(&indexUserID_val);
		assertf(userID == indexUserID, "Index userIDByName gave wrong result (expected %lld, got %lld)\n", userID, indexUserID);

		fprintf(stdout, "	%lld: '%s', '%s', '%s'\n", userID, username, passhash, token);
	}

	mdb_cursor_close(cur); cur = NULL;
	mdb_dbi_close(env, userByID); userByID = 0;
	mdb_dbi_close(env, userIDByName); userIDByName = 0;
	fprintf(stdout, "\n");
}
static void dump_pulls(MDB_env *env, MDB_txn *txn) {
	int rc;
	MDB_dbi pullByID;
	rc = mdb_dbi_open(txn, "pullByID", 0, &pullByID);
	assert_mdb(rc);

	MDB_cursor *cur = NULL;
	rc = mdb_cursor_open(txn, pullByID, &cur);
	assert_mdb(rc);
	fprintf(stdout, "Pulls:\n");

	MDB_val pullID_val;
	MDB_val pull_val;
	rc = mdb_cursor_get(cur, &pullID_val, &pull_val, MDB_FIRST);
	for(; MDB_SUCCESS == rc; rc = mdb_cursor_get(cur, &pullID_val, &pull_val, MDB_NEXT)) {
		int64_t const pullID = db_read_int64(&pullID_val);
		int64_t const userID = db_read_int64(&pull_val);
		strarg_t const host = db_read_text(&pull_val);
		strarg_t const username = db_read_text(&pull_val);
		strarg_t const password = db_read_text(&pull_val);
		strarg_t const cookie = db_read_text(&pull_val);
		strarg_t const query = db_read_text(&pull_val);

		fprintf(stdout, "	%lld: %lld, '%s', '%s', '%s', '%s', '%s'\n", pullID, userID, host, username, password, cookie, query);
	}

	mdb_cursor_close(cur); cur = NULL;
	mdb_dbi_close(env, pullByID); pullByID = 0;
	fprintf(stdout, "\n");
}

int main(int const argc, char const *const *const argv) {
	int rc;

	MDB_env *env = NULL;
	rc = mdb_env_create(&env);
	assert(MDB_SUCCESS == rc);
	rc = mdb_env_set_maxdbs(env, 32);
	assert(MDB_SUCCESS == rc);
	rc = mdb_env_open(env, "/home/ben/Documents/testrepo/efs.db", MDB_NOSUBDIR, 0600);
	assert(MDB_SUCCESS == rc);

	MDB_txn *txn = NULL;
	rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
	assert(MDB_SUCCESS == rc);


	dump_users(env, txn);
	dump_pulls(env, txn);
	// TODO


	mdb_txn_abort(txn); txn = NULL;
	mdb_env_close(env); env = NULL;

	return EXIT_SUCCESS;
}


