#define _GNU_SOURCE
#include "HTTPServer.h"
#include "async.h"

#define BUFFER_SIZE (1024 * 8)
#define URI_MAX 1024
#define FIELD_MAX 80

struct HTTPServer {
	HTTPListener listener;
	void *context;
	HeaderFieldList const *fields;
	uv_tcp_t *socket;
};
typedef struct HTTPConnection {
	// Connection
	HTTPServerRef server;
	cothread_t thread;
	uv_tcp_t *stream;
	http_parser *parser;
	byte_t *buf;
	ssize_t nread;
	bool_t streamEOF; // Unless you are deciding whether to start a new message, you should pretty much always use messageEOF instead. Perhaps we could get rid of this by using on_message_begin instead.

	// Incoming
	str_t *requestURI;
	HeadersRef headers;
	byte_t const *chunk;
	size_t chunkLength;
	bool_t messageEOF;

	// Outgoing
	// nothing yet...
} HTTPConnection;

struct Headers {
	HeaderFieldList const *fields;
	str_t *field;
	index_t current;
	str_t **data;
};

static size_t append(str_t *const dst, size_t const dsize, strarg_t const src, size_t const slen);
static err_t readOnce(HTTPConnectionRef const conn);
static void connection_cb(uv_stream_t *const socket, int const status);
static void write_cb(uv_write_t *const req, int status);
static strarg_t statusstr(uint16_t const status);

HTTPServerRef HTTPServerCreate(HTTPListener const listener, void *const context, HeaderFieldList const *const fields) {
	BTAssert(listener, "HTTPServer listener required");
	HTTPServerRef const server = calloc(1, sizeof(struct HTTPServer));
	server->listener = listener;
	server->context = context;
	server->fields = fields;
	server->socket = NULL;
	return server;
}
void HTTPServerFree(HTTPServerRef const server) {
	if(!server) return;
	HTTPServerClose(server);
	free(server);
}

int HTTPServerListen(HTTPServerRef const server, uint16_t const port, strarg_t const address) {
	if(!server) return 0;
	BTAssert(!server->socket, "HTTPServer already listening");
	// INADDR_ANY, INADDR_LOOPBACK
	server->socket = malloc(sizeof(uv_tcp_t));
	if(!server->socket) return -1;
	if(uv_tcp_init(loop, server->socket) < 0) return -1;
	server->socket->data = server;
	struct sockaddr_in addr;
	if(uv_ip4_addr(address, port, &addr) < 0) return -1;
	if(uv_tcp_bind(server->socket, (struct sockaddr *)&addr, 0) < 0) return -1;
	if(uv_listen((uv_stream_t *)server->socket, 511, connection_cb) < 0) return -1;
	return 0;
}
static void socket_close_cb(uv_handle_t *const handle) {
	bool_t *const safe = handle->data;
	*safe = true;
}
void HTTPServerClose(HTTPServerRef const server) {
	if(!server) return;
	if(!server->socket) return;
	bool_t safe = false;
	server->socket->data = &safe;
	uv_close((uv_handle_t *)server->socket, socket_close_cb);
	while(!safe) uv_run(loop, UV_RUN_ONCE);
	FREE(&server->socket);
}
HeaderFieldList const *HTTPServerGetHeaderFields(HTTPServerRef const server) {
	if(!server) return NULL;
	return server->fields;
}

HeadersRef HeadersCreate(HeaderFieldList const *const fields) {
	if(!fields) return NULL;
	HeadersRef const headers = calloc(1, sizeof(struct Headers));
	headers->fields = fields;
	headers->field = malloc(FIELD_MAX+1);
	headers->field[0] = '\0';
	headers->current = fields->count;
	headers->data = calloc(fields->count, sizeof(str_t *));
	return headers;
}
void HeadersFree(HeadersRef const headers) {
	if(!headers) return;
	FREE(&headers->field);
	for(index_t i = 0; i < headers->fields->count; ++i) {
		FREE(&headers->data[i]);
	}
	FREE(headers->data);
	free(headers);
}
err_t HeadersAppendFieldChunk(HeadersRef const headers, strarg_t const chunk, size_t const len) {
	if(!headers) return 0;
	append(headers->field, FIELD_MAX, chunk, len);
	return 0;
}
err_t HeadersAppendValueChunk(HeadersRef const headers, strarg_t const chunk, size_t const len) {
	if(!headers) return 0;
	HeaderFieldList const *const fields = headers->fields;
	if(headers->field[0]) {
		headers->current = fields->count; // Mark as invalid.
		for(index_t i = 0; i < fields->count; ++i) {
			if(0 != strcasecmp(headers->field, fields->items[i].name)) continue;
			if(headers->data[i]) continue; // Use separate slots for duplicate headers, if available.
			headers->current = i;
			headers->data[i] = malloc(fields->items[i].size+1);
			if(!headers->data[i]) return -1;
			headers->data[i][0] = '\0';
			break;
		}
		headers->field[0] = '\0';
	}
	if(headers->current < fields->count) {
		index_t const i = headers->current;
		append(headers->data[i], fields->items[i].size, chunk, len);
	}
	return 0;
}
void HeadersEnd(HeadersRef const headers) {
	if(!headers) return;
	// No-op.
}
void *HeadersGetData(HeadersRef const headers) {
	if(!headers) return NULL;
	return headers->data;
}
void HeadersClear(HeadersRef const headers) {
	if(!headers) return;
	headers->field[0] = '\0';
	headers->current = headers->fields->count;
	for(index_t i = 0; i < headers->fields->count; ++i) {
		FREE(&headers->data[i]);
	}
}

