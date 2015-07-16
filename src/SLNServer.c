// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include "common.h"
#include "StrongLink.h"
#include "http/HTTPServer.h"
#include "http/HTTPHeaders.h"
#include "http/MultipartForm.h"
#include "http/QueryString.h"

#define QUERY_BATCH_SIZE 50
#define AUTH_FORM_MAX (1023+1)

// TODO: Put this somewhere.
bool URIPath(strarg_t const URI, strarg_t const path, strarg_t *const qs) {
	size_t len = prefix(path, URI);
	if(!len) return false;
	// TODO: It turns out /path and /path/ are different.
	// We should eventually redirect.
//	if('/' == URI[len]) len++;
	if('\0' != URI[len] && '?' != URI[len]) return false;
	if(qs) *qs = URI + len;
	return true;
}


// TODO: Some sort of token-based API auth system, like OAuth?
/*static int POST_auth(SLNRepoRef const repo, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_POST != method) return -1;
	if(!URIPath(URI, "/sln/auth", NULL)) return -1;

	str_t formdata[AUTH_FORM_MAX];
	ssize_t len = HTTPConnectionReadBodyStatic(conn, (byte_t *)formdata, sizeof(formdata)-1);
	if(UV_EMSGSIZE == len) return 413; // Request Entity Too Large
	if(len < 0) return 500;
	formdata[len] = '\0';

	SLNSessionCacheRef const cache = SLNRepoGetSessionCache(repo);
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

	if(rc < 0) return 403;

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
}*/
static int GET_file(SLNRepoRef const repo, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;
	int len = 0;
	str_t algo[SLN_ALGO_SIZE];
	str_t hash[SLN_HASH_SIZE];
	algo[0] = '\0';
	hash[0] = '\0';
	sscanf(URI, "/sln/file/" SLN_ALGO_FMT "/" SLN_HASH_FMT "%n", algo, hash, &len);
	if(!algo[0] || !hash[0]) return -1;
	if('\0' != URI[len] && '?' != URI[len]) return -1;

	// TODO: Check for conditional get headers and return 304 Not Modified.

	str_t fileURI[SLN_URI_MAX];
	int rc = snprintf(fileURI, sizeof(fileURI), "hash://%s/%s", algo, hash);
	if(rc < 0 || rc >= sizeof(fileURI)) return 500;

	SLNFileInfo info[1];
	rc = SLNSessionGetFileInfo(session, fileURI, info);
	if(DB_EACCES == rc) return 403;
	if(DB_NOTFOUND == rc) return 404;
	if(rc < 0) return 500;

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
//	HTTPConnectionWriteHeader(conn, "Vary", "Accept, Accept-Ranges");
	// TODO: Double check Vary header syntax.
	// Also do we need to change the ETag?
	HTTPConnectionBeginBody(conn);
	if(HTTP_HEAD != method) {
		HTTPConnectionWriteFile(conn, file);
	}
	HTTPConnectionEnd(conn);

	SLNFileInfoCleanup(info);
	async_fs_close(file);
	return 0;
}
static int GET_meta(SLNRepoRef const repo, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	// TODO: This is pretty much copy and pasted from above.
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;
	int len = 0;
	str_t algo[SLN_ALGO_SIZE];
	str_t hash[SLN_HASH_SIZE];
	algo[0] = '\0';
	hash[0] = '\0';
	sscanf(URI, "/sln/meta/" SLN_ALGO_FMT "/" SLN_HASH_FMT "%n", algo, hash, &len);
	if(!algo[0] || !hash[0]) return -1;
	if('\0' != URI[len] && '?' != URI[len]) return -1;

	// TODO
	return 501; // Not Implemented
}

