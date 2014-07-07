#define _GNU_SOURCE
#include <uv.h>
#include "../async.h"
#include "HTTPServer.h"

struct HTTPServer {
	HTTPListener listener;
	void *context;
	uv_tcp_t *socket;
};

static void connection_cb(uv_stream_t *const socket, int const status);

HTTPServerRef HTTPServerCreate(HTTPListener const listener, void *const context) {
	assertf(listener, "HTTPServer listener required");
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

int HTTPServerListen(HTTPServerRef const server, uint16_t const port, strarg_t const address) {
	if(!server) return 0;
	assertf(!server->socket, "HTTPServer already listening");
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

static uv_stream_t *connection_socket;
static void connection(void) {
	uv_stream_t *const socket = connection_socket;
	HTTPServerRef const server = socket->data;
	HTTPConnectionRef const conn = HTTPConnectionCreateIncoming(socket);
	if(!conn) return co_terminate();

	for(;;) {
		HTTPMessageRef const msg = HTTPMessageCreateIncoming(conn);
		if(!msg) break;
		server->listener(server->context, msg);
		HTTPMessageDrain(msg);
		HTTPMessageFree(msg);
		if(HPE_OK != HTTPConnectionError(conn)) break;
	}

	HTTPConnectionFree(conn);
	co_terminate();
}
static void connection_cb(uv_stream_t *const socket, int const status) {
	connection_socket = socket;
	co_switch(co_create(STACK_SIZE, connection));
}

