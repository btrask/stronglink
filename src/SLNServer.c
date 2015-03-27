#include <assert.h>
#include "common.h"
#include "StrongLink.h"
#include "http/HTTPServer.h"
#include "http/HTTPHeaders.h"
#include "http/MultipartForm.h"
#include "http/QueryString.h"

#define QUERY_BATCH_SIZE 50

// TODO: Get rid of this eventually
typedef struct {
	strarg_t content_type;
	strarg_t content_disposition;
} SLNFormHeaders;
static strarg_t const SLNFormFields[] = {
	"content-type",
	"content-disposition",
};

// TODO: Put this somewhere.
bool URIPath(strarg_t const URI, strarg_t const path, strarg_t *const qs) {
	size_t len = prefix(path, URI);
	if(!len) return false;
	if('/' == URI[len]) len++;
	if('\0' != URI[len] && '?' != URI[len]) return false;
	if(qs) *qs = URI + len;
	return true;
}


// TODO: These methods ought to be built on a public C API because the C API needs to support the same features as the HTTP interface.

static int POST_auth(SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
//	if(HTTP_POST != method) return -1;
	if(!URIPath(URI, "/efs/auth", NULL)) return -1;

	SLNRepoRef const repo = SLNSessionGetRepo(session);
	str_t *cookie = NULL;
	int rc = SLNRepoCookieCreate(repo, "ben", "testing", &cookie); // TODO
	if(0 != rc) {
		FREE(&cookie);
		return 403;
	}

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteSetCookie(conn, "s", cookie, "/", 60 * 60 * 24 * 365);
	HTTPConnectionWriteContentLength(conn, 0);
	HTTPConnectionBeginBody(conn);
	HTTPConnectionEnd(conn);

	FREE(&cookie);
	return 0;
}
static int GET_file(SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;
	int len = 0;
	str_t algo[SLN_ALGO_SIZE];
	str_t hash[SLN_HASH_SIZE];
	algo[0] = '\0';
	hash[0] = '\0';
	sscanf(URI, "/efs/file/" SLN_ALGO_FMT "/" SLN_HASH_FMT "%n", algo, hash, &len);
	if(!algo[0] || !hash[0]) return -1;
	if('/' == URI[len]) len++;
	if('\0' != URI[len] && '?' != URI[len]) return -1;

	str_t fileURI[SLN_URI_MAX];
	int rc = snprintf(fileURI, SLN_URI_MAX, "hash://%s/%s", algo, hash);
	if(rc < 0 || rc >= SLN_URI_MAX) return 500;

	SLNFileInfo info[1];
	rc = SLNSessionGetFileInfo(session, fileURI, info);
	if(rc < 0) switch(rc) { // TODO
		case UV_ECANCELED: return 0;
		case DB_NOTFOUND: return 404;
		default: return 500;
	}

	// TODO: Send other headers
	// Content-Disposition?
	// http://www.ibuildings.com/blog/2013/03/4-http-security-headers-you-should-always-be-using
	// X-Content-Type-Options: nosniff
	// Content-Security-Policy (disable all js and embedded resources?)
	// This requires some real thought because not all browsers support
	// these headers, and because we'd like to let users view "regular"
	// files in-line.
	// Maybe the only truly secure option is to host this stuff from a
	// different domain, but for regular users running home servers, that
	// isn't really an option. Perhaps the best option is to host raw files
	// on an alternate port. That still shares cookies (which is brain-dead)
	// but there are things we can do to minimize that impact.
	HTTPConnectionSendFile(conn, info->path, info->type, info->size);
	SLNFileInfoCleanup(info);
	return 0;
}
static int POST_file(SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_POST != method) return -1;
	int len = 0;
	sscanf(URI, "/efs/file%n", &len);
	if(!len) return -1;
	if('/' == URI[len]) len++;
	if('\0' != URI[len] && '?' != URI[len]) return -1;

	strarg_t const type = HTTPHeadersGet(headers, "Content-Type");
	if(!type) return 400;

	int rc;
	SLNSubmissionRef sub = SLNSubmissionCreate(session, type);
	if(!sub) return 500;
	for(;;) {
		uv_buf_t buf[1] = {};
		rc = HTTPConnectionReadBody(conn, buf);
		if(rc < 0) {
			SLNSubmissionFree(&sub);
			return 0;
		}
		if(0 == buf->len) break;
		rc = SLNSubmissionWrite(sub, (byte_t *)buf->base, buf->len);
		if(rc < 0) {
			SLNSubmissionFree(&sub);
			return 500;
		}
	}
	rc = SLNSubmissionEnd(sub);
	if(rc < 0) {
		SLNSubmissionFree(&sub);
		return 500;
	}
	rc = SLNSubmissionBatchStore(&sub, 1);
	if(rc < 0) {
		SLNSubmissionFree(&sub);
		return 500;
	}
	strarg_t const location = SLNSubmissionGetPrimaryURI(sub);
	if(!location) {
		SLNSubmissionFree(&sub);
		return 500;
	}

	rc = 0;
	rc = rc < 0 ? rc : HTTPConnectionWriteResponse(conn, 201, "Created");
	rc = rc < 0 ? rc : HTTPConnectionWriteHeader(conn, "X-Location", location);
	// TODO: X-Content-Address or something? Or X-Name?
	rc = rc < 0 ? rc : HTTPConnectionWriteContentLength(conn, 0);
	rc = rc < 0 ? rc : HTTPConnectionBeginBody(conn);
	rc = rc < 0 ? rc : HTTPConnectionEnd(conn);
	SLNSubmissionFree(&sub);
	if(rc < 0) return 500;
	return 0;
}

