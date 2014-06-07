#include <uv.h>
#include "../deps/crypt_blowfish-1.0.4/ow-crypt.h"
#include "../deps/libco/libco.h"
#include "../deps/sqlite/sqlite3.h"
#include "EarthFS.h"

struct EFSSession {
	EFSRepoRef repo;
	int64_t userID;
	EFSMode mode;
};

static int passcmp(strarg_t const a, strarg_t const b) {
	volatile int r = 0;
	for(off_t i = 0; ; ++i) {
		if(a[i] != b[i]) r = -1;
		if(!a[i] || !b[i]) break;
	}
	return r;
}

typedef struct {
	cothread_t thread;
} generic_args;
static void done(uv_work_t *const req, int const status) {
	generic_args *const args = req->data;
	co_switch(args->thread);
}

typedef struct {
	cothread_t thread;
	// IN
	EFSRepoRef repo;
	strarg_t username;
	strarg_t password;
	strarg_t cookie;
	// OUT
	int64_t userID;
	EFSMode mode;
} auth_args;
static void sql_auth(uv_work_t *const req) {
	auth_args *const args = req->data;

	sqlite3 *const db = EFSRepoDBConnect(args->repo);
	if(!db) return;

	sqlite3_stmt *stmt = NULL;
	(void)BTSQLiteErr(sqlite3_prepare_v2(db,
		"SELECT \"userID\", \"passhash\""
		" FROM \"users\" WHERE \"username\" = ?"
		" LIMIT 1",
		-1, &stmt, NULL));
	(void)BTSQLiteErr(sqlite3_bind_text(stmt, 1, args->username, -1, SQLITE_TRANSIENT));
	(void)BTSQLiteErr(sqlite3_step(stmt));

	int64_t const userID = sqlite3_column_int(stmt, 0);
	strarg_t passhash = (strarg_t)sqlite3_column_text(stmt, 1);

	if(!passhash) {
		(void)BTSQLiteErr(sqlite3_finalize(stmt));
		EFSRepoDBClose(args->repo, db);
		return;
	}

	int size = 0;
	void *data = NULL;
	strarg_t attempt = crypt_ra(args->password, passhash, &data, &size);
	if(!attempt || 0 != passcmp(attempt, passhash)) {
		FREE(&data); attempt = NULL;
		(void)BTSQLiteErr(sqlite3_finalize(stmt));
		EFSRepoDBClose(args->repo, db);
		return;
	}
	FREE(&data); attempt = NULL;
	(void)BTSQLiteErr(sqlite3_finalize(stmt));
	EFSRepoDBClose(args->repo, db);

	args->userID = userID;
	args->mode = EFS_RDWR; // TODO
}

EFSSessionRef EFSRepoCreateSession(EFSRepoRef const repo, strarg_t const username, strarg_t const password, strarg_t const cookie, EFSMode const mode) {
	if(!repo) return NULL;

	auth_args args = {
		.thread = co_active(),
		.repo = repo,
		.username = username,
		.password = password,
		.cookie = cookie,
	};
	uv_work_t req = { .data = &args };
	uv_queue_work(uv_default_loop(), &req, sql_auth, done);
	EFSYieldToRepoThread(repo);

	if(!args.userID) return NULL;

	EFSSessionRef const session = calloc(1, sizeof(struct EFSSession));
	session->repo = repo;
	session->userID = args.userID;
	session->mode = args.mode;
	return session;
}
void EFSSessionFree(EFSSessionRef const session) {
	if(!session) return;
	session->repo = NULL;
	session->userID = -1;
	session->mode = 0;
	free(session);
}

