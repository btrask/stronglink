#define _GNU_SOURCE
#include "EarthFS.h"
#include "async.h"
#include "http/HTTPMessage.h"

#define URI_MAX 1024

struct EFSPull {
	int64_t pullID;
	EFSSessionRef session;
	str_t *host;
	str_t *username;
	str_t *password;
	str_t *cookie;
	str_t *query;
};

static err_t auth(EFSPullRef const pull, HTTPConnectionRef const conn);

EFSPullRef EFSRepoCreatePull(EFSRepoRef const repo, int64_t const pullID, int64_t const userID, strarg_t const host, strarg_t const username, strarg_t const password, strarg_t const cookie, strarg_t const query) {
	EFSPullRef const pull = calloc(1, sizeof(struct EFSPull));
	pull->pullID = pullID;
	pull->session = EFSRepoCreateSessionInternal(repo, userID);
	pull->username = strdup(username);
	pull->password = strdup(password);
	pull->cookie = cookie ? strdup(cookie) : NULL;
	pull->host = strdup(host);
	pull->query = strdup(query);
	return pull;
}
void EFSPullFree(EFSPullRef const pull) {
	if(!pull) return;
	pull->pullID = -1;
	EFSSessionFree(pull->session); pull->session = NULL;
	FREE(&pull->host);
	FREE(&pull->username);
	FREE(&pull->password);
	FREE(&pull->query);
	free(pull);
}

static EFSPullRef pull_arg;
static void pull_thread(void) {
	EFSPullRef const pull = pull_arg;

	HTTPConnectionRef conn = NULL;
	HTTPMessageRef msg = NULL;

	for(;;) {
		if(conn) {
			HTTPMessageFree(msg); msg = NULL;
			HTTPConnectionFree(conn); conn = NULL;
			async_sleep(1000 * 5);
		}

		conn = HTTPConnectionCreateOutgoing(pull->host);

		if(auth(pull, conn) < 0) {
			fprintf(stderr, "Pull auth error\n");
			continue;
		}

		msg = HTTPMessageCreate(conn);
		HTTPMessageWriteRequest(msg, HTTP_GET, "/api/query/latest?count=all", pull->host); // TODO: /efs/query
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
				if(EFSPullImportURI(pull, URI) >= 0) break;
				async_sleep(1000);
			}
		}

	}

	HTTPMessageFree(msg); msg = NULL;
	HTTPConnectionFree(conn); conn = NULL;

	co_terminate();
}
void EFSPullStart(EFSPullRef const pull) {
	if(!pull) return;
	pull_arg = pull;
	async_wakeup(co_create(STACK_SIZE, pull_thread));
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

	HTTPMessageFree(msg); msg = NULL;

	return 0;
}


typedef struct {
	strarg_t content_type;
} EFSImportHeaders;
static HeaderField const EFSImportFields[] = {
	{"content-type", 100},
};
err_t EFSPullImportURI(EFSPullRef const pull, strarg_t const URI) {
	if(!pull) return 0;
	if(!URI) return 0;

	fprintf(stderr, "Pulling URI '%s'\n", URI);

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
	asprintf(&path, "/api/file/best/%s/%s", algo, hash); // TODO: /efs/file
	HTTPMessageWriteRequest(msg, HTTP_GET, path, pull->host);
	FREE(&path);

	HTTPMessageWriteHeader(msg, "Cookie", pull->cookie);
	HTTPMessageBeginBody(msg);
	HTTPMessageEnd(msg);
	uint16_t const status = HTTPMessageGetResponseStatus(msg);
	fprintf(stderr, "Importing %s, %d\n", URI, status);
	err = err < 0 ? err : (status < 200 || status >= 300);

	if(0 == err) {
		EFSImportHeaders const *const headers = HTTPMessageGetHeaders(msg, EFSImportFields, numberof(EFSImportFields));
		EFSSubmissionRef const submission = EFSRepoCreateSubmission(repo, headers->content_type, (ssize_t (*)())HTTPMessageGetBuffer, msg);
		err = EFSSessionAddSubmission(session, submission);
		EFSSubmissionFree(submission);
	}

	HTTPMessageDrain(msg);
	HTTPMessageFree(msg); msg = NULL;
	HTTPConnectionFree(conn); conn = NULL;

	return err;
}