static count_t getURIs(SLNSessionRef const session, SLNFilterRef const filter, int const dir, uint64_t *const sortID, uint64_t *const fileID, str_t **const URIs, count_t const max) {
	SLNRepoRef const repo = SLNSessionGetRepo(session);
	DB_env *db = NULL;
	SLNRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	assertf(DB_SUCCESS == rc, "Database error %s", db_strerror(rc));

	SLNFilterPrepare(filter, txn);
	SLNFilterSeek(filter, dir, *sortID, *fileID);

	count_t count = 0;
	while(count < max) {
		str_t *const URI = SLNFilterCopyNextURI(filter, dir, txn);
		if(!URI) break;
		URIs[count++] = URI;
	}

	SLNFilterCurrent(filter, dir, sortID, fileID);

	db_txn_abort(txn); txn = NULL;
	SLNRepoDBClose(repo, &db);
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
static int POST_query(SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_POST != method && HTTP_GET != method) return -1; // TODO: GET is just for testing
	if(!URIPath(URI, "/efs/query", NULL)) return -1;

	SLNJSONFilterParserRef parser = NULL;
	SLNFilterRef filter = SLNFilterCreate(SLNMetaFileFilterType);

	if(HTTP_POST == method) {
		// TODO: Check Content-Type header for JSON.
		parser = SLNJSONFilterParserCreate();
		for(;;) {
			uv_buf_t buf[1];
			int rc = HTTPConnectionReadBody(conn, buf);
			if(rc < 0) return 400;
			if(!buf->len) break;

			SLNJSONFilterParserWrite(parser, (str_t const *)buf->base, buf->len);
		}
		SLNFilterAddFilterArg(filter, SLNJSONFilterParserEnd(parser));
	} else {
		SLNFilterAddFilterArg(filter, SLNFilterCreate(SLNAllFilterType));
	}

	str_t **URIs = malloc(sizeof(str_t *) * QUERY_BATCH_SIZE);
	if(!URIs) {
		SLNFilterFree(&filter);
		SLNJSONFilterParserFree(&parser);
		return 500;
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


	SLNRepoRef const repo = SLNSessionGetRepo(session);
	for(;;) {
		uint64_t const timeout = uv_now(loop)+(1000 * 30);
		bool const ready = SLNRepoSubmissionWait(repo, sortID, timeout);
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
	SLNFilterFree(&filter);
	SLNJSONFilterParserFree(&parser);
	return 0;
}


int SLNServerDispatch(SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	int rc = -1;
	rc = rc >= 0 ? rc : POST_auth(session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_file(session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : POST_file(session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : POST_query(session, conn, method, URI, headers);
	return rc;
}

