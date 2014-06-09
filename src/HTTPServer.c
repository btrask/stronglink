#define _GNU_SOURCE
#include "HTTPServer.h"
#include "async.h"

#define BUFFER_SIZE (1024 * 8)

struct HTTPServer {
	HTTPListener listener;
	void *context;
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

	// Request
	str_t *URI;
	size_t URISize;
	count_t headersSize;
	HTTPHeaderList *headers;
	byte_t const *chunk;
	size_t chunkLength;
	bool_t eof;
} HTTPConnection;

static err_t readOnce(HTTPConnectionRef const conn);
static void connection_cb(uv_stream_t *const socket, int const status);
static void write_cb(uv_write_t *const req, int status);
static strarg_t statusstr(uint16_t const status);

HTTPServerRef HTTPServerCreate(HTTPListener const listener, void *const context) {
	BTAssert(listener, "HTTPServer listener required");
	HTTPServerRef const server = calloc(1, sizeof(struct HTTPServer));
	server->listener = listener;
	server->context = context;
	server->socket = NULL;
	return server;
}
void HTTPServerFree(HTTPServerRef const server) {
	if(!server) return;
	HTTPServerClose(server);
	free(server);
}

int HTTPServerListen(HTTPServerRef const server, in_port_t const port, strarg_t const address) {
	if(!server) return 0;
	BTAssert(!server->socket, "HTTPServer already listening");
	// INADDR_ANY, INADDR_LOOPBACK
	server->socket = malloc(sizeof(uv_tcp_t));
	if(!server->socket) return -1;
	if(BTUVErr(uv_tcp_init(loop, server->socket))) return -1;
	server->socket->data = server;
	struct sockaddr_in addr;
	if(BTUVErr(uv_ip4_addr(address, port, &addr))) return -1;
	if(BTUVErr(uv_tcp_bind(server->socket, (struct sockaddr *)&addr, 0))) return -1;
	if(BTUVErr(uv_listen((uv_stream_t *)server->socket, 511, connection_cb))) return -1;
	return 0;
}
void HTTPServerClose(HTTPServerRef const server) {
	if(!server) return;
	if(!server->socket) return;
	uv_close((uv_handle_t *)server->socket, NULL);
	FREE(&server->socket);
}

HTTPMethod HTTPConnectionGetRequestMethod(HTTPConnectionRef const conn) {
	if(!conn) return 0;
	return conn->parser->method;
}
strarg_t HTTPConnectionGetRequestURI(HTTPConnectionRef const conn) {
	if(!conn) return NULL;
	return conn->URI;
}
HTTPHeaderList const *HTTPConnectionGetHeaders(HTTPConnectionRef const conn) {
	// TODO: Convenient method for random access lookup.
	if(!conn) return NULL;
	return conn->headers;
}
ssize_t HTTPConnectionRead(HTTPConnectionRef const conn, byte_t *const buf, size_t const len) {
	// TODO: Zero-copy version that provides access to the original buffer.
	if(!conn) return -1;
	if(!conn->chunkLength) return conn->eof ? 0 : -1;
	size_t const used = MIN(len, conn->chunkLength);
	memcpy(buf, conn->chunk, used);
	conn->chunk += used;
	conn->chunkLength -= used;
	if(!conn->eof && -1 == readOnce(conn)) return -1;
	return used;
}
ssize_t HTTPConnectionGetBuffer(HTTPConnectionRef const conn, byte_t const **const buf) {
	if(!conn) return -1;
	if(!conn->chunkLength) {
		if(conn->eof) return 0;
		if(-1 == readOnce(conn)) return -1;
	}
	size_t const used = conn->chunkLength;
	*buf = conn->chunk;
	conn->chunk = NULL;
	conn->chunkLength = 0;
	return used;
}

