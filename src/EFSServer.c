#define _GNU_SOURCE
#include "common.h"
#include "EarthFS.h"
#include "http/HTTPServer.h"
#include "http/MultipartForm.h"
#include "http/QueryString.h"

typedef struct {
	strarg_t cookie;
	strarg_t content_type;
} EFSHTTPHeaders;
static HeaderField const EFSHTTPFields[] = {
	{"cookie", 100},
	{"content-type", 100},
};
typedef struct {
	strarg_t content_type;
} EFSFormHeaders;
static HeaderField const EFSFormFields[] = {
	{"content-type", 100},
};

// TODO: Put this somewhere.
bool_t URIPath(strarg_t const URI, strarg_t const path, strarg_t *const qs) {
	size_t pathlen = prefix(path, URI);
	if(!pathlen) return false;
	if('/' == URI[pathlen]) ++pathlen;
	if(!pathterm(URI, pathlen)) return false;
	if(qs) *qs = URI + pathlen;
	return true;
}


// TODO: These methods ought to be built on a public C API because the C API needs to support the same features as the HTTP interface.

static bool_t postAuth(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
//	if(HTTP_POST != method) return false;
	if(!URIPath(URI, "/efs/auth", NULL)) return false;

	str_t *cookie = EFSRepoCreateCookie(repo, "ben", "testing"); // TODO
	if(!cookie) {
		HTTPConnectionSendStatus(conn, 403);
		return true;
	}

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteSetCookie(conn, "s", cookie, "/", 60 * 60 * 24 * 365);
	HTTPConnectionWriteContentLength(conn, 0);
	HTTPConnectionBeginBody(conn);
	HTTPConnectionEnd(conn);

	FREE(&cookie);
	return true;
}
static bool_t getFile(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_GET != method && HTTP_HEAD != method) return false;
	str_t algo[32] = {};
	str_t hash[256] = {};
	size_t pathlen = 0; // TODO: correct type for scanf %n ?
	(void)sscanf(URI, "/efs/file/%31[a-zA-Z0-9.-]/%255[a-zA-Z0-9.%_-]%n", algo, hash, &pathlen);
	if(!pathlen) return false;
	if('/' == URI[pathlen]) ++pathlen;
	if(!pathterm(URI, pathlen)) return false;

	EFSHTTPHeaders const *const headers = HTTPConnectionGetHeaders(conn, EFSHTTPFields, numberof(EFSHTTPFields));
	EFSSessionRef const session = EFSRepoCreateSession(repo, headers->cookie);
	if(!session) {
		HTTPConnectionSendStatus(conn, 403);
		return true;
	}

	str_t *fileURI;
	asprintf(&fileURI, "hash://%s/%s", algo, hash); // TODO: decode, normalize, error checking.
	EFSFileInfo *info = EFSSessionCopyFileInfo(session, fileURI);
	FREE(&fileURI);
	if(!info) {
		fprintf(stderr, "Couldn't find hash://%s/%s\n", algo, hash);
		HTTPConnectionSendStatus(conn, 404);
		EFSSessionFree(session);
		return true;
	}

	// TODO: Do we need to send other headers?
	HTTPConnectionSendFile(conn, info->path, info->type, info->size);

	EFSFileInfoFree(info); info = NULL;
	EFSSessionFree(session);
	return true;
}
static bool_t postFile(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_POST != method) return false;
	size_t pathlen = prefix("/efs/file", URI);
	if(!pathlen) return false;
	if('/' == URI[pathlen]) ++pathlen;
	if(!pathterm(URI, (size_t)pathlen)) return false;

	EFSHTTPHeaders const *const headers = HTTPConnectionGetHeaders(conn, EFSHTTPFields, numberof(EFSHTTPFields));
	EFSSessionRef const session = EFSRepoCreateSession(repo, headers->cookie);
	if(!session) {
		HTTPConnectionSendStatus(conn, 403);
		return true;
	}

	strarg_t type;
	ssize_t (*read)();
	void *context;

	MultipartFormRef const form = MultipartFormCreate(conn, headers->content_type, EFSFormFields, numberof(EFSFormFields));
	if(form) {
		FormPartRef const part = MultipartFormGetPart(form);
		if(!part) {
			HTTPConnectionSendStatus(conn, 400);
			EFSSessionFree(session);
			return true;
		}
		EFSFormHeaders const *const formHeaders = FormPartGetHeaders(part);
		type = formHeaders->content_type;
		read = FormPartGetBuffer;
		context = part;
	} else {
		type = headers->content_type;
		read = HTTPConnectionGetBuffer;
		context = conn;
	}

	EFSSubmissionRef const sub = EFSRepoCreateSubmission(repo, type, read, context);
	if(EFSSessionAddSubmission(session, sub) < 0) {
		fprintf(stderr, "Submission error for file type %s\n", type);
		HTTPConnectionSendStatus(conn, 500);
		EFSSubmissionFree(sub);
		EFSSessionFree(session);
		return true;
	}

	HTTPConnectionWriteResponse(conn, 201, "Created"); // TODO: Is this right?
	// TODO: Some of our API clients follow redirects automatically, so it's a bad idea to do it here, but our blog submission form needs a redirect.
