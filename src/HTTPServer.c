#define _GNU_SOURCE
#include <uv.h>
#include "async.h"
#include "HTTPServer.h"

#define READ_BUFFER_SIZE (1024 * 8)

struct HTTPServer {
	HTTPListener listener;
	void *context;
	HeaderFieldList const *fields;
	uv_tcp_t *socket;
};

static void connection_cb(uv_stream_t *const socket, int const status);

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

static uv_stream_t *connection_socket;
static void connection(void) {
	uv_stream_t *const socket = connection_socket;
	HTTPServerRef const server = socket->data;

	uv_tcp_t stream;
	if(uv_tcp_init(loop, &stream) < 0) return co_terminate();
	if(uv_accept(socket, (uv_stream_t *)&stream) < 0) return co_terminate();

	http_parser parser;
	http_parser_init(&parser, HTTP_REQUEST);
	byte_t *buf = malloc(READ_BUFFER_SIZE);
	for(;;) {
		HTTPConnectionRef const conn = HTTPConnectionCreateIncoming(&stream, &parser, server->fields, buf, READ_BUFFER_SIZE);
		if(!conn) break;
		server->listener(server->context, conn);
		HTTPConnectionDrain(conn);
		HTTPConnectionFree(conn);
	}
	FREE(&buf);

	stream.data = co_active();
	uv_close((uv_handle_t *)&stream, async_close_cb);
	co_switch(yield);
	co_terminate();
}
static void connection_cb(uv_stream_t *const socket, int const status) {
	connection_socket = socket;
	co_switch(co_create(STACK_SIZE, connection));
}

