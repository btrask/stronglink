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
static strarg_t const EFSHTTPFields[] = {
	"cookie",
	"content-type",
};
typedef struct {
	strarg_t content_type;
	strarg_t content_disposition;
} EFSFormHeaders;
static strarg_t const EFSFormFields[] = {
	"content-type",
	"content-disposition",
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

static bool_t postAuth(EFSRepoRef const repo, HTTPMessageRef const msg, HTTPMethod const method, strarg_t const URI) {
//	if(HTTP_POST != method) return false;
	if(!URIPath(URI, "/efs/auth", NULL)) return false;

	str_t *cookie = EFSRepoCreateCookie(repo, "ben", "testing"); // TODO
	if(!cookie) {
		HTTPMessageSendStatus(msg, 403);
		return true;
	}

	HTTPMessageWriteResponse(msg, 200, "OK");
	HTTPMessageWriteSetCookie(msg, "s", cookie, "/", 60 * 60 * 24 * 365);
	HTTPMessageWriteContentLength(msg, 0);
	HTTPMessageBeginBody(msg);
	HTTPMessageEnd(msg);

	FREE(&cookie);
	return true;
}
static bool_t getFile(EFSRepoRef const repo, HTTPMessageRef const msg, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_GET != method && HTTP_HEAD != method) return false;
	str_t algo[32] = {};
	str_t hash[256] = {};
	size_t pathlen = 0; // TODO: correct type for scanf %n ?
	(void)sscanf(URI, "/efs/file/%31[a-zA-Z0-9.-]/%255[a-zA-Z0-9.%_-]%n", algo, hash, &pathlen);
	if(!pathlen) return false;
	if('/' == URI[pathlen]) ++pathlen;
	if(!pathterm(URI, pathlen)) return false;

	EFSHTTPHeaders const *const headers = HTTPMessageGetHeaders(msg, EFSHTTPFields, numberof(EFSHTTPFields));
	EFSSessionRef session = EFSRepoCreateSession(repo, headers->cookie);
	if(!session) {
		HTTPMessageSendStatus(msg, 403);
		return true;
	}

	str_t *fileURI = EFSFormatURI(algo, hash);
	EFSFileInfo info;
	if(EFSSessionGetFileInfo(session, fileURI, &info) < 0) {
		fprintf(stderr, "Couldn't find %s", fileURI);
		FREE(&fileURI);
		HTTPMessageSendStatus(msg, 404);
		EFSSessionFree(&session);
		return true;
	}
	FREE(&fileURI);

	// TODO: Do we need to send other headers?
	HTTPMessageSendFile(msg, info.path, info.type, info.size);

	EFSFileInfoCleanup(&info);
	EFSSessionFree(&session);
	return true;
}
static bool_t postFile(EFSRepoRef const repo, HTTPMessageRef const msg, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_POST != method) return false;
	if(!URIPath(URI, "/efs/file", NULL)) return false;

	EFSHTTPHeaders const *const headers = HTTPMessageGetHeaders(msg, EFSHTTPFields, numberof(EFSHTTPFields));
	EFSSessionRef session = EFSRepoCreateSession(repo, headers->cookie);
	if(!session) {
		HTTPMessageSendStatus(msg, 403);
		return true;
	}

	// TODO: CSRF token

	strarg_t type;
	ssize_t (*read)();
	void *context;

	MultipartFormRef form = MultipartFormCreate(msg, headers->content_type, EFSFormFields, numberof(EFSFormFields));
	if(form) {
		FormPartRef const part = MultipartFormGetPart(form);
		if(!part) {
			HTTPMessageSendStatus(msg, 400);
			EFSSessionFree(&session);
			return true;
		}
		EFSFormHeaders const *const formHeaders = FormPartGetHeaders(part);
		type = formHeaders->content_type;
		read = FormPartGetBuffer;
		context = part;
	} else {
		type = headers->content_type;
		read = HTTPMessageGetBuffer;
		context = msg;
	}

	EFSSubmissionRef sub = EFSSubmissionCreateAndAdd(session, type, read, context);
	if(sub) {
		HTTPMessageWriteResponse(msg, 201, "Created");
		HTTPMessageWriteHeader(msg, "X-Location", EFSSubmissionGetPrimaryURI(sub)); // TODO: X-Content-Address or something? Or X-Name?
		HTTPMessageWriteContentLength(msg, 0);
		HTTPMessageBeginBody(msg);
		HTTPMessageEnd(msg);
//		fprintf(stderr, "POST %s -> %s\n", type, EFSSubmissionGetPrimaryURI(sub));
	} else {
		fprintf(stderr, "Submission error for file type %s\n", type);
		HTTPMessageSendStatus(msg, 500);
	}

	EFSSubmissionFree(&sub);
	MultipartFormFree(&form);
	EFSSessionFree(&session);
	return true;
}
static bool_t query(EFSRepoRef const repo, HTTPMessageRef const msg, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_POST != method && HTTP_GET != method) return false;
	if(!URIPath(URI, "/efs/query", NULL)) return false;

	EFSHTTPHeaders const *const headers = HTTPMessageGetHeaders(msg, EFSHTTPFields, numberof(EFSHTTPFields));
	EFSSessionRef session = EFSRepoCreateSession(repo, headers->cookie);
	if(!session) {
		HTTPMessageSendStatus(msg, 403);
		return true;
	}

	EFSJSONFilterParserRef parser = NULL;
	EFSFilterRef filter = NULL;

	if(HTTP_POST == method) {
		// TODO: Check Content-Type header for JSON.
		parser = EFSJSONFilterParserCreate();
		for(;;) {
			byte_t const *buf = NULL;
			ssize_t const len = HTTPMessageGetBuffer(msg, &buf);
			if(-1 == len) {
				HTTPMessageSendStatus(msg, 400);
				EFSSessionFree(&session);
				return true;
			}
			if(!len) break;

			EFSJSONFilterParserWrite(parser, (str_t const *)buf, len);
		}
		filter = EFSJSONFilterParserEnd(parser);
	} else {
		filter = EFSFilterCreate(EFSNoFilterType);
	}

	// TODO: Use EFSSessionCreateFilteredURIList? Streaming version?
	HTTPMessageSendStatus(msg, 500);
