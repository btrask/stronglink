#include <assert.h>
#include "common.h"
#include "StrongLink.h"
#include "http/HTTPServer.h"
#include "http/HTTPHeaders.h"
#include "http/MultipartForm.h"
#include "http/QueryString.h"

#define QUERY_BATCH_SIZE 50
#define AUTH_FORM_MAX 1023

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
	if(HTTP_POST != method) return -1;
	if(!URIPath(URI, "/efs/auth", NULL)) return -1;

	str_t formdata[AUTH_FORM_MAX+1];
	ssize_t len = HTTPConnectionReadBodyStatic(conn, (byte_t *)formdata, AUTH_FORM_MAX);
	if(UV_EMSGSIZE == len) return 413; // Request Entity Too Large
	if(len < 0) return 500;
	formdata[len] = '\0';

	SLNSessionCacheRef const cache = SLNRepoGetSessionCache(SLNSessionGetRepo(session));
	static strarg_t const fields[] = {
		"user",
		"pass",
		"token", // TODO: CSRF protection
	};
	str_t *values[numberof(fields)] = {};
	QSValuesParse(formdata, values, fields, numberof(fields));
	SLNSessionRef s;
	int rc = SLNSessionCacheCreateSession(cache, values[0], values[1], &s);
	QSValuesCleanup(values, numberof(values));

	if(DB_SUCCESS != rc) return 403;

	str_t *cookie = SLNSessionCopyCookie(s);
	SLNSessionRelease(&s);
	if(!cookie) return 500;

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteSetCookie(conn, cookie, "/", 60 * 60 * 24 * 365);
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

	if(!SLNSessionHasPermission(session, SLN_RDONLY)) return 403;

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

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteContentLength(conn, info->size);
	HTTPConnectionWriteHeader(conn, "Content-Type", info->type);
	HTTPConnectionWriteHeader(conn, "Cache-Control", "max-age=31536000");
	HTTPConnectionWriteHeader(conn, "ETag", "1");
//	HTTPConnectionWriteHeader(conn, "Accept-Ranges", "bytes"); // TODO
	HTTPConnectionWriteHeader(conn, "Content-Security-Policy", "'none'");
	HTTPConnectionWriteHeader(conn, "X-Content-Type-Options", "nosniff");
	HTTPConnectionBeginBody(conn);
	if(HTTP_HEAD != method) {
		HTTPConnectionWriteFile(conn, file);
	}
	HTTPConnectionEnd(conn);

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

	if(!SLNSessionHasPermission(session, SLN_WRONLY)) return 403;

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

	HTTPConnectionWriteResponse(conn, 201, "Created");
	HTTPConnectionWriteHeader(conn, "X-Location", location);
	// TODO: X-Content-Address or something? Or X-Name?
	HTTPConnectionWriteContentLength(conn, 0);
	HTTPConnectionBeginBody(conn);
	HTTPConnectionEnd(conn);
	SLNSubmissionFree(&sub);
	if(rc < 0) return 500;
	return 0;
}