static size_t append(str_t *const dst, size_t const dsize, strarg_t const src, size_t const slen) {
	size_t const olen = strlen(dst);
	size_t const nlen = MIN(olen + slen, dsize);
	memcpy(dst + olen, src, nlen - olen);
	dst[nlen] = '\0';
	return nlen - olen;
}

HTTPMethod HTTPConnectionGetRequestMethod(HTTPConnectionRef const conn) {
	if(!conn) return 0;
	return conn->parser->method;
}
strarg_t HTTPConnectionGetRequestURI(HTTPConnectionRef const conn) {
	if(!conn) return NULL;
	return conn->requestURI;
}
void *HTTPConnectionGetHeaders(HTTPConnectionRef const conn) {
	if(!conn) return NULL;
	return HeadersGetData(conn->headers);
}
ssize_t HTTPConnectionRead(HTTPConnectionRef const conn, byte_t *const buf, size_t const len) {
	if(!conn) return -1;
	if(!conn->chunkLength) return conn->messageEOF ? 0 : -1;
	size_t const used = MIN(len, conn->chunkLength);
	memcpy(buf, conn->chunk, used);
	conn->chunk += used;
	conn->chunkLength -= used;
	if(!conn->messageEOF && -1 == readOnce(conn)) return -1;
	return used;
}
ssize_t HTTPConnectionGetBuffer(HTTPConnectionRef const conn, byte_t const **const buf) {
	if(!conn) return -1;
	if(!conn->chunkLength) {
		if(conn->messageEOF) return 0;
		if(readOnce(conn) < 0) return -1;
	}
	size_t const used = conn->chunkLength;
	*buf = conn->chunk;
	conn->chunk = NULL;
	conn->chunkLength = 0;
	return used;
}