/*	sqlite3f *db = EFSRepoDBConnect(repo);
	EFSFilterCreateTempTables(db);
	EFSFilterExec(filter, db, 0);
	sqlite3_stmt *const select = QUERY(db,
		"SELECT f.internal_hash\n"
		"FROM files AS f\n"
		"INNER JOIN results AS r ON (f.file_id = r.file_id)\n"
		"ORDER BY r.sort DESC LIMIT 50");

	HTTPMessageWriteResponse(msg, 200, "OK");
	HTTPMessageWriteHeader(msg, "Transfer-Encoding", "chunked");
	// TODO: Ugh, more stuff to support.
	HTTPMessageWriteHeader(msg, "Content-Type", "text/uri-list; charset=utf-8");
	HTTPMessageBeginBody(msg);

	while(SQLITE_ROW == STEP(select)) {
		strarg_t const hash = (strarg_t)sqlite3_column_text(select, 0);
		str_t *URI = EFSFormatURI(EFS_INTERNAL_ALGO, hash);
		uv_buf_t const parts[] = {
			uv_buf_init((char *)URI, strlen(URI)),
			uv_buf_init("\n", 1),
			uv_buf_init("\r\n", 2),
		};
		HTTPMessageWritev(msg, parts, numberof(parts));
	}
	sqlite3f_finalize(select);

	HTTPMessageWriteChunkLength(msg, 0);
	HTTPMessageWrite(msg, (byte_t const *)"\r\n", 2);
	HTTPMessageEnd(msg);

	EFSRepoDBClose(repo, &db);*/

	EFSFilterFree(&filter);
	EFSJSONFilterParserFree(&parser);
	EFSSessionFree(&session);
	return true;
}


bool_t EFSServerDispatch(EFSRepoRef const repo, HTTPMessageRef const msg, HTTPMethod const method, strarg_t const URI) {
	if(postAuth(repo, msg, method, URI)) return true;
	if(getFile(repo, msg, method, URI)) return true;
	if(postFile(repo, msg, method, URI)) return true;
	if(query(repo, msg, method, URI)) return true;
	return false;
}

