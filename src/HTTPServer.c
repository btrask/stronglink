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
str_t const *HTTPConnectionGetRequestURI(HTTPConnectionRef const conn) {
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
	if(!conn->chunkLength) {
		return conn->eof ? 0 : -1;
	}
	size_t const used = MIN(len, conn->chunkLength);
	memcpy(buf, conn->chunk, used);
	conn->chunk += used;
	conn->chunkLength -= used;
	if(-1 == readOnce(conn)) return -1;
	return used;
}

fd_t HTTPConnectionGetStream(HTTPConnectionRef const conn) {
	if(!conn) return -1;
	return conn->stream;
}
void HTTPConnectionWriteResponse(HTTPConnectionRef const conn, uint16_t const status, str_t const *const message) {
	if(!conn) return;
	// TODO: TCP_CORK?
	// TODO: Suppply our own message for known status codes.
	str_t *str;
	int const slen = BTErrno(asprintf(&str, "HTTP/1.1 %d %s\r\n", status, message));
	if(-1 != slen) write(conn->stream, str, slen);
	FREE(&str);
}
void HTTPConnectionWriteHeader(HTTPConnectionRef const conn, str_t const *const field, str_t const *const value) {
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


// INTERNAL

static ssize_t append(str_t **const dst, size_t *const dsize, str_t const *const src, size_t const len) {
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

