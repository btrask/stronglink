#define _GNU_SOURCE
#include <assert.h>
#include "async/async.h"
#include "common.h"
#include "EarthFS.h"
#include "http/HTTPServer.h"
#include "http/MultipartForm.h"
#include "http/QueryString.h"

#define QUERY_BATCH_SIZE 50

typedef struct {
	strarg_t content_type;
	strarg_t content_disposition;
} EFSFormHeaders;
static strarg_t const EFSFormFields[] = {
	"content-type",
	"content-disposition",
};

// TODO: Put this somewhere.
bool URIPath(strarg_t const URI, strarg_t const path, strarg_t *const qs) {
	size_t pathlen = prefix(path, URI);
	if(!pathlen) return false;
	if('/' == URI[pathlen]) ++pathlen;
	if(!pathterm(URI, pathlen)) return false;
	if(qs) *qs = URI + pathlen;
	return true;
}


// TODO: These methods ought to be built on a public C API because the C API needs to support the same features as the HTTP interface.

static bool postAuth(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
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
static bool getFile(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_GET != method && HTTP_HEAD != method) return false;
	str_t algo[32] = {};
	str_t hash[256] = {};
	int pathlen = 0;
	(void)sscanf(URI, "/efs/file/%31[a-zA-Z0-9.-]/%255[a-zA-Z0-9.%_-]%n", algo, hash, &pathlen);
	if(!pathlen) return false;
	if('/' == URI[pathlen]) ++pathlen;
	if(!pathterm(URI, pathlen)) return false;

	static str_t const fields[][FIELD_MAX] = {
		"cookie",
		"content-type",
	};
	str_t headers[numberof(fields)][VALUE_MAX];
	int rc = HTTPConnectionReadHeaders(conn, headers, fields, numberof(fields), NULL);
	if(rc < 0) {
		HTTPConnectionSendStatus(conn, 400);
		return true;
	}
	EFSSessionRef session = EFSRepoCreateSession(repo, headers[0]);
	if(!session) {
		HTTPConnectionSendStatus(conn, 403);
		return true;
	}

	str_t *fileURI = EFSFormatURI(algo, hash);
	EFSFileInfo info;
	if(EFSSessionGetFileInfo(session, fileURI, &info) < 0) {
		fprintf(stderr, "Couldn't find %s", fileURI);
		FREE(&fileURI);
		HTTPConnectionSendStatus(conn, 404);
		EFSSessionFree(&session);
		return true;
	}
	FREE(&fileURI);

	// TODO: Do we need to send other headers?
	HTTPConnectionSendFile(conn, info.path, info.type, info.size);

	EFSFileInfoCleanup(&info);
	EFSSessionFree(&session);
	return true;
}
static bool postFile(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	return false; // TODO

/*	if(HTTP_POST != method) return false;
	if(!URIPath(URI, "/efs/file", NULL)) return false;

	static str_t const fields[][FIELD_MAX] = {
		"cookie",
		"content-type",
	};
	str_t headers[numberof(fields)][VALUE_MAX];
	int rc = HTTPConnectionReadHeaders(conn, headers, fields, numberof(fields), NULL);
	if(rc < 0) {
		HTTPConnectionSendStatus(conn, 400);
		return true;
	}
	EFSSessionRef session = EFSRepoCreateSession(repo, headers[0]);
	if(!session) {
		HTTPConnectionSendStatus(conn, 403);
		return true;
	}

	// TODO: CSRF token

	strarg_t type;
	ssize_t (*read)();
	void *context;

	MultipartFormRef form = MultipartFormCreate(conn, headers[1], EFSFormFields, numberof(EFSFormFields));
	if(form) {
		FormPartRef const part = MultipartFormGetPart(form);
		if(!part) {
			HTTPConnectionSendStatus(conn, 400);
			EFSSessionFree(&session);
			return true;
		}
		EFSFormHeaders const *const formHeaders = FormPartGetHeaders(part);
		type = formHeaders->content_type;
		read = FormPartGetBuffer;
		context = part;
	} else {
		type = headers[1];
		read = HTTPConnectionGetBuffer;
		context = conn;
	}

	EFSSubmissionRef sub = EFSSubmissionCreateQuick(session, type, read, context);
	if(sub && EFSSubmissionBatchStore(&sub, 1) >= 0) {
		HTTPConnectionWriteResponse(conn, 201, "Created");
		HTTPConnectionWriteHeader(conn, "X-Location", EFSSubmissionGetPrimaryURI(sub)); // TODO: X-Content-Address or something? Or X-Name?
		HTTPConnectionWriteContentLength(conn, 0);
		HTTPConnectionBeginBody(conn);
		HTTPConnectionEnd(conn);
//		fprintf(stderr, "POST %s -> %s\n", type, EFSSubmissionGetPrimaryURI(sub));
	} else {
		fprintf(stderr, "Submission error for file type %s\n", type);
		HTTPConnectionSendStatus(conn, 500);
	}

	EFSSubmissionFree(&sub);
	MultipartFormFree(&form);
	EFSSessionFree(&session);
	return true;*/
}