void HTTPConnectionWrite(HTTPConnectionRef const conn, byte_t const *const buf, size_t const len) {
	if(!conn) return;
	uv_buf_t obj = uv_buf_init((char *)buf, len);
	uv_write_t req = { .data = co_active() };
	(void)BTUVErr(uv_write(&req, (uv_stream_t *)conn->stream, &obj, 1, async_write_cb));
	co_switch(yield);
}
void HTTPConnectionWriteResponse(HTTPConnectionRef const conn, uint16_t const status, strarg_t const message) {
	if(!conn) return;
	// TODO: TCP_CORK?
	// TODO: Suppply our own message for known status codes.
	str_t *str;
	int const slen = BTErrno(asprintf(&str, "HTTP/1.1 %d %s\r\n", status, message));
	if(-1 != slen) HTTPConnectionWrite(conn, (byte_t *)str, slen);
	FREE(&str);
}
void HTTPConnectionWriteHeader(HTTPConnectionRef const conn, strarg_t const field, strarg_t const value) {
	if(!conn) return;
	uv_buf_t parts[] = {
		uv_buf_init((char *)field, strlen(field)),
		uv_buf_init(": ", 2),
		uv_buf_init((char *)value, strlen(value)),
		uv_buf_init("\r\n", 2),
	};
	uv_write_t req = { .data = co_active() };
	(void)BTUVErr(uv_write(&req, (uv_stream_t *)conn->stream, parts, numberof(parts), async_write_cb));
	co_switch(yield);
}
void HTTPConnectionWriteContentLength(HTTPConnectionRef const conn, size_t const len) {
	if(!conn) return;
	str_t *str = NULL;
	int const slen = BTErrno(asprintf(&str, "Content-Length: %llu\r\n", (unsigned long long)len));
	if(-1 != slen) HTTPConnectionWrite(conn, (byte_t *)str, slen);
	FREE(&str);
}
void HTTPConnectionBeginBody(HTTPConnectionRef const conn) {
	if(!conn) return;
	HTTPConnectionWriteHeader(conn, "Connection", "keepalive"); // TODO: Make sure we're doing this right.
	HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2); // TODO: Safe for HEAD requests?
	// TODO: TCP_CORK?
}
void HTTPConnectionClose(HTTPConnectionRef const conn) {
	if(!conn) return;
	// TODO: Figure out keepalive. Do we just never close connections?
}

void HTTPConnectionSendMessage(HTTPConnectionRef const conn, uint16_t const status, strarg_t const msg) {
	size_t const len = strlen(msg);
	HTTPConnectionWriteResponse(conn, status, msg);
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/plain; charset=utf-8");
	HTTPConnectionWriteContentLength(conn, len);
	HTTPConnectionBeginBody(conn);
	// TODO: Check how HEAD responses should look.
	if(HTTP_HEAD != HTTPConnectionGetRequestMethod(conn)) {
		HTTPConnectionWrite(conn, (byte_t const *)msg, len);
	}
	HTTPConnectionClose(conn);
	if(status >= 400) fprintf(stderr, "%s: %d %s\n", HTTPConnectionGetRequestURI(conn), (int)status, msg);
}
void HTTPConnectionSendStatus(HTTPConnectionRef const conn, uint16_t const status) {
	strarg_t const msg = statusstr(status);
	HTTPConnectionSendMessage(conn, status, msg);
}


// INTERNAL

static ssize_t append(str_t **const dst, size_t *const dsize, strarg_t const src, size_t const len) {
	size_t const old = *dst ? strlen(*dst) : 0;
	if(old + len > *dsize) {
		*dsize = MAX(10, MAX(*dsize * 2, len+1));
		*dst = realloc(*dst, *dsize);
		if(!*dst) {
			*dsize = 0;
			return -1;
		}
	}
	memcpy(*dst + old, src, len);
	(*dst)[old+len] = '\0';
	return old+len;
}

