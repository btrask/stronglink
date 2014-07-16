#define _GNU_SOURCE
#include "EarthFS.h"
#include "async.h"
#include "http/HTTPMessage.h"

#define URI_MAX 1024

struct EFSPull {
	EFSRepoRef repo;
	int64_t pullID;
	EFSSessionRef session;
	str_t *host;
	str_t *username;
	str_t *password;
	str_t *cookie;
	str_t *query;
};

static err_t auth(EFSPullRef const pull, HTTPConnectionRef const conn);
static err_t import(EFSPullRef const pull, HTTPConnectionRef const conn, strarg_t const URI);

EFSPullRef EFSRepoCreatePull(EFSRepoRef const repo, int64_t const pullID, int64_t const userID, strarg_t const host, strarg_t const username, strarg_t const password, strarg_t const cookie, strarg_t const query) {
	EFSPullRef pull = calloc(1, sizeof(struct EFSPull));
	if(!pull) return NULL;
	pull->repo = repo;
	pull->pullID = pullID;
	pull->session = EFSRepoCreateSessionInternal(repo, userID);
	pull->username = strdup(username);
	pull->password = strdup(password);
	pull->cookie = cookie ? strdup(cookie) : NULL;
	pull->host = strdup(host);
	pull->query = strdup(query);
	return pull;
}
void EFSPullFree(EFSPullRef *const pullptr) {
	EFSPullRef pull = *pullptr;
	if(!pull) return;
	pull->repo = NULL;
	pull->pullID = -1;
	EFSSessionFree(&pull->session);
	FREE(&pull->host);
	FREE(&pull->username);
	FREE(&pull->password);
	FREE(&pull->query);
	FREE(pullptr); pull = NULL;
}

static EFSPullRef pull_arg;
static void pull_thread(void) {
	EFSPullRef const pull = pull_arg;

	HTTPConnectionRef queryConn = NULL;
	HTTPConnectionRef fileConn = NULL;
	HTTPMessageRef msg = NULL;

	for(;;) {
		if(queryConn || fileConn || msg) {
			HTTPMessageFree(&msg);
			HTTPConnectionFree(&fileConn);
			HTTPConnectionFree(&queryConn);
			async_sleep(1000 * 5);
		}

		queryConn = HTTPConnectionCreateOutgoing(pull->host);
		fileConn = HTTPConnectionCreateOutgoing(pull->host);

		if(auth(pull, queryConn) < 0) {
			fprintf(stderr, "Pull auth error\n");
			continue;
		}

		msg = HTTPMessageCreate(queryConn);
		HTTPMessageWriteRequest(msg, HTTP_GET, "/efs/query?count=all", pull->host);
		if(pull->cookie) HTTPMessageWriteHeader(msg, "Cookie", pull->cookie);
		HTTPMessageBeginBody(msg);
		HTTPMessageEnd(msg);
		uint16_t const status = HTTPMessageGetResponseStatus(msg);
		if(403 == status) {
			FREE(&pull->cookie);
			continue;
		}
		if(status < 200 || status >= 300) {
			fprintf(stderr, "Pull query error %d\n", (int)status);
			continue;
		}

		for(;;) {
			str_t URI[URI_MAX+1];
			if(HTTPMessageReadLine(msg, URI, URI_MAX) < 0) break;
			for(;;) {
				if(import(pull, fileConn, URI) >= 0) break;
				async_sleep(1000);
			}
		}

	}

	HTTPMessageFree(&msg);
	HTTPConnectionFree(&fileConn);
	HTTPConnectionFree(&queryConn);

	co_terminate();
}
void EFSPullStart(EFSPullRef const pull) {
	if(!pull) return;
	pull_arg = pull;
	async_wakeup(co_create(STACK_DEFAULT, pull_thread));
}

