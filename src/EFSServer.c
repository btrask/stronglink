#define _GNU_SOURCE
#include "common.h"
#include "EarthFS.h"
#include "http/HTTPServer.h"
#include "http/MultipartForm.h"
#include "http/QueryString.h"

typedef struct {
	strarg_t content_type;
} EFSHeaders;
static HeaderField const EFSHeaderFields[] = {
	{"content-type", 100},
};
static HeaderFieldList const EFSHeaderFieldList = {
	.count = numberof(EFSHeaderFields),
	.items = EFSHeaderFields,
};

static EFSMode method2mode(HTTPMethod const method) {
	switch(method) {
		case HTTP_GET:
		case HTTP_HEAD:
			return EFS_RDONLY;
		case HTTP_POST:
		case HTTP_PUT:
			return EFS_RDWR;
		default:
			assertf(0, "Unknown method %d", (int)method);
	}
}


// TODO: This should be static, but for now we're accessing it from Blog.c.
EFSSessionRef auth(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const qs) {
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


// TODO: These methods ought to be built on a public C API because the C API needs to support the same features as the HTTP interface.


static bool_t getFile(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_GET != method && HTTP_HEAD != method) return false;
	str_t algo[32] = {};
	str_t hash[256] = {};
	size_t pathlen = 0; // TODO: correct type for scanf %n ?
	(void)sscanf(URI, "/efs/file/%31[a-zA-Z0-9.-]/%255[a-zA-Z0-9.%_-]%n", algo, hash, &pathlen);
	if(!pathlen) return false;
	if('/' == URI[pathlen]) ++pathlen;
	if(!pathterm(URI, pathlen)) return false;
	EFSSessionRef const session = auth(repo, conn, method, URI+pathlen);
	if(!session) {
		HTTPConnectionSendStatus(conn, 403);
		return true;
	}

	sqlite3 *const db = EFSRepoDBConnect(repo);
	sqlite3_stmt *const select = QUERY(db,
		"SELECT f.internal_hash, f.file_type, f.file_size\n"
		"FROM files AS f\n"
		"LEFT JOIN file_uris AS f2 ON (f2.file_id = f.file_id)\n"
		"LEFT JOIN uris AS u ON (u.uri_id = f2.uri_id)\n"
		"WHERE u.uri = ('hash://' || ? || '/' || ?)\n"
		"ORDER BY f.file_id ASC LIMIT 1");
	sqlite3_bind_text(select, 1, algo, -1, SQLITE_STATIC); // TODO: Lowercase.
	sqlite3_bind_text(select, 2, hash, -1, SQLITE_STATIC);
	int const status = sqlite3_step(select);
	if(SQLITE_ROW != status) {
		sqlite3_finalize(select);
		EFSRepoDBClose(repo, db);
		HTTPConnectionSendStatus(conn, 404);
		EFSSessionFree(session);
		return true;
	}

	str_t *internalHash = strdup((char const *)sqlite3_column_text(select, 0));
	str_t *type = strdup((char const *)sqlite3_column_text(select, 1));
	int64_t const size = sqlite3_column_int64(select, 2);

	sqlite3_finalize(select);
	EFSRepoDBClose(repo, db);

	str_t *path = EFSRepoCopyInternalPath(repo, internalHash);

	// TODO: Do we need to send other headers?
	HTTPConnectionSendFile(conn, path, type, size);

	FREE(&path);
	FREE(&internalHash);
	FREE(&type);
	EFSSessionFree(session);
	return true;
}
static bool_t postFile(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_POST != method) return false;
	size_t pathlen = prefix("/efs/file", URI);
	if(!pathlen) return false;
	if('/' == URI[pathlen]) ++pathlen;
	if(!pathterm(URI, (size_t)pathlen)) return false;
	EFSSessionRef const session = auth(repo, conn, method, URI+pathlen);
	if(!session) {
		HTTPConnectionSendStatus(conn, 403);
		return true;
	}

	strarg_t type;
	ssize_t (*read)();
	void *context;

	EFSHeaders *const h1 = HTTPConnectionGetHeaders(conn, &EFSHeaderFieldList);
	MultipartFormRef const form = MultipartFormCreate(conn, h1->content_type, &EFSHeaderFieldList); // TODO: We shouldn't be reusing EFSHeaderFieldList for two purposes, but it's so simple that it works for now.
	if(form) {
		FormPartRef const part = MultipartFormGetPart(form);
		if(!part) {
			HTTPConnectionSendStatus(conn, 400);
			EFSSessionFree(session);
			return true;
		}
		EFSHeaders *const h2 = FormPartGetHeaders(part);
		type = h2->content_type;
		read = FormPartGetBuffer;
		context = part;
	} else {
		type = h1->content_type;
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
	size_t pathlen = prefix("/efs/query", URI);
	if(!pathlen) return false;
	if('/' == URI[pathlen]) ++pathlen;
	if(!pathterm(URI, (size_t)pathlen)) return false;
	EFSSessionRef const session = auth(repo, conn, HTTP_GET, URI+pathlen);
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
	if(getFile(repo, conn, method, URI)) return true;
	if(postFile(repo, conn, method, URI)) return true;
	if(query(repo, conn, method, URI)) return true;
	return false;
}