static int on_url(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	ssize_t const total = append(&conn->URI, &conn->URISize, at, len);
	return -1 == total ? -1 : 0;
}
static int on_header_field(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	if(conn->headers->items[conn->headers->count].value) {
		if(conn->headers->count >= conn->headersSize) {
			conn->headersSize *= 2;
			conn->headers = realloc(conn->headers, sizeof(HTTPHeaderList) + sizeof(HTTPHeader) * conn->headersSize);
			if(!conn->headers) return -1;
			memset(&conn->headers[conn->headers->count], 0, sizeof(HTTPHeader) * (conn->headersSize - conn->headers->count));
		}
		++conn->headers->count;
	}
	HTTPHeader *const header = &conn->headers->items[conn->headers->count];
	ssize_t const total = append(&header->field, &header->fsize, at, len);
	return -1 == total ? -1 : 0;
}
static int on_header_value(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	HTTPHeader *const header = &conn->headers->items[conn->headers->count];
	ssize_t const total = append(&header->value, &header->vsize, at, len);
	return -1 == total ? -1 : 0;
}
static int on_headers_complete(http_parser *const parser) {
	HTTPConnectionRef const conn = parser->data;
	++conn->headers->count; // Last header finished.
	// TODO: Lowercase and sort by field name for faster access.
	return 0;
}
static int on_body(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	BTAssert(!conn->chunkLength, "Chunk already waiting");
	BTAssert(!conn->eof, "Message already complete");
	conn->chunk = (byte_t const *)at;
	conn->chunkLength = len;
	return 0;
}
static int on_message_complete(http_parser *const parser) {
	HTTPConnectionRef const conn = parser->data;
	conn->eof = 1;
	return 0;
}
static http_parser_settings const settings = {
	.on_message_begin = NULL,
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
	if(conn->eof) return -1;
	conn->nread = 0;
	for(;;) {
		(void)BTUVErr(uv_read_start((uv_stream_t *)conn->stream, alloc_cb, read_cb));
		co_switch(yield);
		(void)BTUVErr(uv_read_stop((uv_stream_t *)conn->stream));
		if(conn->nread) break;
	}
	if(UV_EOF == conn->nread) conn->nread = 0;
	if(conn->nread < 0) return -1;
	size_t const plen = http_parser_execute(conn->parser, &settings, (char const *)conn->buf, conn->nread);
	if(plen != conn->nread) return -1;
	return plen;
}
static err_t readHeaders(HTTPConnectionRef const conn) {
	for(;;) {
		if(-1 == readOnce(conn)) return -1;
		if(conn->chunkLength || conn->eof) return 0;
	}
}
static err_t handleMessage(HTTPConnectionRef const conn) {
	conn->headersSize = 10;
	conn->headers = calloc(1, sizeof(HTTPHeaderList) + sizeof(HTTPHeader) * conn->headersSize);

	err_t const err = readHeaders(conn);
	if(-1 != err) {
		conn->server->listener(conn->server->context, conn);
	}

	FREE(&conn->URI);
	conn->URISize = 0;
	for(index_t i = 0; i < conn->headers->count; ++i) {
		FREE(&conn->headers->items[i].field);
		conn->headers->items[i].fsize = 0;
		FREE(&conn->headers->items[i].value);
		conn->headers->items[i].vsize = 0;
	}
	FREE(&conn->headers);
	conn->headersSize = 0;
	return err;
}
static struct {
	uv_stream_t *socket;
} fiber_args = {};
static void handleStream(void) {
	uv_stream_t *const socket = fiber_args.socket;
	uv_tcp_t stream;
	(void)BTUVErr(uv_tcp_init(loop, &stream));
	if(BTUVErr(uv_accept(socket, (uv_stream_t *)&stream))) {
		fprintf(stderr, "Accept failed\n");
		co_switch(yield); // TODO: Destroy thread
		return;
	}

	struct http_parser parser;
	http_parser_init(&parser, HTTP_REQUEST);
	byte_t buf[BUFFER_SIZE];
	for(;;) {
		HTTPConnection conn = {};
		conn.server = socket->data;
		conn.thread = co_active();
		conn.stream = &stream;
		conn.parser = &parser;
		stream.data = &conn;
		parser.data = &conn;
		conn.buf = buf;
		if(-1 == handleMessage(&conn)) break;
	}
	fprintf(stderr, "Stream closing\n");
	uv_close((uv_handle_t *)&stream, NULL);
	co_switch(yield);
	// TODO: Destroy the thread.
}
static void connection_cb(uv_stream_t *const socket, int const status) {
	fiber_args.socket = socket;
	co_switch(co_create(1024 * 100 * sizeof(void *) / 4, handleStream));
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