typedef struct {
	strarg_t set_cookie;
} EFSAuthHeaders;
static HeaderField const EFSAuthFields[] = {
	{"set-cookie", 100},
};
static err_t auth(EFSPullRef const pull, HTTPConnectionRef const conn) {
	if(!pull) return 0;
	if(pull->cookie) return 0;

	HTTPMessageRef msg = HTTPMessageCreate(conn);
	HTTPMessageWriteRequest(msg, HTTP_POST, "/efs/auth", pull->host);
	HTTPMessageWriteContentLength(msg, 0);
	HTTPMessageBeginBody(msg);
	// TODO: Send credentials.
	HTTPMessageEnd(msg);

	uint16_t const status = HTTPMessageGetResponseStatus(msg);
	EFSAuthHeaders const *const headers = HTTPMessageGetHeaders(msg, EFSAuthFields, numberof(EFSAuthFields));

	fprintf(stderr, "Session cookie %s\n", headers->set_cookie);
	// TODO: Parse and store.

	HTTPMessageFree(&msg);

	return 0;
}


typedef struct {
	strarg_t content_type;
	strarg_t content_length;
} EFSImportHeaders;
static HeaderField const EFSImportFields[] = {
	{"content-type", 100},
	{"content-length", 100},
};
static err_t import(EFSPullRef const pull, HTTPConnectionRef const conn, strarg_t const URI) {
	if(!pull) return 0;
	if(!URI) return 0;

	str_t algo[EFS_ALGO_SIZE];
	str_t hash[EFS_HASH_SIZE];
	if(!EFSParseURI(URI, algo, hash)) return 0;

	// TODO: Have a public API for testing whether a file exists without actually loading the file info with EFSSessionCopyFileInfo(). Even once we have a smarter fast-forward algorithm, this will still be useful.
	EFSSessionRef const session = pull->session;
	EFSRepoRef const repo = EFSSessionGetRepo(session);
	sqlite3 *db = EFSRepoDBConnect(repo);
	sqlite3_stmt *test = QUERY(db,
		"SELECT file_id FROM file_uris\n"
		"WHERE uri = ? LIMIT 1");
	sqlite3_bind_text(test, 1, URI, -1, SQLITE_STATIC);
	bool_t exists = SQLITE_ROW == sqlite3_step(test);
	sqlite3_finalize(test); test = NULL;
	EFSRepoDBClose(repo, &db);
	if(exists) return 0;

	fprintf(stderr, "Pulling %s\n", URI);

	err_t err = 0;

	HTTPMessageRef msg = HTTPMessageCreate(conn);
	if(!conn || !msg) {
		HTTPMessageFree(&msg);
		return -1;
	}

	str_t *path;
	asprintf(&path, "/efs/file/%s/%s", algo, hash);
	HTTPMessageWriteRequest(msg, HTTP_GET, path, pull->host);
	FREE(&path);

	HTTPMessageWriteHeader(msg, "Cookie", pull->cookie);
	HTTPMessageBeginBody(msg);
	HTTPMessageEnd(msg);
	uint16_t const status = HTTPMessageGetResponseStatus(msg);
	err = err < 0 ? err : (status < 200 || status >= 300);

	EFSImportHeaders const *const headers = HTTPMessageGetHeaders(msg, EFSImportFields, numberof(EFSImportFields));
	EFSSubmissionRef sub = NULL;
	EFSSubmissionRef meta = NULL;
	err = err < 0 ? err : EFSSubmissionCreatePair(session, headers->content_type, (ssize_t (*)())HTTPMessageGetBuffer, msg, NULL, &sub, &meta);

	if(err >= 0) {
		sqlite3 *db = EFSRepoDBConnect(pull->repo);
		EXEC(QUERY(db, "SAVEPOINT store"));
		if(
			EFSSubmissionStore(sub, db) < 0 ||
			EFSSubmissionStore(meta, db) < 0
		) {
			EXEC(QUERY(db, "ROLLBACK TO store"));
			err = -1;
		}
		EXEC(QUERY(db, "RELEASE store"));
		EFSRepoDBClose(pull->repo, &db);
	}

	EFSSubmissionFree(&sub);
	EFSSubmissionFree(&meta);

	HTTPMessageDrain(msg);
	HTTPMessageFree(&msg);

	return err;
}