static void created(strarg_t const URI, HTTPConnectionRef const conn) {
	HTTPConnectionWriteResponse(conn, 201, "Created");
	HTTPConnectionWriteHeader(conn, "X-Location", URI);
	// TODO: X-Content-Address or something? Or X-Name?
	HTTPConnectionWriteContentLength(conn, 0);
	HTTPConnectionBeginBody(conn);
	HTTPConnectionEnd(conn);
}
static int accept_sub(SLNSessionRef const session, strarg_t const knownURI, HTTPConnectionRef const conn, HTTPHeadersRef const headers) {
	strarg_t const type = HTTPHeadersGet(headers, "Content-Type");
	if(!type) return 415; // Unsupported Media Type

	SLNSubmissionRef sub = NULL;
	int rc = SLNSubmissionCreate(session, knownURI, type, &sub);
	if(rc < 0) goto cleanup;
	for(;;) {
		uv_buf_t buf[1] = {};
		rc = HTTPConnectionReadBody(conn, buf);
		if(rc < 0) goto cleanup;
		if(0 == buf->len) break;
		rc = SLNSubmissionWrite(sub, (byte_t const *)buf->base, buf->len);
		if(rc < 0) goto cleanup;
	}
	rc = SLNSubmissionEnd(sub);
	if(rc < 0) goto cleanup;
	rc = SLNSubmissionStoreBatch(&sub, 1);
	if(rc < 0) goto cleanup;
	strarg_t const location = SLNSubmissionGetPrimaryURI(sub);
	if(!location) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	created(location, conn);

cleanup:
	SLNSubmissionFree(&sub);
	if(UV_EACCES == rc) return 403;
	if(SLN_HASHMISMATCH == rc) return 409; // Conflict
	if(rc < 0) return 500;
	return 0;
}
static int POST_file(SLNRepoRef const repo, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_POST != method) return -1;
	int len = 0;
	sscanf(URI, "/sln/file%n", &len);
	if(!len) return -1;
	if('\0' != URI[len] && '?' != URI[len]) return -1;

	return accept_sub(session, NULL, conn, headers);
}
static int PUT_file(SLNRepoRef const repo, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	// TODO: This is pretty much copy and pasted from above.
	if(HTTP_PUT != method) return -1;
	int len = 0;
	str_t algo[SLN_ALGO_SIZE];
	str_t hash[SLN_HASH_SIZE];
	algo[0] = '\0';
	hash[0] = '\0';
	sscanf(URI, "/sln/file/" SLN_ALGO_FMT "/" SLN_HASH_FMT "%n", algo, hash, &len);
	if(!algo[0] || !hash[0]) return -1;
	if('\0' != URI[len] && '?' != URI[len]) return -1;

	str_t *knownURI = SLNFormatURI(algo, hash);
	if(!knownURI) return 500;

	int rc = SLNSessionGetFileInfo(session, knownURI, NULL);
	if(rc >= 0) {
		created(knownURI, conn); // TODO: Don't return 201 if the file already exists?
		FREE(&knownURI);
		return 0;
	}
	if(DB_NOTFOUND != rc) {
		FREE(&knownURI);
		return 500;
	}

	rc = accept_sub(session, knownURI, conn, headers);
	FREE(&knownURI);
	return rc;
}

