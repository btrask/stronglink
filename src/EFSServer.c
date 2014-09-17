#define _GNU_SOURCE
#include <assert.h>
#include "async.h"
#include "common.h"
#include "EarthFS.h"
#include "http/HTTPServer.h"
#include "http/MultipartForm.h"
#include "http/QueryString.h"

#define QUERY_BATCH_SIZE 50

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

	EFSSubmissionRef sub = EFSSubmissionCreateQuick(session, type, read, context);
	if(sub && EFSSubmissionBatchStore(&sub, 1) >= 0) {
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

static count_t getURIs(EFSSessionRef const session, EFSFilterRef const filter, int const dir, uint64_t *const sortID, uint64_t *const fileID, str_t **const URIs, count_t const max) {
	EFSRepoRef const repo = EFSSessionGetRepo(session);
	EFSConnection const *conn = EFSRepoDBOpen(repo);
	assert(conn);
	MDB_txn *txn = NULL;
	int rc = mdb_txn_begin(conn->env, NULL, MDB_RDONLY, &txn);
	assertf(MDB_SUCCESS == rc, "Database error %s", mdb_strerror(rc));

	EFSFilterPrepare(filter, txn, conn);
	EFSFilterSeek(filter, dir, *sortID, *fileID);

	count_t count = 0;
	while(count < max) {
		str_t *const URI = EFSFilterCopyNextURI(filter, dir, txn, conn);
		if(!URI) break;
		URIs[count++] = URI;
	}

	EFSFilterCurrent(filter, dir, sortID, fileID);

	mdb_txn_abort(txn); txn = NULL;
	EFSRepoDBClose(repo, &conn);
	return count;
}
static void sendURIs(HTTPMessageRef const msg, str_t *const *const URIs, count_t const count) {
	for(index_t i = 0; i < count; ++i) {
		uv_buf_t const parts[] = {
			uv_buf_init((char *)URIs[i], strlen(URIs[i])),
			uv_buf_init("\r\n", 2),
		};
		HTTPMessageWriteChunkv(msg, parts, numberof(parts));
	}
}
static void cleanupURIs(str_t **const URIs, count_t const count) {
	for(index_t i = 0; i < count; ++i) {
		FREE(&URIs[i]);
	}
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
	EFSFilterRef filter = EFSFilterCreate(EFSMetaFileFilterType);

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
		EFSFilterAddFilterArg(filter, EFSJSONFilterParserEnd(parser));
	} else {
		EFSFilterAddFilterArg(filter, EFSFilterCreate(EFSAllFilterType));
	}

	str_t **URIs = malloc(sizeof(str_t *) * QUERY_BATCH_SIZE);
	if(!URIs) {
		HTTPMessageSendStatus(msg, 500);
		EFSFilterFree(&filter);
		EFSJSONFilterParserFree(&parser);
		EFSSessionFree(&session);
		return true;
	}

	HTTPMessageWriteResponse(msg, 200, "OK");
	HTTPMessageWriteHeader(msg, "Transfer-Encoding", "chunked");
	HTTPMessageWriteHeader(msg, "Content-Type", "text/uri-list; charset=utf-8");
	HTTPMessageBeginBody(msg);

	uint64_t sortID = 0;
	uint64_t fileID = 0;

	for(;;) {
		count_t const count = getURIs(session, filter, +1, &sortID, &fileID, URIs, QUERY_BATCH_SIZE);
		sendURIs(msg, URIs, count);
		cleanupURIs(URIs, count);
		if(count < QUERY_BATCH_SIZE) break;
	}


	for(;;) {
		uint64_t const timeout = uv_now(loop)+(1000 * 30);
		bool_t const ready = EFSRepoSubmissionWait(repo, sortID, timeout);
		if(!ready) {
			uv_buf_t const parts[] = { uv_buf_init("\r\n", 2) };
			if(HTTPMessageWriteChunkv(msg, parts, numberof(parts)) < 0) break;
			continue;
		}

		for(;;) {
			count_t const count = getURIs(session, filter, +1, &sortID, &fileID, URIs, QUERY_BATCH_SIZE);
			sendURIs(msg, URIs, count);
			cleanupURIs(URIs, count);
			if(count < QUERY_BATCH_SIZE) break;
		}
	}


	HTTPMessageWriteChunkv(msg, NULL, 0);
	HTTPMessageEnd(msg);

	FREE(&URIs);
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

