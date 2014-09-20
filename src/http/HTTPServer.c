#define _GNU_SOURCE
#include "../../deps/uv/include/uv.h"
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
void HTTPServerFree(HTTPServerRef *const serverptr) {
	HTTPServerRef server = *serverptr;
	if(!server) return;
	HTTPServerClose(server);
	server->listener = NULL;
	server->context = NULL;
	assert_zeroed(server, 1);
	FREE(serverptr); server = NULL;
}

err_t HTTPServerListen(HTTPServerRef const server, strarg_t const port, uint32_t const address) {
	if(!server) return 0;
	assertf(!server->socket, "HTTPServer already listening");
	assertf(INADDR_ANY == address || INADDR_LOOPBACK == address, "HTTPServer unsupported address");
	server->socket = malloc(sizeof(uv_tcp_t));
	if(!server->socket) return -1;
	if(uv_tcp_init(loop, server->socket) < 0) return -1;
	server->socket->data = server;
	struct addrinfo const hints = {
		.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_NUMERICSERV |
			(INADDR_ANY == address ? AI_PASSIVE : 0),
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = 0, // ???
	};
	struct addrinfo *info;
	if(async_getaddrinfo(NULL, port, &hints, &info) < 0) {
		return -1;
	}
	if(uv_tcp_bind(server->socket, info->ai_addr, 0) < 0) {
		uv_freeaddrinfo(info);
		return -1;
	}
	uv_freeaddrinfo(info);
	if(uv_listen((uv_stream_t *)server->socket, 511, connection_cb) < 0) {
		return -1;
	}
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

static void connection(uv_stream_t *const socket) {
	HTTPServerRef const server = socket->data;
	HTTPConnectionRef conn = HTTPConnectionCreateIncoming(socket);
	if(!conn) return;

	for(;;) {
		HTTPMessageRef msg = HTTPMessageCreate(conn);
		if(!msg) break;
		server->listener(server->context, msg);
		HTTPMessageDrain(msg);
		HTTPMessageFree(&msg);
		if(HPE_OK != HTTPConnectionError(conn)) break;
	}

	HTTPConnectionFree(&conn);
}
static void connection_cb(uv_stream_t *const socket, int const status) {
	async_thread(STACK_DEFAULT, (void (*)())connection, socket);
}

