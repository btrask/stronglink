#define _GNU_SOURCE
#include <assert.h>
#include "async/async.h"
#include "common.h"
#include "EarthFS.h"
#include "http/HTTPServer.h"
#include "http/HTTPHeaders.h"
#include "http/MultipartForm.h"
#include "http/QueryString.h"

#define QUERY_BATCH_SIZE 50

// TODO: Get rid of this eventually
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
	size_t len = prefix(path, URI);
	if(!len) return false;
	if('/' == URI[len]) len++;
	if('\0' != URI[len] && '?' != URI[len]) return false;
	if(qs) *qs = URI + len;
	return true;
}


// TODO: These methods ought to be built on a public C API because the C API needs to support the same features as the HTTP interface.

static int POST_auth(EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
//	if(HTTP_POST != method) return -1;
	if(!URIPath(URI, "/efs/auth", NULL)) return -1;

	EFSRepoRef const repo = EFSSessionGetRepo(session);
	str_t *cookie = NULL;
	int rc = EFSRepoCookieCreate(repo, "ben", "testing", &cookie); // TODO
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
static int GET_file(EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;
	int len = 0;
	str_t algo[EFS_ALGO_SIZE];
	str_t hash[EFS_HASH_SIZE];
	algo[0] = '\0';
	hash[0] = '\0';
	sscanf(URI, "/efs/file/" EFS_ALGO_FMT "/" EFS_HASH_FMT "%n", algo, hash, &len);
	if(!algo[0] || !hash[0]) return -1;
	if('/' == URI[len]) len++;
	if('\0' != URI[len] && '?' != URI[len]) return -1;

	str_t fileURI[EFS_URI_MAX];
	int rc = snprintf(fileURI, EFS_URI_MAX, "hash://%s/%s", algo, hash);
	if(rc < 0 || rc >= EFS_URI_MAX) return 500;

	EFSFileInfo info[1];
	rc = EFSSessionGetFileInfo(session, fileURI, info);
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
	EFSFileInfoCleanup(info);
	return 0;
}
static int POST_file(EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_POST != method) return -1;
	int len = 0;
	sscanf(URI, "/efs/file%n", &len);
	if(!len) return -1;
	if('/' == URI[len]) len++;
	if('\0' != URI[len] && '?' != URI[len]) return -1;

	strarg_t const type = HTTPHeadersGet(headers, "Content-Type");
	if(!type) return 400;

	int rc;
	EFSSubmissionRef sub = EFSSubmissionCreate(session, type);
	if(!sub) return 500;
	for(;;) {
		uv_buf_t buf[1] = {};
		rc = HTTPConnectionReadBody(conn, buf);
		if(rc < 0) {
			EFSSubmissionFree(&sub);
			return 0;
		}
		if(0 == buf->len) break;
		rc = EFSSubmissionWrite(sub, (byte_t *)buf->base, buf->len);
		if(rc < 0) {
			EFSSubmissionFree(&sub);
			return 500;
		}
	}
	rc = EFSSubmissionEnd(sub);
	if(rc < 0) {
		EFSSubmissionFree(&sub);
		return 500;
	}
	rc = EFSSubmissionBatchStore(&sub, 1);
	if(rc < 0) {
		EFSSubmissionFree(&sub);
		return 500;
	}
	strarg_t const location = EFSSubmissionGetPrimaryURI(sub);
	if(!location) {
		EFSSubmissionFree(&sub);
		return 500;
	}

	rc = 0;
	rc = rc < 0 ? rc : HTTPConnectionWriteResponse(conn, 201, "Created");
	rc = rc < 0 ? rc : HTTPConnectionWriteHeader(conn, "X-Location", location);
	// TODO: X-Content-Address or something? Or X-Name?
	rc = rc < 0 ? rc : HTTPConnectionWriteContentLength(conn, 0);
	rc = rc < 0 ? rc : HTTPConnectionBeginBody(conn);
	rc = rc < 0 ? rc : HTTPConnectionEnd(conn);
	EFSSubmissionFree(&sub);
	if(rc < 0) return 500;
	return 0;
}

static count_t getURIs(EFSSessionRef const session, EFSFilterRef const filter, int const dir, uint64_t *const sortID, uint64_t *const fileID, str_t **const URIs, count_t const max) {
	EFSRepoRef const repo = EFSSessionGetRepo(session);
	DB_env *db = NULL;
	EFSRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	int rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
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
static int POST_query(EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_POST != method && HTTP_GET != method) return -1; // TODO: GET is just for testing
	if(!URIPath(URI, "/efs/query", NULL)) return -1;

	EFSJSONFilterParserRef parser = NULL;
	EFSFilterRef filter = EFSFilterCreate(EFSMetaFileFilterType);

	if(HTTP_POST == method) {
		// TODO: Check Content-Type header for JSON.
		parser = EFSJSONFilterParserCreate();
		for(;;) {
			uv_buf_t buf[1];
			int rc = HTTPConnectionReadBody(conn, buf);
			if(rc < 0) return 400;
			if(!buf->len) break;

			EFSJSONFilterParserWrite(parser, (str_t const *)buf->base, buf->len);
		}
		EFSFilterAddFilterArg(filter, EFSJSONFilterParserEnd(parser));
	} else {
		EFSFilterAddFilterArg(filter, EFSFilterCreate(EFSAllFilterType));
	}

	str_t **URIs = malloc(sizeof(str_t *) * QUERY_BATCH_SIZE);
	if(!URIs) {
		EFSFilterFree(&filter);
		EFSJSONFilterParserFree(&parser);
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


	EFSRepoRef const repo = EFSSessionGetRepo(session);
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
	return 0;
}


int EFSServerDispatch(EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	int rc = -1;
	rc = rc >= 0 ? rc : POST_auth(session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_file(session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : POST_file(session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : POST_query(session, conn, method, URI, headers);
	return rc;
}