static void sendURIList(SLNSessionRef const session, SLNFilterRef const filter, strarg_t const qs, bool const meta, HTTPConnectionRef const conn) {
	SLNFilterPosition pos = { .dir = +1 };
	uint64_t count = UINT64_MAX;
	bool wait = true;
	SLNFilterParseOptions(qs, &pos, &count, NULL, &wait);

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

	int rc = SLNFilterWriteURIs(filter, session, &pos, meta, count, wait, (SLNFilterWriteCB)HTTPConnectionWriteChunkv, conn);
	if(rc < 0) {
		fprintf(stderr, "Query response error %s\n", sln_strerror(rc));
	}

	HTTPConnectionWriteChunkEnd(conn);
	HTTPConnectionEnd(conn);
	SLNFilterPositionCleanup(&pos);
}
static int parseFilter(SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, HTTPHeadersRef const headers, SLNFilterRef *const out) {
	assert(HTTP_POST == method);
	// TODO: Check Content-Type header for JSON.
	SLNJSONFilterParserRef parser;
	int rc = SLNJSONFilterParserCreate(session, &parser);
	if(rc < 0) return rc;
	for(;;) {
		uv_buf_t buf[1];
		rc = HTTPConnectionReadBody(conn, buf);
		if(rc < 0) {
			SLNJSONFilterParserFree(&parser);
			return DB_ENOMEM;
		}
		if(0 == buf->len) break;

		SLNJSONFilterParserWrite(parser, (str_t const *)buf->base, buf->len);
	}
	*out = SLNJSONFilterParserEnd(parser);
	SLNJSONFilterParserFree(&parser);
	if(!*out) return DB_ENOMEM;
	return 0;
}
static int GET_query(SLNRepoRef const repo, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;
	strarg_t qs;
	if(!URIPath(URI, "/sln/query", &qs)) return -1;

	SLNFilterRef filter = NULL;
	int rc;

	static strarg_t const fields[] = { "q" };
	str_t *values[numberof(fields)] = {};
	QSValuesParse(qs, values, fields, numberof(fields));
	rc = SLNUserFilterParse(session, values[0], &filter);
	QSValuesCleanup(values, numberof(values));
	if(DB_EINVAL == rc) rc = SLNFilterCreate(session, SLNVisibleFilterType, &filter);
	if(DB_EACCES == rc) return 403;
	if(rc < 0) return 500;

	sendURIList(session, filter, qs, false, conn);
	SLNFilterFree(&filter);
	return 0;
}
static int POST_query(SLNRepoRef const repo, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_POST != method) return -1;
	strarg_t qs;
	if(!URIPath(URI, "/sln/query", &qs)) return -1;

	SLNFilterRef filter;
	int rc = parseFilter(session, conn, method, headers, &filter);
	if(DB_EACCES == rc) return 403;
	if(rc < 0) return 500;
	sendURIList(session, filter, qs, false, conn);
	SLNFilterFree(&filter);
	return 0;
}
static int GET_metafiles(SLNRepoRef const repo, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method) return -1;
	strarg_t qs;
	if(!URIPath(URI, "/sln/metafiles", &qs)) return -1;

	SLNFilterRef filter;
	int rc = SLNFilterCreate(session, SLNMetaFileFilterType, &filter);
	if(DB_EACCES == rc) return 403;
	if(rc < 0) return 500;
	sendURIList(session, filter, qs, true, conn);
	SLNFilterFree(&filter);
	return 0;
}
// TODO: Remove this once we rewrite the sync system not to need it.
static int GET_query_obsolete(SLNRepoRef const repo, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method) return -1;
	strarg_t qs;
	if(!URIPath(URI, "/sln/query-obsolete", &qs)) return -1;

	SLNFilterRef filter, subfilter;
	int rc = SLNFilterCreate(session, SLNBadMetaFileFilterType, &filter);
	if(DB_EACCES == rc) return 403;
	if(rc < 0) return 500;

	static strarg_t const fields[] = { "q" };
	str_t *values[numberof(fields)] = {};
	QSValuesParse(qs, values, fields, numberof(fields));
	rc = SLNUserFilterParse(session, values[0], &subfilter);
	QSValuesCleanup(values, numberof(values));
	if(DB_EINVAL == rc) rc = SLNFilterCreate(session, SLNVisibleFilterType, &subfilter);
	if(rc < 0) {
		SLNFilterFree(&filter);
		return 500;
	}

	rc = SLNFilterAddFilterArg(filter, subfilter);
	if(rc < 0) {
		SLNFilterFree(&subfilter);
		SLNFilterFree(&filter);
		return 500;
	}

	sendURIList(session, filter, qs, false, conn);
	SLNFilterFree(&filter);
	return 0;
}


int SLNServerDispatch(SLNRepoRef const repo, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	int rc = -1;
//	rc = rc >= 0 ? rc : POST_auth(repo, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_file(repo, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_meta(repo, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : POST_file(repo, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : PUT_file(repo, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_query(repo, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : POST_query(repo, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_metafiles(repo, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_query_obsolete(repo, session, conn, method, URI, headers);
	if(rc >= 0) return rc;

	// We "own" the /sln prefix.
	// Any other paths within it are invalid.
	size_t const pfx = prefix("/sln", URI);
	if(0 == pfx) return -1;
	if('\0' == URI[pfx]) return 400;
	if('?' == URI[pfx]) return 400;
	if('/' == URI[pfx]) return 400;
	return -1;
}

