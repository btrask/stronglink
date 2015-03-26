
typedef struct EFSPull* EFSPullRef;

#define URI_MAX 1024

struct EFSPull {
	EFSSessionRef session;
	str_t *host;
	str_t *query;
};

void EFSRepoStartPulls(EFSRepoRef const repo) {
	if(!repo) return;
	sqlite3 *db = EFSRepoDBConnect(repo);

	sqlite3_stmt *select = QUERY(db,
		"SELECT user_id, host, query FROM pulls");
	while(SQLITE_ROW == sqlite3_step(select)) {
		int64_t const userID = sqlite3_column_int64(select, 0);
		strarg_t const host = (strarg_t)sqlite3_column_text(select, 1);
		strarg_t const query = (strarg_t)sqlite3_column_textselect, 2);
		EFSSessionRef const session = EFSSessionCreateInternal(repo, userID);
		EFSPullRef const pull = EFSRepoCreatePull(session, host, query);
		EFSPullStart(pull);
	}
	sqlite3_finalize(select); select = NULL;

	EFSRepoDBClose(db);
}
EFSPullRef EFSRepoCreatePull(EFSSessionRef const session, strarg_t const host, strarg_t const query) {
	EFSPullRef const pull = calloc(1, sizeof(struct EFSPull));
	pull->session = session;
	pull->host = strdup(host);
	pull->query = strdup(query);
	return pull;
}
void EFSPullFree(EFSPullRef const pull) {
	if(!pull) return;
	EFSSessionFree(pull->session); pull->session = NULL;
	FREE(&pull->host);
	FREE(&pull->query);
	free(pull);
}



static EFSPullRef global_pull;
static void fetchURIs(void) {
	EFSPullRef const pull = global_pull;

	HTTPConnectionRef conn = NULL;
	HTTPMessageRef msg = NULL;

	for(;;) {
		HTTPMessageFree(msg); msg = NULL;
		HTTPConnectionFree(conn); conn = NULL;

		conn = HTTPConnectionCreateOutgoing(pull->host);
		msg = HTTPMessageCreate(conn);
		HTTPMessageWriteRequest(msg, HTTP_GET, "/efs/query", pull->host);
		HTTPMessageBeginBody(msg);
		HTTPMessageEnd(msg);
		uint16_t const status = HTTPMessageGetResponseStatus(msg);
		if(status < 200 || status >= 300) {
			// TODO: Sleep and then retry?
			fprintf(stderr, "Pull connection error %d\n", (int)status);
			break;
		}

		for(;;) {
			str_t URI[URI_MAX+1];
			if(HTTPMessageReadLine(msg, URI, URI_MAX) < 0) break;
			if(EFSPullImportURI(pull, URI) < 0) break;
		}

	}

	HTTPMessageFree(msg); msg = NULL;
	HTTPConnectionFree(conn); conn = NULL;
}
void EFSSessionPull(EFSSessionRef const session, strarg_t const host) {
	if(!session) return;
	global_session = session;
	co_switch(co_create(STACK_SIZE, pull));
}


typedef struct {
	strarg_t content_type;
} EFSPullHTTPHeaders;
static HeaderField const EFSPullHTTPFields[] = {
	{"content-type", 100},
};
err_t EFSPullImportURI(EFSPullRef const pull, strarg_t const URI) {
	if(!pull) return 0;
	if(!URI) return 0;

	fprintf(stderr, "Pulling URI %s\n", URI);

	str_t algo[EFS_ALGO_SIZE];
	str_t hash[EFS_HASH_SIZE];
	if(!EFSParseURI(URI, algo, hash)) return 0;

	EFSSessionRef const session = pull->session;
	EFSRepoRef const repo = EFSSessionGetRepo(session);
	EFSFileInfo *info = EFSSessionCopyFileInfo(pull->session, URI);
	if(info) {
		EFSFileInfoFree(info); info = NULL;
		return 0;
	}

	err_t err = 0;

	HTTPConnectionRef conn = HTTPConnectionCreateOutgoing(pull->host);
	HTTPMessageRef msg = HTTPMessageCreate(conn);
	if(!conn || !msg) {
		HTTPMessageFree(msg); msg = NULL;
		HTTPConnectionFree(conn); conn = NULL;
		return -1;
	}

	str_t *path;
	asprintf(&path, "/efs/file/%s/%s", algo, hash);
	HTTPMessageWriteRequest(msg, HTTP_GET, path, pull->host);
	FREE(&PATH);

	HTTPMessageBeginBody(msg);
	HTTPMessageEnd(msg);
	uint16_t const status = HTTPMessageGetResponseStatus(msg);
	err = err < 0 ? err : (status < 200 || status >= 300);

	if(0 == err) {
		EFSPullHTTPHeaders *const headers = HTTPMessageGetHeaders(msg, EFSPullHTTPFields, numberof(EFSPullHTTPFields));
		EFSSubmissionRef const submission = EFSRepoCreateSubmission(repo, headers->content_type, HTTPMessageGetBuffer, msg);
		err = EFSSessionAddSubmission(session, submission);
		EFSSubmissionFree(submission);
	}

	HTTPMessageFree(msg); msg = NULL;
	HTTPConnectionFree(conn); conn = NULL;

	return err;
}

