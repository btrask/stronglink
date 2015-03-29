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
	SLNMode mode = 0;
	int rc = SLNRepoCookieCreate(repo, "ben", "testing", &cookie, &mode); // TODO
	if(DB_SUCCESS != rc) {
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

	// TODO: Check for conditional get headers and return 304 Not Modified.

	str_t fileURI[SLN_URI_MAX];
	int rc = snprintf(fileURI, SLN_URI_MAX, "hash://%s/%s", algo, hash);
	if(rc < 0 || rc >= SLN_URI_MAX) return 500;

	SLNFileInfo info[1];
	rc = SLNSessionGetFileInfo(session, fileURI, info);
	if(DB_NOTFOUND == rc) return 404;
	if(DB_SUCCESS != rc) return 500;

	uv_file file = async_fs_open(info->path, O_RDONLY, 0000);
	if(UV_ENOENT == file) {
		SLNFileInfoCleanup(info);
		return 410; // Gone
	}
	if(file < 0) {
		SLNFileInfoCleanup(info);
		return 500;
	}

	// TODO: Hosting untrusted data is really hard.
	// The same origin policy isn't doing us any favors, because most
	// simple installations won't have a second domain to use.
	// - For loopback, we can use an alternate loopback IP, but that
	//   still only covers a limited range of use cases.
	// - We would really like suborigins, if they're ever widely supported.
	//   <http://www.chromium.org/developers/design-documents/per-page-suborigins>
	// - We could report untrusted types as "text/plain" or
	//   "application/binary", which would be inconvenient but
	//   very reliable.

	// TODO: Use Content-Disposition to suggest a filename, for file types
	// that aren't useful to view inline.

	rc=0;
	rc=rc<0?rc: HTTPConnectionWriteResponse(conn, 200, "OK");
	rc=rc<0?rc: HTTPConnectionWriteContentLength(conn, info->size);
	rc=rc<0?rc: HTTPConnectionWriteHeader(conn, "Content-Type", info->type);
	rc=rc<0?rc: HTTPConnectionWriteHeader(conn, "Cache-Control", "max-age=31536000");
	rc=rc<0?rc: HTTPConnectionWriteHeader(conn, "ETag", "1");
//	rc=rc<0?rc: HTTPConnectionWriteHeader(conn, "Accept-Ranges", "bytes"); // TODO
	rc=rc<0?rc: HTTPConnectionWriteHeader(conn, "Content-Security-Policy", "'none'");
	rc=rc<0?rc: HTTPConnectionWriteHeader(conn, "X-Content-Type-Options", "nosniff");
	rc=rc<0?rc: HTTPConnectionBeginBody(conn);
	if(HTTP_HEAD != method) {
		rc=rc<0?rc: HTTPConnectionWriteFile(conn, file);
	}
	rc=rc<0?rc: HTTPConnectionEnd(conn);

	SLNFileInfoCleanup(info);
	async_fs_close(file);
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
	int rc;

	if(HTTP_POST == method) {
		// TODO: Check Content-Type header for JSON.
		parser = SLNJSONFilterParserCreate();
		for(;;) {
			uv_buf_t buf[1];
			rc = HTTPConnectionReadBody(conn, buf);
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

	// I'm aware that we're abusing HTTP for sending real-time push data.
	// I'd also like to support WebSocket at some point, but this is simpler
	// and frankly probably more widely supported.
	// Note that the protocol doesn't really break even if this data is
	// cached. It DOES break if a proxy tries to buffer the whole response
	// before passing it back to the client. I'd be curious to know whether
	// such proxies still exist in 2015.

	rc=0;
	rc=rc<0?rc: HTTPConnectionWriteResponse(conn, 200, "OK");
	rc=rc<0?rc: HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	rc=rc<0?rc: HTTPConnectionWriteHeader(conn,
		"Content-Type", "text/uri-list; charset=utf-8");
	rc=rc<0?rc: HTTPConnectionWriteHeader(conn, "Cache-Control", "no-store");
	rc=rc<0?rc: HTTPConnectionWriteHeader(conn, "Vary", "*");
	rc=rc<0?rc: HTTPConnectionBeginBody(conn);

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
		int rc = SLNRepoSubmissionWait(repo, sortID, timeout);
		if(UV_ETIMEDOUT == rc) {
			uv_buf_t const parts[] = { uv_buf_init("\r\n", 2) };
			if(HTTPConnectionWriteChunkv(conn, parts, numberof(parts)) < 0) break;
			continue;
		}
		assert(rc >= 0); // TODO: Handle cancellation?

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