//	HTTPConnectionWriteHeader(conn, "Location", "/");
	HTTPConnectionWriteHeader(conn, "X-Location", EFSSubmissionGetPrimaryURI(sub));
	HTTPConnectionWriteContentLength(conn, 0);
	HTTPConnectionBeginBody(conn);
	HTTPConnectionEnd(conn);
//	fprintf(stderr, "POST %s -> %s\n", type, EFSSubmissionGetPrimaryURI(sub));

	EFSSubmissionFree(sub);
	MultipartFormFree(form);
	EFSSessionFree(session);
	return true;
}
static bool_t query(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_POST != method && HTTP_GET != method) return false;
	if(!URIPath(URI, "/efs/query", NULL)) return false;

	EFSHTTPHeaders const *const headers = HTTPConnectionGetHeaders(conn, EFSHTTPFields, numberof(EFSHTTPFields));
	EFSSessionRef const session = EFSRepoCreateSession(repo, headers->cookie);
	if(!session) {
		HTTPConnectionSendStatus(conn, 403);
		return true;
	}

	EFSJSONFilterBuilderRef builder = NULL;
	EFSFilterRef filter = NULL;

	if(HTTP_POST == method) {
		// TODO: Check Content-Type header for JSON.
		builder = EFSJSONFilterBuilderCreate();
		for(;;) {
			byte_t const *buf = NULL;
			ssize_t const len = HTTPConnectionGetBuffer(conn, &buf);
			if(-1 == len) {
				HTTPConnectionSendStatus(conn, 400);
				EFSSessionFree(session);
				return true;
			}
			if(!len) break;

			EFSJSONFilterBuilderParse(builder, (str_t const *)buf, len);
		}
		filter = EFSJSONFilterBuilderDone(builder);
	} else {
		filter = EFSFilterCreate(EFSNoFilter);
	}

	sqlite3 *const db = EFSRepoDBConnect(repo);
	EFSFilterCreateTempTables(db);
	EFSFilterExec(filter, db, 0);
	sqlite3_stmt *const select = QUERY(db,
		"SELECT f.internal_hash\n"
		"FROM files AS f\n"
		"LEFT JOIN results AS r ON (f.file_id = r.file_id)\n"
		"ORDER BY r.sort DESC LIMIT 50");

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	// TODO: Ugh, more stuff to support.
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/uri-list; charset=utf-8");
	HTTPConnectionBeginBody(conn);

	strarg_t const prefix = "hash://sha256/"; // TODO: Handle different types of internal hashes.
	size_t const prefixlen = strlen(prefix);
	while(SQLITE_ROW == sqlite3_step(select)) {
		strarg_t const hash = (strarg_t)sqlite3_column_text(select, 0);
		size_t const hashlen = strlen(hash);
		size_t const total = prefixlen + hashlen + 1;
		HTTPConnectionWriteChunkLength(conn, total);
		uv_buf_t parts[] = {
			uv_buf_init((char *)prefix, prefixlen),
			uv_buf_init((char *)hash, hashlen),
			uv_buf_init("\n", 1),
			uv_buf_init("\r\n", 2),
		};
		HTTPConnectionWritev(conn, parts, numberof(parts));
	}
	sqlite3_finalize(select);

	HTTPConnectionWriteChunkLength(conn, 0);
	HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2);
	HTTPConnectionEnd(conn);

	EFSRepoDBClose(repo, db);

	EFSFilterFree(filter);
	EFSJSONFilterBuilderFree(builder);
	EFSSessionFree(session);
	return true;
}


bool_t EFSServerDispatch(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(postAuth(repo, conn, method, URI)) return true;
	if(getFile(repo, conn, method, URI)) return true;
	if(postFile(repo, conn, method, URI)) return true;
	if(query(repo, conn, method, URI)) return true;
	return false;
}

