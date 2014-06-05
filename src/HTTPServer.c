#define _GNU_SOURCE // For asprintf().

#include <sys/socket.h>
#include <sys/types.h>
#include <pthread.h>
#include "HTTPServer.h"

#define BUFFER_SIZE (1024 * 10)
#define THREAD_POOL_SIZE 10

struct HTTPServer {
	HTTPListener listener;
	void *context;
	volatile fd_t socket;
	pthread_t threads[THREAD_POOL_SIZE];
};
typedef struct HTTPConnection {
	// Connection
	fd_t stream;
	http_parser *parser;
	byte_t *buf;

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
static void *serverThread(HTTPServerRef const server);
static strarg_t statusstr(uint16_t const status);

HTTPServerRef HTTPServerCreate(HTTPListener const listener, void *const context) {
	BTAssert(listener, "HTTPServer listener required");
	HTTPServerRef const server = calloc(1, sizeof(struct HTTPServer));
	server->listener = listener;
	server->context = context;
	server->socket = -1;
	return server;
}
void HTTPServerFree(HTTPServerRef const server) {
	if(!server) return;
	HTTPServerClose(server);
	free(server);
}

int HTTPServerListen(HTTPServerRef const server, in_port_t const port, in_addr_t const address) {
	if(!server) return 0;
	BTAssert(-1 == server->socket, "HTTPServer already listening");
	// INADDR_ANY, INADDR_LOOPBACK
	server->socket = BTErrno(socket(PF_INET, SOCK_STREAM, IPPROTO_TCP));
	if(-1 == server->socket) return -1;
	int const yes = 1;
	(void)BTErrno(setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)));
	struct sockaddr_in const addr = {
//		.sin_len = sizeof(addr),
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr = {
			.s_addr = htonl(address),
		},
	};
	(void)BTErrno(bind(server->socket, (struct sockaddr *)&addr, sizeof(addr)));
	(void)BTErrno(listen(server->socket, 511));

	for(index_t i = 0; i < THREAD_POOL_SIZE; ++i) {
		(void)BTErrno(pthread_create(&server->threads[i], NULL, (void *(*)(void *))serverThread, server));
	}

	return 0;
}
void HTTPServerClose(HTTPServerRef const server) {
	if(-1 == server->socket) return;
	(void)BTErrno(close(server->socket)); server->socket = -1;
	for(index_t i = 0; i < THREAD_POOL_SIZE; ++i) {
		(void)BTErrno(pthread_join(server->threads[i], NULL));
		server->threads[i] = 0;
	}
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

fd_t HTTPConnectionGetStream(HTTPConnectionRef const conn) {
	if(!conn) return -1;
	return conn->stream;
}
void HTTPConnectionWriteResponse(HTTPConnectionRef const conn, uint16_t const status, strarg_t const message) {
	if(!conn) return;
	// TODO: TCP_CORK?
	// TODO: Suppply our own message for known status codes.
	str_t *str;
	int const slen = BTErrno(asprintf(&str, "HTTP/1.1 %d %s\r\n", status, message));
	if(-1 != slen) write(conn->stream, str, slen);
	FREE(&str);
}
void HTTPConnectionWriteHeader(HTTPConnectionRef const conn, strarg_t const field, strarg_t const value) {
	if(!conn) return;
	fd_t const stream = conn->stream;
	write(stream, field, strlen(field));
	write(stream, ": ", 2);
	write(stream, value, strlen(value));
	write(stream, "\r\n", 2);
}
void HTTPConnectionWriteContentLength(HTTPConnectionRef const conn, size_t const len) {
	if(!conn) return;
	str_t *str = NULL;
	int const slen = BTErrno(asprintf(&str, "Content-Length: %llu\r\n", (unsigned long long)len));
	if(-1 != slen) write(conn->stream, str, slen);
	FREE(&str);
}
void HTTPConnectionBeginBody(HTTPConnectionRef const conn) {
	if(!conn) return;
	fd_t const stream = conn->stream;
	HTTPConnectionWriteHeader(conn, "Connection", "close"); // TODO: Keepalive.
	write(stream, "\r\n", 2); // TODO: Safe for HEAD requests?
	// TODO: TCP_CORK?
}
void HTTPConnectionClose(HTTPConnectionRef const conn) {
	if(!conn) return;
	close(conn->stream); // TODO: Keepalive.
}

void HTTPConnectionSendMessage(HTTPConnectionRef const conn, uint16_t const status, strarg_t const msg) {
	size_t const len = strlen(msg);
	HTTPConnectionWriteResponse(conn, status, msg);
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/plain; charset=utf-8");
	HTTPConnectionWriteContentLength(conn, len);
	HTTPConnectionBeginBody(conn);
	// TODO: Check how HEAD responses should look.
	if(HTTP_HEAD != HTTPConnectionGetRequestMethod(conn)) {
		write(HTTPConnectionGetStream(conn), msg, len);
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

static ssize_t readOnce(HTTPConnectionRef const conn) {
	if(conn->eof) return -1;
	ssize_t const rlen = read(conn->stream, conn->buf, BUFFER_SIZE);
	if(-1 == rlen) return -1;
	size_t const plen = http_parser_execute(conn->parser, &settings, (char const *)conn->buf, rlen);
	if(plen != rlen) return -1;
	return plen;
}
static err_t readHeaders(HTTPConnectionRef const conn) {
	for(;;) {
		if(-1 == readOnce(conn)) return -1;
		if(conn->chunkLength || conn->eof) return 0;
	}
}
static err_t handleMessage(HTTPServerRef const server, HTTPConnectionRef const conn) {
	conn->headersSize = 10;
	conn->headers = calloc(1, sizeof(HTTPHeaderList) + sizeof(HTTPHeader) * conn->headersSize);

	err_t const err = readHeaders(conn);
	if(-1 != err) {
		server->listener(server->context, conn);
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
static void *serverThread(HTTPServerRef const server) {
	BTAssert(server, "HTTPServer thread requires server");
	for(;;) {
		fd_t const socket = server->socket;
		if(-1 == socket) break;

		struct sockaddr_in connection = {};
		socklen_t socklen = sizeof(connection);
		fd_t const stream = BTErrno(accept(socket, (struct sockaddr *)&connection, &socklen));
		if(-1 == stream) continue;

		struct http_parser parser;
		http_parser_init(&parser, HTTP_REQUEST);
		byte_t buf[BUFFER_SIZE];

		for(;;) {
			HTTPConnection conn = {};
			conn.stream = stream;
			conn.parser = &parser;
			conn.parser->data = &conn;
			conn.buf = buf;
			if(-1 == handleMessage(server, &conn)) break;
		}

	}

	return NULL; // TODO: What does pthread expect me to return?
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