static int getURIs(SLNSessionRef const session, SLNFilterRef const filter, int const dir, uint64_t *const sortID, uint64_t *const fileID, str_t **const URIs, size_t *const count) {
	if(!SLNSessionHasPermission(session, SLN_RDONLY)) return DB_EACCES;
	SLNRepoRef const repo = SLNSessionGetRepo(session);
	int rc = 0;
	DB_env *db = NULL;
	SLNRepoDBOpen(repo, &db);
	DB_txn *txn = NULL;
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(DB_SUCCESS != rc) goto cleanup;

	SLNFilterPrepare(filter, txn);
	SLNFilterSeek(filter, dir, *sortID, *fileID);

	size_t const max = *count;
	size_t i =  0;
	for(; i < max; i++) {
		URIs[i] = SLNFilterCopyNextURI(filter, dir, txn);
		if(!URIs[i]) break;
	}

	SLNFilterCurrent(filter, dir, sortID, fileID);
	*count = i;

cleanup:
	db_txn_abort(txn); txn = NULL;
	SLNRepoDBClose(repo, &db);
	return rc;
}
static int sendURIBatch(SLNSessionRef const session, SLNFilterRef const filter, int const dir, uint64_t *const sortID, uint64_t *const fileID, HTTPConnectionRef const conn) {
	str_t *URIs[QUERY_BATCH_SIZE];
	size_t count = numberof(URIs);
	int rc = getURIs(session, filter, dir, sortID, fileID, URIs, &count);
	if(DB_SUCCESS != rc) return rc;
	if(0 == count) return DB_NOTFOUND;
	rc = 0;
	for(size_t i = 0; i < count; i++) {
		if(rc >= 0) {
			uv_buf_t const parts[] = {
				uv_buf_init((char *)URIs[i], strlen(URIs[i])),
				uv_buf_init("\r\n", 2),
			};
			rc = HTTPConnectionWriteChunkv(conn, parts, numberof(parts));
		}
		FREE(&URIs[i]);
	}
	if(rc < 0) return DB_EIO;
	return DB_SUCCESS;
}
static int POST_query(SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_POST != method && HTTP_GET != method) return -1; // TODO: GET is just for testing
	if(!URIPath(URI, "/efs/query", NULL)) return -1;

	if(!SLNSessionHasPermission(session, SLN_RDONLY)) return 403;
	int rc;

	SLNFilterRef filter = SLNFilterCreate(SLNMetaFileFilterType);
	if(HTTP_POST == method) {
		// TODO: Check Content-Type header for JSON.
		SLNJSONFilterParserRef parser = SLNJSONFilterParserCreate();
		for(;;) {
			uv_buf_t buf[1];
			rc = HTTPConnectionReadBody(conn, buf);
			if(rc < 0) return 400;
			if(0 == buf->len) break;

			SLNJSONFilterParserWrite(parser, (str_t const *)buf->base, buf->len);
		}
		SLNFilterAddFilterArg(filter, SLNJSONFilterParserEnd(parser));
		SLNJSONFilterParserFree(&parser);
	} else {
		SLNFilterAddFilterArg(filter, SLNFilterCreate(SLNAllFilterType));
	}

	// I'm aware that we're abusing HTTP for sending real-time push data.
	// I'd also like to support WebSocket at some point, but this is simpler
	// and frankly probably more widely supported.
	// Note that the protocol doesn't really break even if this data is
	// cached. It DOES break if a proxy tries to buffer the whole response
	// before passing it back to the client. I'd be curious to know whether
	// such proxies still exist in 2015.

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn,
		"Content-Type", "text/uri-list; charset=utf-8");
	HTTPConnectionWriteHeader(conn, "Cache-Control", "no-store");
	HTTPConnectionWriteHeader(conn, "Vary", "*");
	HTTPConnectionBeginBody(conn);

	uint64_t sortID = 0;
	uint64_t fileID = 0;

	for(;;) {
		rc = sendURIBatch(session, filter, +1, &sortID, &fileID, conn);
		fprintf(stderr, "sent batch %s\n", db_strerror(rc));
		if(DB_NOTFOUND == rc) break;
		if(DB_SUCCESS == rc) continue;
		fprintf(stderr, "Query error: %s\n", db_strerror(rc));
		goto cleanup;
	}

	SLNRepoRef const repo = SLNSessionGetRepo(session);
	for(;;) {
		uint64_t const timeout = uv_now(loop)+(1000 * 30);
		int rc = SLNRepoSubmissionWait(repo, sortID, timeout);
		if(UV_ETIMEDOUT == rc) {
			uv_buf_t const parts[] = { uv_buf_init("\r\n", 2) };
			rc = HTTPConnectionWriteChunkv(conn, parts, numberof(parts));
			if(rc < 0) break;
			continue;
		}
		assert(rc >= 0); // TODO: Handle cancellation?

		for(;;) {
			rc = sendURIBatch(session, filter, +1, &sortID, &fileID, conn);
			if(DB_NOTFOUND == rc) break;
			if(DB_SUCCESS == rc) continue;
			fprintf(stderr, "Query error: %s\n", db_strerror(rc));
			goto cleanup;
		}
	}


cleanup:
	HTTPConnectionWriteChunkv(conn, NULL, 0);
	HTTPConnectionEnd(conn);

	SLNFilterFree(&filter);
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

