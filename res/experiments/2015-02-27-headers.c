
#define HEADERS_MAX 16
#define HEADERS_DUP_MAX 4

struct {
	hash_t fields[1];
	uint16_t offsets[HEADERS_MAX][HEADERS_DUP_MAX];
	str_t *values;
};

// is it a separate object?
// is it part of HTTPConnection?
// is it allocated separately, on the stack, or as part of something else?


// ...for 20 headers, is a hash table even worth it?

// 1. discard header fields longer than 16 chars
// 2. allocate 256 bytes for 16 packed header fields (no nul term?)
// 3. have an array of offsets
// 4. have a dynamic array for values, up to 1k each (so 16k max)

// we can also still use variable length fields
// so shorter headers can be packed in more


// Sec-WebSocket-Accept is 20 bytes long...
// we can over-allocate the available space
// e.g. 128 bytes total, 24 bytes max, 20 headers max



#define HEADERS_FIELDS_SIZE_MAX 128
#define HEADERS_FIELDS_COUNT_MAX 20
#define HEADERS_FIELD_LENGTH_MAX 24

struct {
	str_t *fields;
	uint16_t offsets;
	str_t *values;
};



// however, the question is still not answered
// how should we store and access this information?

// we know that it has to be parsed once before any handlers are called

// HTTPConnectionHeadersComplete(conn)
// plus HTTPHeaderSet or something?

// or something more generic like StringMap?



// maybe we should start by looking at our handler functions...

// main.c
static void listener(void *ctx, HTTPConnectionRef const conn) {
	HTTPMethod method;
	str_t URI[URI_MAX];
	int rc = HTTPConnectionReadRequestURI(conn, URI, URI_MAX, &method);
	if(rc < 0) return;
	if(EFSServerDispatch(repo, conn, method, URI)) return;
	if(BlogDispatch(blog, conn, method, URI)) return;
	HTTPConnectionSendStatus(conn, 400);
}

// EFSServer.c
bool EFSServerDispatch(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(postAuth(repo, conn, method, URI)) return true;
	if(getFile(repo, conn, method, URI)) return true;
	if(postFile(repo, conn, method, URI)) return true;
	if(query(repo, conn, method, URI)) return true;
	return false;
}


// we need some sort of semi-legitimate object thing
// so that we can call functions like

strarg_t HTTPHeaderGet(headers, "Set-Cookie");

// or

strarg_t HTTPConnectionGetHeader(conn, "Cookie");


// then we also need to support multiple values for the same field

strarg_t HTTPConnectionGetHeader(conn, "Cookie", 0);


// having a separate object sort of makes sense from an abstraction sense
// since we can pass it into some auth function without passing the whole connection?

// actually though, auth should be done ahead of time by the listener
// that'd be nice


bool EFSServerDispatch(EFSRepoRef const repo, EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI);

// getting messy...



bool EFSServerDispatch(EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI);
// session is always valid, but might not support certain operations...
// that seems like a bad design (error-prone)


int EFSServerDispatch(EFSRepoRef const repo, EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers);

// that seems like a very long and complicated list of arguments
// but at least there's a certain logic to it...



int EFSServerDispatch(EFSRepoRef const repo, EFSSessionRef const session, HTTPConnectionRef const conn);

// that's a bit simpler...
// but the usage is more complex...



// what about use?

int POST_file(conn, repo, session) {
	if(check(conn, HTTP_POST, "/sln/file") < 0) return 404;
	if(!session) return 403;
	
}


// it'd be even better if individual handlers didnt have to return anything

int EFSServerDispatch(HTTPConnectionRef const conn, EFSRepoRef const repo, EFSSessionRef const session) {
	dispatch(conn, repo, session, HTTP_POST, "/sln/file", POST_file);
}

// the problem with this is that it doesn't compose
// it works for individual handlers
// but it doesn't work for sets of them (e.g. EFSServer, Blog)




int POST_file(conn, repo, session) {
	if(check(conn, HTTP_POST, "/sln/file") < 0) return 404;
	if(!session) return 403;
	
}

// okay, this is getting pretty good...




int POST_file(repo, conn) {
	EFSSessionRef session;
	if(check(conn, HTTP_POST, "/sln/file") < 0) return 404;
	if(auth(conn, &session) < 0) return 403;
	
}

// worse...




int POST_file(conn, repo, session) {
	if(HTTPConnectionMatchRequest(conn, HTTP_POST, "/sln/file") < 0) return 404;
	if(!session) return 403;
	
}




// okay, how to handle dynamic paths and query parameters?

int GET_file(conn, repo, session) {
	if(HTTP_GET != HTTPConnectionRequestMethod(conn)) return 404;
	
}



// try again