ssize_t HTTPConnectionWrite(HTTPConnectionRef const conn, byte_t const *const buf, size_t const len) {
	if(!conn) return 0;
	uv_buf_t obj = uv_buf_init((char *)buf, len);
	async_state state = { .thread = co_active() };
	uv_write_t req = { .data = &state };
	uv_write(&req, (uv_stream_t *)conn->stream, &obj, 1, async_write_cb);
	co_switch(yield);
	return state.status;
}
ssize_t HTTPConnectionWritev(HTTPConnectionRef const conn, uv_buf_t const *const parts, unsigned int const count) {
	if(!conn) return 0;
	async_state state = { .thread = co_active() };
	uv_write_t req = { .data = &state };
	uv_write(&req, (uv_stream_t *)conn->stream, parts, count, async_write_cb);
	co_switch(yield);
	return state.status;
}
ssize_t HTTPConnectionWriteResponse(HTTPConnectionRef const conn, uint16_t const status, strarg_t const message) {
	if(!conn) return 0;
	// TODO: TCP_CORK?
	// TODO: Suppply our own message for known status codes.
	str_t *str;
	int const slen = asprintf(&str, "HTTP/1.1 %d %s\r\n", status, message);
	if(slen < 0) return -1;
	ssize_t const wlen = HTTPConnectionWrite(conn, (byte_t *)str, slen);
	FREE(&str);
	return wlen < 0 ? -1 : 0;
}
err_t HTTPConnectionWriteHeader(HTTPConnectionRef const conn, strarg_t const field, strarg_t const value) {
	if(!conn) return 0;
	uv_buf_t parts[] = {
		uv_buf_init((char *)field, strlen(field)),
		uv_buf_init(": ", 2),
		uv_buf_init((char *)value, strlen(value)),
		uv_buf_init("\r\n", 2),
	};
	ssize_t const wlen = HTTPConnectionWritev(conn, parts, numberof(parts));
	return wlen < 0 ? -1 : 0;
}
err_t HTTPConnectionWriteContentLength(HTTPConnectionRef const conn, uint64_t const length) {
	if(!conn) return 0;
	str_t *str = NULL;
	int const slen = asprintf(&str, "Content-Length: %llu\r\n", (unsigned long long)length);
	if(slen < 0) return -1;
	ssize_t const wlen = HTTPConnectionWrite(conn, (byte_t *)str, slen);
	FREE(&str);
	return wlen < 0 ? -1 : 0;
}
err_t HTTPConnectionBeginBody(HTTPConnectionRef const conn) {
	if(!conn) return 0;
	if(HTTPConnectionWriteHeader(conn, "Connection", "keep-alive") < 0) return -1; // TODO: Make sure we're doing this right.
	if(HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2) < 0) return -1; // TODO: Safe for HEAD requests?
	// TODO: TCP_CORK?
	return 0;
}
err_t HTTPConnectionWriteFile(HTTPConnectionRef const conn, uv_file const file) {
	// TODO: How do we use uv_fs_sendfile to a TCP stream? Is it impossible?
	cothread_t const thread = co_active();
	uv_fs_t req = { .data = thread };
	async_state state = { .thread = thread };
	uv_write_t wreq = { .data = &state };
	byte_t *buf = malloc(BUFFER_SIZE);
	int64_t pos = 0;
	for(;;) {
		uv_buf_t const read = uv_buf_init((char *)buf, BUFFER_SIZE);
		uv_fs_read(loop, &req, file, &read, 1, pos, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);
		if(0 == req.result) break;
		if(req.result < 0) { // TODO: EAGAIN, etc?
			FREE(&buf);
			return req.result;
		}
		pos += req.result;
		uv_buf_t const write = uv_buf_init((char *)buf, req.result);
		uv_write(&wreq, (uv_stream_t *)conn->stream, &write, 1, async_write_cb);
		co_switch(yield);
		if(state.status < 0) {
			FREE(&buf);
			return state.status;
		}
	}
	FREE(&buf);
	return 0;
}
err_t HTTPConnectionWriteChunkLength(HTTPConnectionRef const conn, uint64_t const length) {
	if(!conn) return 0;
	str_t *str;
	int const slen = asprintf(&str, "%llx\r\n", (unsigned long long)length);
	if(slen < 0) return -1;
	ssize_t const wlen = HTTPConnectionWrite(conn, (byte_t const *)str, slen);
	FREE(&str);
	return wlen < 0 ? -1 : 0;
}
err_t HTTPConnectionWriteChunkFile(HTTPConnectionRef const conn, strarg_t const path) {
	uv_fs_t req = { .data = co_active() };
	uv_fs_open(loop, &req, path, O_RDONLY, 0000, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	if(req.result < 0) {
		return -1;
	}
	uv_file const file = req.result;
	uv_fs_fstat(loop, &req, file, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	if(req.result < 0) {
		uv_fs_close(loop, &req, file, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);
		return -1;
	}
	uint64_t const size = req.statbuf.st_size;

	if(size) {
		HTTPConnectionWriteChunkLength(conn, size);
		HTTPConnectionWriteFile(conn, file);
		HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2);
	}

	uv_fs_close(loop, &req, file, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	return 0;
}
err_t HTTPConnectionEnd(HTTPConnectionRef const conn) {
	if(!conn) return 0;
	if(HTTPConnectionWrite(conn, (byte_t *)"", 0) < 0) return -1;
	// TODO: Figure out keep-alive. If the client doesn't support it, we should close the connection here.
	return 0;
}

void HTTPConnectionSendMessage(HTTPConnectionRef const conn, uint16_t const status, strarg_t const msg) {
	size_t const len = strlen(msg);
	HTTPConnectionWriteResponse(conn, status, msg);
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/plain; charset=utf-8");
	HTTPConnectionWriteContentLength(conn, len+1);
	HTTPConnectionBeginBody(conn);
	// TODO: Check how HEAD responses should look.
	if(HTTP_HEAD != HTTPConnectionGetRequestMethod(conn)) {
		HTTPConnectionWrite(conn, (byte_t const *)msg, len);
		HTTPConnectionWrite(conn, (byte_t const *)"\n", 1);
	}
	HTTPConnectionEnd(conn);
//	if(status >= 400) fprintf(stderr, "%s: %d %s\n", HTTPConnectionGetRequestURI(conn), (int)status, msg);
}
void HTTPConnectionSendStatus(HTTPConnectionRef const conn, uint16_t const status) {
	strarg_t const msg = statusstr(status);
	HTTPConnectionSendMessage(conn, status, msg);
}
void HTTPConnectionSendFile(HTTPConnectionRef const conn, strarg_t const path, strarg_t const type, int64_t size) {
	uv_fs_t req = { .data = co_active() };
	uv_fs_open(loop, &req, path, O_RDONLY, 0600, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	uv_file const file = req.result;
	if(file < 0) return HTTPConnectionSendStatus(conn, 400); // TODO: Error conversion.
	if(size < 0) {
		uv_fs_fstat(loop, &req, file, async_fs_cb);
		co_switch(yield);
		if(req.result < 0) {
			uv_fs_req_cleanup(&req);
			return HTTPConnectionSendStatus(conn, 400);
		}
		size = req.statbuf.st_size;
	}
	uv_fs_req_cleanup(&req);
	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteContentLength(conn, size);
	if(type) HTTPConnectionWriteHeader(conn, "Content-Type", type);
	HTTPConnectionBeginBody(conn);
	HTTPConnectionWriteFile(conn, file);
	HTTPConnectionEnd(conn);
	uv_fs_close(loop, &req, file, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
}


// INTERNAL


static int on_message_begin(http_parser *const parser) {
	return 0;
}
static int on_url(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	append(conn->requestURI, URI_MAX, at, len);
	return 0;
}
static int on_header_field(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	HeadersAppendFieldChunk(conn->headers, at, len);
	return 0;
}
static int on_header_value(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	HeadersAppendValueChunk(conn->headers, at, len);
	return 0;
}
static int on_headers_complete(http_parser *const parser) {
	HTTPConnectionRef const conn = parser->data;
	HeadersEnd(conn->headers);
	return 0;
}
static int on_body(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	BTAssert(conn->requestURI[0], "Body chunk received out of order");
	BTAssert(!conn->chunkLength, "Chunk already waiting");
	BTAssert(!conn->messageEOF, "Message already complete");
	conn->chunk = (byte_t const *)at;
	conn->chunkLength = len;
	return 0;
}
static int on_message_complete(http_parser *const parser) {
	HTTPConnectionRef const conn = parser->data;
	conn->messageEOF = true;
	return 0;
}
static http_parser_settings const settings = {
	.on_message_begin = on_message_begin,
	.on_url = on_url,
	.on_header_field = on_header_field,
	.on_header_value = on_header_value,
	.on_headers_complete = on_headers_complete,
	.on_body = on_body,
	.on_message_complete = on_message_complete,
};


static void alloc_cb(uv_handle_t *const handle, size_t const suggested_size, uv_buf_t *const buf) {
	HTTPConnectionRef const conn = handle->data;
	*buf = uv_buf_init((char *)conn->buf, BUFFER_SIZE);
}
static void read_cb(uv_stream_t *const stream, ssize_t const nread, const uv_buf_t *const buf) {
	HTTPConnectionRef const conn = stream->data;
	conn->nread = nread;
	co_switch(conn->thread);
}

static ssize_t readOnce(HTTPConnectionRef const conn) {
	if(conn->messageEOF) return -1;
	conn->nread = 0;
	for(;;) {
		uv_read_start((uv_stream_t *)conn->stream, alloc_cb, read_cb);
		co_switch(yield);
		uv_read_stop((uv_stream_t *)conn->stream);
		if(conn->nread) break;
	}
	if(UV_EOF == conn->nread) {
		conn->streamEOF = true;
		conn->nread = 0;
	}
	if(conn->nread < 0) return -1;
	size_t const plen = http_parser_execute(conn->parser, &settings, (char const *)conn->buf, conn->nread);
	if(plen != conn->nread) return -1;
	return plen;
}
static err_t readHeaders(HTTPConnectionRef const conn) {
	for(;;) {
		if(readOnce(conn) < 0) return -1;
		if(conn->chunkLength || conn->messageEOF) return 0;
		if(conn->streamEOF) return -1;
	}
}
static err_t handleMessage(HTTPConnectionRef const conn) {
	err_t const err = readHeaders(conn);
	if(err) return err;
	BTAssert(conn->requestURI[0], "No URI in request");
	conn->server->listener(conn->server->context, conn);
	if(!conn->messageEOF) {
		// Use up any unread data.
		do {
			conn->chunk = NULL;
			conn->chunkLength = 0;
		} while(readOnce(conn) >= 0);
	}
	return 0;
}
static struct {
	uv_stream_t *socket;
} fiber_args = {};
static void handleStream(void) {
	uv_stream_t *const socket = fiber_args.socket;
	HTTPServerRef const server = socket->data;
	uv_tcp_t stream;
	if(uv_tcp_init(loop, &stream) < 0) {
		fprintf(stderr, "Stream init failed %p\n", co_active());
		co_terminate();
		return;
	}
	if(uv_accept(socket, (uv_stream_t *)&stream) < 0) {
		fprintf(stderr, "Accept failed %p\n", co_active());
		co_terminate();
		return;
	}

	struct http_parser parser;
	http_parser_init(&parser, HTTP_REQUEST);
	byte_t *const buf = malloc(BUFFER_SIZE);
	str_t *const requestURI = malloc(URI_MAX+1);
	HeadersRef const headers = HeadersCreate(server->fields);
	err_t status = 0;
	while(0 == status) {
		HTTPConnection conn = {};
		conn.server = server;
		conn.thread = co_active();
		conn.stream = &stream;
		conn.parser = &parser;
		conn.buf = buf;
		conn.requestURI = requestURI;
		conn.headers = headers;
		stream.data = &conn;
		parser.data = &conn;
		requestURI[0] = '\0';
		status = handleMessage(&conn);
		HeadersClear(headers);
	}
	free(buf);
	free(requestURI);
	HeadersFree(headers);
	stream.data = co_active();
	uv_close((uv_handle_t *)&stream, async_close_cb);
	co_switch(yield);
//	fprintf(stderr, "Closing thread %p\n", co_active());
	co_terminate();
}
static void connection_cb(uv_stream_t *const socket, int const status) {
	fiber_args.socket = socket;
	cothread_t const thread = co_create(1024 * 50 * sizeof(void *) / 4, handleStream);
//	fprintf(stderr, "Opening thread %p\n", thread);
	co_switch(thread);
}

static strarg_t statusstr(uint16_t const status) {
	switch(status) { // Ripped from Node.js
		case 100: return "Continue";
		case 101: return "Switching Protocols";
		case 102: return "Processing"; // RFC 2518, obsoleted by RFC 4918
		case 200: return "OK";
		case 201: return "Created";
		case 202: return "Accepted";
		case 203: return "Non-Authoritative Information";
		case 204: return "No Content";
		case 205: return "Reset Content";
		case 206: return "Partial Content";
		case 207: return "Multi-Status"; // RFC 4918
		case 300: return "Multiple Choices";
		case 301: return "Moved Permanently";
		case 302: return "Moved Temporarily";
		case 303: return "See Other";
		case 304: return "Not Modified";
		case 305: return "Use Proxy";
		case 307: return "Temporary Redirect";
		case 400: return "Bad Request";
		case 401: return "Unauthorized";
		case 402: return "Payment Required";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 406: return "Not Acceptable";
		case 407: return "Proxy Authentication Required";
		case 408: return "Request Time-out";
		case 409: return "Conflict";
		case 410: return "Gone";
		case 411: return "Length Required";
		case 412: return "Precondition Failed";
		case 413: return "Request Entity Too Large";
		case 414: return "Request-URI Too Large";
		case 415: return "Unsupported Media Type";
		case 416: return "Requested Range Not Satisfiable";
		case 417: return "Expectation Failed";
		case 418: return "I'm a teapot";               // RFC 2324
		case 422: return "Unprocessable Entity";       // RFC 4918
		case 423: return "Locked";                     // RFC 4918
		case 424: return "Failed Dependency";          // RFC 4918
		case 425: return "Unordered Collection";       // RFC 4918
		case 426: return "Upgrade Required";           // RFC 2817
		case 428: return "Precondition Required";      // RFC 6585
		case 429: return "Too Many Requests";          // RFC 6585
		case 431: return "Request Header Fields Too Large";// RFC 6585
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 502: return "Bad Gateway";
		case 503: return "Service Unavailable";
		case 504: return "Gateway Time-out";
		case 505: return "HTTP Version Not Supported";
		case 506: return "Variant Also Negotiates";    // RFC 2295
		case 507: return "Insufficient Storage";       // RFC 4918
		case 509: return "Bandwidth Limit Exceeded";
		case 510: return "Not Extended";               // RFC 2774
		case 511: return "Network Authentication Required"; // RFC 6585
		default: return NULL;
	}
}