static count_t getURIs(EFSSessionRef const session, EFSFilterRef const filter, int const dir, uint64_t *const sortID, uint64_t *const fileID, str_t **const URIs, count_t const max) {
	EFSRepoRef const repo = EFSSessionGetRepo(session);
	DB_env *db = NULL;
	int rc = EFSRepoDBOpen(repo, &db);
	assert(rc >= 0);
	DB_txn *txn = NULL;
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	assertf(DB_SUCCESS == rc, "Database error %s", db_strerror(rc));

	EFSFilterPrepare(filter, txn);
	EFSFilterSeek(filter, dir, *sortID, *fileID);

	count_t count = 0;
	while(count < max) {
		str_t *const URI = EFSFilterCopyNextURI(filter, dir, txn);
		if(!URI) break;
		URIs[count++] = URI;
	}

	EFSFilterCurrent(filter, dir, sortID, fileID);

	db_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(repo, &db);
	return count;
}
static void sendURIs(HTTPConnectionRef const conn, str_t *const *const URIs, count_t const count) {
	for(index_t i = 0; i < count; ++i) {
		uv_buf_t const parts[] = {
			uv_buf_init((char *)URIs[i], strlen(URIs[i])),
			uv_buf_init("\r\n", 2),
		};
		HTTPConnectionWriteChunkv(conn, parts, numberof(parts));
	}
}
static void cleanupURIs(str_t **const URIs, count_t const count) {
	for(index_t i = 0; i < count; ++i) {
		FREE(&URIs[i]);
	}
}
static bool query(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_POST != method && HTTP_GET != method) return false;
	if(!URIPath(URI, "/efs/query", NULL)) return false;

	static str_t const fields[][FIELD_MAX] = {
		"cookie",
		"content-type",
	};
	str_t headers[numberof(fields)][VALUE_MAX];
	int rc = HTTPConnectionReadHeaders(conn, headers, fields, numberof(fields), NULL);
	if(rc < 0) {
		HTTPConnectionSendStatus(conn, 400);
		return true;
	}
	EFSSessionRef session = EFSRepoCreateSession(repo, headers[0]);
	if(!session) {
		HTTPConnectionSendStatus(conn, 403);
		return true;
	}

	EFSJSONFilterParserRef parser = NULL;
	EFSFilterRef filter = EFSFilterCreate(EFSMetaFileFilterType);

	if(HTTP_POST == method) {
		// TODO: Check Content-Type header for JSON.
		parser = EFSJSONFilterParserCreate();
		for(;;) {
			uv_buf_t buf[1];
			int rc = HTTPConnectionReadBody(conn, buf, NULL);
			if(rc < 0) {
				HTTPConnectionSendStatus(conn, 400);
				EFSSessionFree(&session);
				return true;
			}
			if(!buf->len) break;

			EFSJSONFilterParserWrite(parser, (str_t const *)buf->base, buf->len);
		}
		EFSFilterAddFilterArg(filter, EFSJSONFilterParserEnd(parser));
	} else {
		EFSFilterAddFilterArg(filter, EFSFilterCreate(EFSAllFilterType));
	}

	str_t **URIs = malloc(sizeof(str_t *) * QUERY_BATCH_SIZE);
	if(!URIs) {
		HTTPConnectionSendStatus(conn, 500);
		EFSFilterFree(&filter);
		EFSJSONFilterParserFree(&parser);
		EFSSessionFree(&session);
		return true;
	}

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/uri-list; charset=utf-8");
	HTTPConnectionBeginBody(conn);

	uint64_t sortID = 0;
	uint64_t fileID = 0;

	for(;;) {
		count_t const count = getURIs(session, filter, +1, &sortID, &fileID, URIs, QUERY_BATCH_SIZE);
		sendURIs(conn, URIs, count);
		cleanupURIs(URIs, count);
		if(count < QUERY_BATCH_SIZE) break;
	}


	for(;;) {
		uint64_t const timeout = uv_now(loop)+(1000 * 30);
		bool const ready = EFSRepoSubmissionWait(repo, sortID, timeout);
		if(!ready) {
			uv_buf_t const parts[] = { uv_buf_init("\r\n", 2) };
			if(HTTPConnectionWriteChunkv(conn, parts, numberof(parts)) < 0) break;
			continue;
		}

		for(;;) {
			count_t const count = getURIs(session, filter, +1, &sortID, &fileID, URIs, QUERY_BATCH_SIZE);
			sendURIs(conn, URIs, count);
			cleanupURIs(URIs, count);
			if(count < QUERY_BATCH_SIZE) break;
		}
	}


	HTTPConnectionWriteChunkv(conn, NULL, 0);
	HTTPConnectionEnd(conn);

	FREE(&URIs);
	EFSFilterFree(&filter);
	EFSJSONFilterParserFree(&parser);
	EFSSessionFree(&session);
	return true;
}


bool EFSServerDispatch(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(postAuth(repo, conn, method, URI)) return true;
	if(getFile(repo, conn, method, URI)) return true;
	if(postFile(repo, conn, method, URI)) return true;
	if(query(repo, conn, method, URI)) return true;
	return false;
}