int GET_file(EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
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
	if(rc < 0) switch(rc) {
		case UV_ECANCELED: return 0;
		case DB_NOTFOUND: return 404;
		default: return 500;
	}

	// TODO: Send other headers
	// Content-Disposition? What else?
	HTTPConnectionSendFile(conn, info->path, info->type, info->size);
	EFSFileInfoCleanup(info);
	return 0;
}
int POST_file(EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	if(HTTP_POST != method) return -1;
	int len = 0;
	sscanf(URI, "/efs/file%n", &len);
	if(!len) return -1;
	if('/' == URI[len]) len++;
	if('\0' != URI[len] && '?' != URI[len]) return -1;

	strarg_t const type = HTTPConnectionGetHeader(conn, "Content-Type", 0);
	if(!type) return 400;

	EFSSubmissionRef sub = NULL;
	rc = EFSSubmissionCreate(session, type, &sub);
	if(rc < 0) return 500;
	for(;;) {
		if(pull->stop) goto fail;
		uv_buf_t buf[1] = {};
		rc = HTTPConnectionReadBody(*conn, buf);
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





int EFSServerDispatch(EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI) {
	int rc = -1;
	rc = POST_file(session, conn, method, URI);
	if(rc >= 0) return rc;
	rc = GET_file(session, conn, method, URI);
	if(rc >= 0) return rc;
	return -1;
}


// what we want is a single unique value that means "continue"
// other results mean "break"
// and then 100-599 means "return appropriate status"
// it would be nice to just use 404, but that's ambiguous
// we could do something stupid like +404 and -404, but that's stupid
// UV_EINVAL is ugly, and it's weird to distinguish it from all the other errors

// 0 could be the special value but usually it means success


// in conclusion, -1 is the only thing that makes sense



// ugh, now how to deal with cancellation...
// basically, our strategy for requests is to block new requests
// but let outstanding requests finish
// which means not canceling them in the first place

// however, after a timeout we will probably have to cancel them
// especially long requests (e.g. persistent queries)

// maybe we should have an async_enter_cancelable() and async_leave_cancelable()
// and you could leave before you enter to block cancelation in a section that would otherwise be cancelable

// or just handle it everywhere




// okay, everything is ideal EXCEPT
// the header system itself



static void listener(void *ctx, HTTPConnectionRef const conn) {
	HTTPMethod method;
	str_t URI[URI_MAX];
	int rc = HTTPConnectionReadRequest(conn, &method, URI, URI_MAX);
	if(rc < 0) return;

	HTTPHeadersRef headers = HTTPHeadersCreateFromConnection(conn);
	if(!headers) return;

	strarg_t const cookie = HTTPHeadersGet(headers, "cookie");
	EFSSessionRef session = NULL;
	rc = EFSSessionCreate(repo, cookie, &session);
	if(rc < 0) {
		HTTPHeadersFree(&headers);
		HTTPConnectionSendStatus(conn, 403); // TODO
		return;
	}

	rc = -1;
	rc = rc >= 0 ? rc : EFSServerDispatch(session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : BlogDispatch(blog, session, conn, method, URI, headers);

	EFSSessionFree(&session);
	HTTPHeadersFree(&headers);
	if(rc < 0) rc = 404;
	if(rc > 0) HTTPConnectionSendStatus(conn, rc);
}




// i guess my last complaint
// is that i dont like how HTTPConnection mixes streaming and buffered i/o


// i think we need a master "read stuff" function
// HTTPConnectionReadRequest might as well be it

// for clarity, maybe it shouldn't return any values itself
// HTTPConnectionReadRequest(conn)
// HTTPConnectionGetRequest(conn, &method, URI, URI_MAX)
// HTTPConnectionGetHeader(conn, "cookie")

// if we're going to buffer the headers, we might as well buffer the uri...?

// although for some reason buffering the uri seems needlessly ugly




// okay
// adding header storage directly into the http-connection is just too ugly
// we need a separate object

#define HEADERS_MAX 20
#define HEADER_LEN 24
#define FIELDS_SIZE 128
#define VALUES_MIN (1024 * 1)
#define VALUES_MAX (1024 * 16)

struct HTTPHeaders {
	size_t offset;
	size_t count;
	str_t *fields;
	uint16_t *offsets;
	str_t *values;
};

int http_headers_init(http_headers_t *const headers);

HTTPHeadersRef HTTPHeadersCreate(void);

int HTTPHeadersInit(HTTPHeadersRef *const headers);


HTTPHeaderSetRef HTTPHeaderSetCreate(void);


HeaderSetRef HeaderSetCreate(void);



// should we just skip to using a general-purpose map/dictionary?




typedef struct HTTPHeaders* HTTPHeadersRef;

HTTPHeadersRef HTTPHeadersCreate(void);
HTTPHeadersRef HTTPHeadersFromConnection(HTTPConnectionRef const conn);
void HTTPHeadersFree(HTTPHeadersRef *const headersptr);
int HTTPHeadersLoad(HTTPConnectionRef const conn);
strarg_t HTTPHeadersGet(HTTPHeadersRef const headers, strarg_t const field);




#define HEADERS_MAX 20
#define FIELDS_SIZE 128
#define FIELD_MAX 24
#define VALUE_MAX 1024
#define VALUES_MIN (1024 * 1)
#define VALUES_MAX (1024 * 16)

struct HTTPHeaders {
	size_t offset;
	size_t count;
	str_t *fields;
	uint16_t *offsets;
	str_t *values;
	size_t values_size;
};

HTTPHeadersRef HTTPHeadersCreate(void) {
	HTTPHeadersRef h = calloc(1, sizeof(struct HTTPHeaders));
	if(!h) return NULL;
	h->offset = 0;
	h->count = 0;
	h->fields = calloc(FIELDS_SIZE, 1);
	h->offsets = calloc(HEADERS_MAX, sizeof(*h->offsets));
	h->values = NULL;
	h->values_size = 0;
	if(!h->fields || !h->offsets || !h->values) {
		HTTPHeadersFree(&h);
		return NULL;
	}
	return h;
}
HTTPHeadersRef HTTPHeadersFromConnection(HTTPConnectionRef const conn) {
	assert(conn);
	HTTPHeadersRef h = HTTPHeadersCreate();
	if(!h) return NULL;
	int rc = HTTPHeadersLoad(h, conn);
	if(rc < 0) {
		HTTPHeadersFree(&h);
		return NULL;
	}
	return h;
}
void HTTPHeadersFree(HTTPHeadersRef *const headersptr) {
	HTTPHeadersRef h = *headersptr;
	if(!h) return;
	h->offset = 0;
	h->count = 0;
	FREE(&h->fields);
	FREE(&h->offsets);
	FREE(&h->values);
	h->values_size = 0;
	FREE(headersptr); h = NULL;
}
int HTTPHeadersLoad(HTTPHeadersRef const h, HTTPConnectionRef const conn) {
	if(!h) return 0;
	if(!conn) return UV_EINVAL;
	uv_buf_t buf[1];
	HTTPEvent type;
	str_t field[FIELD_MAX];
	str_t value[VALUE_MAX];
	for(;;) {
		rc = HTTPConnectionPeek(conn, &type, buf);
		if(rc < 0) return rc;
		if(HTTPHeadersComplete == type) {
			HTTPConnectionPop(conn, buf->len);
			break;
		}
		field[0] = '\0';
		value[0] = '\0';
		rc = HTTPConnectionReadHeaderField(conn, field, FIELD_MAX);
		if(rc < 0) return rc;
		rc = HTTPConnectionReadHeaderValue(conn, value, VALUE_MAX);
		if(rc < 0) return rc;

		if(h->count >= HEADERS_MAX) continue;

		size_t const flen = strlen(field)+1;
		size_t const vlen = strlen(value)+1;
		if(!flen || !vlen) continue;
		if(flen >= FIELD_MAX) continue;
		if(vlen >= VALUE_MAX) continue;
		if(h->offset+flen >= FIELDS_SIZE) continue;
		if(h->offset[h->count]+vlen >= VALUES_MAX) continue;
		if(h->offset[h->count]+vlen >= h->values_size) {
			h->values_size = MAX(VALUES_MIN, h->values_size * 2);
			h->values_size = MAX(h->values_size, h->offset[h->count]+vlen);
			h->values = realloc(h->values, h->values_size);
			assert(h->values); // TODO
		}

		memcpy(h->fields + h->offset, field, flen);
		memcpy(h->values + h->offsets[h->count], value, vlen);
		h->offset += flen;
		h->offsets[++h->count] = vlen;
	}
	return 0;
}
strarg_t HTTPHeadersGet(HTTPHeadersRef const headers, strarg_t const field) {
	if(!headers) return NULL;
	if(!field) return NULL;
	size_t const len = strlen(field)+1;
	if(len > FIELD_MAX) return NULL;
	size_t pos = 0;
	for(size_t i = 0; i < h->count; i++) {
		if(pos+len >= FIELDS_SIZE) break;
		if(0 == memcmp(field, h->fields+pos, len)) {
			return h->values[h->offsets[i]];
		}
		pos += strlen(h->fields+pos);
	}
	return NULL;
}





// okay, that's well and good
// now in order to reach the pretty listener() function we have above
// we need to be able to create sessions for non-users
// so like userID 0?


// actually we can hack it for now






// this file has served us well















