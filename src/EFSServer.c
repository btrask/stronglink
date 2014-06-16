#define _GNU_SOURCE
#include "common.h"
#include "EarthFS.h"
#include "HTTPServer.h"
#include "QueryString.h"

typedef struct {
	strarg_t content_type;
} EFSHeaders;
static HeaderField const EFSHeaderFields[] = {
	{"content-type", 100},
};
HeaderFieldList const EFSHeaderFieldList = {
	.count = numberof(EFSHeaderFields),
	.items = EFSHeaderFields,
};

EFSSubmissionRef EFSRepoCreateSubmission(EFSRepoRef const repo, HTTPConnectionRef const conn);

static bool_t pathterm(strarg_t const URI, size_t const len) {
	char const x = URI[len];
	return '\0' == x || '?' == x || '#' == x;
}
static EFSMode method2mode(HTTPMethod const method) {
	switch(method) {
		case HTTP_GET:
		case HTTP_HEAD:
			return EFS_RDONLY;
		case HTTP_POST:
		case HTTP_PUT:
			return EFS_RDWR;
		default:
			BTAssert(0, "Unknown method %d", (int)method);
	}
}


static EFSSessionRef auth(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const qs) {
	str_t *user = NULL;
	str_t *pass = NULL;

	strarg_t pos = qs;
	for(;;) {
		size_t flen, vlen;
		if(!QSRead(pos, &flen, &vlen)) break;

		if(substr("u", pos+1, flen-1) && !user) {
			user = strndup(pos+flen+1, vlen-1);
		}
		if(substr("p", pos+1, flen-1) && !pass) {
			pass = strndup(pos+flen+1, vlen-1);
		}

		pos += flen + vlen;
	}

	// TODO: Cookie

	EFSMode const mode = method2mode(method);

	EFSSessionRef const session = EFSRepoCreateSession(repo, user, pass, NULL, mode);
	FREE(&user);
	FREE(&pass);
	return session;
}


typedef struct {
	str_t *path;
	str_t *type;
	size_t size;
	// alternate uris?
	// tags? [we don't actually support tags, we just support links]
	// 	but we support links to tag files... we could have a table for them too
} EFSFileInfo;

EFSFileInfo *EFSSessionCopyInfoForAddress(EFSSessionRef const session, strarg_t const URI); // TODO


static bool getFile(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_GET != method && HTTP_HEAD != method) return false;
	str_t algo[32] = {};
	str_t hash[256] = {};
	size_t pathlen = 0; // TODO: correct type for scanf %n ?
	(void)sscanf(URI, "/efs/file/%31[a-zA-Z0-9.%_-]/%255[a-zA-Z0-9.%_-]%n", algo, hash, &pathlen);
	if(!pathterm(URI, pathlen)) return false;
	EFSSessionRef const session = auth(repo, conn, method, URI+pathlen);
	if(!session) {
		HTTPConnectionSendStatus(conn, 403);
		return true;
	}


	HTTPConnectionSendStatus(conn, 200); // TODO

	return true;
}
static bool postFile(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_POST != method) return false;
	size_t const pathlen = prefix("/efs/file", URI);
	if(!pathlen) return false;
	if(!pathterm(URI, (size_t)pathlen)) return false;
	EFSSessionRef const session = auth(repo, conn, method, URI+pathlen);
	if(!session) {
		HTTPConnectionSendStatus(conn, 403);
		return true;
	}

	EFSHeaders *const headers = HTTPConnectionGetHeaders(conn);
	size_t const tlen = prefix("multipart/form-data; boundary=", headers->content_type);
	if(!tlen) {
		HTTPConnectionSendStatus(conn, 400);
		return true;
	}
	

	EFSSubmissionRef const sub = EFSRepoCreateSubmission(repo, conn);
	EFSSessionAddSubmission(session, sub);
	EFSSubmissionFree(sub);

	return true;
}
static bool postQuery(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_POST != method && HTTP_GET != method) return false; // TODO: Temporarily accept/ignore gets.
	size_t const pathlen = prefix("/efs/query", URI);
	if(!pathlen) return false;
	if(!pathterm(URI, (size_t)pathlen)) return false;
	if(HTTP_GET == method) return true; // TODO: Accept and ignore.
	EFSSessionRef const session = auth(repo, conn, method, URI+pathlen);
	if(!session) {
		HTTPConnectionSendStatus(conn, 403);
		return true;
	}

	// TODO: Check Content-Type header for JSON.

	EFSJSONFilterBuilderRef const builder = EFSJSONFilterBuilderCreate();

	for(;;) {
		byte_t const *buf = NULL;
		ssize_t const len = HTTPConnectionGetBuffer(conn, &buf);
		if(-1 == len) {
			HTTPConnectionSendStatus(conn, 400);
			return true;
		}
		if(!len) break;

		EFSJSONFilterBuilderParse(builder, (str_t const *)buf, len);
	}

	EFSFilterRef const filter = EFSJSONFilterBuilderDone(builder);

	sqlite3 *const db = EFSRepoDBConnect(repo);
	EFSFilterCreateTempTables(db);
	EFSFilterExec(filter, db, 0);
	EFSRepoDBClose(repo, db);

	EFSFilterFree(filter);
	EFSJSONFilterBuilderFree(builder);

	return true;
}


void EFSServerDispatch(EFSRepoRef const repo, HTTPConnectionRef const conn) {
	HTTPMethod const method = HTTPConnectionGetRequestMethod(conn);
	strarg_t const URI = HTTPConnectionGetRequestURI(conn);

	if(getFile(repo, conn, method, URI)) return;
	if(postFile(repo, conn, method, URI)) return;
	if(postQuery(repo, conn, method, URI)) return;

	// TODO: Validate URI (no `..` segments, etc.) and append `index.html` if necessary.
	// Also, don't use a hardcoded path...
	str_t *path = NULL;
	(void)BTErrno(asprintf(&path, "/home/ben/Code/EarthFS-C/build/www/%s", URI));
	HTTPConnectionSendFile(conn, path);
	FREE(&path);
}

