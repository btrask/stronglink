#include "../../deps/uv/include/uv.h"
#include "../async/async.h"
#include "HTTPServer.h"

struct HTTPServer {
	HTTPListener listener;
	void *context;
	uv_tcp_t socket[1];
};

static void connection_cb(uv_stream_t *const socket, int const status);

HTTPServerRef HTTPServerCreate(HTTPListener const listener, void *const context) {
	assertf(listener, "HTTPServer listener required");
	HTTPServerRef const server = calloc(1, sizeof(struct HTTPServer));
	server->listener = listener;
	server->context = context;
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

int HTTPServerListen(HTTPServerRef const server, strarg_t const port, uint32_t const type) {
	if(!server) return 0;
	assertf(!server->socket->data, "HTTPServer already listening");
	int rc;
	rc = uv_tcp_init(loop, server->socket);
	if(rc < 0) return rc;
	server->socket->data = server;

	assertf(INADDR_LOOPBACK == type || INADDR_ANY == type, "HTTPServer unsupported type");
	int const loopback = INADDR_LOOPBACK == type ? 0 : AI_PASSIVE;
	struct addrinfo const hints = {
		.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_NUMERICSERV | loopback,
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = 0, // ???
	};
	struct addrinfo *info;
	rc = async_getaddrinfo(NULL, port, &hints, &info);
	if(rc < 0) {
		HTTPServerClose(server);
		return rc;
	}
	int bound = 0;
	rc = 0;
	for(struct addrinfo *each = info; each; each = each->ai_next) {
		rc = uv_tcp_bind(server->socket, each->ai_addr, 0);
		if(rc >= 0) bound++;
	}
	uv_freeaddrinfo(info);
	if(!bound) {
		HTTPServerClose(server);
		if(rc < 0) return rc;
		return UV_EADDRNOTAVAIL;
	}
	rc = uv_listen((uv_stream_t *)server->socket, 511, connection_cb);
	if(rc < 0) {
		HTTPServerClose(server);
		return rc;
	}
	return 0;
}
void HTTPServerClose(HTTPServerRef const server) {
	if(!server) return;
	if(!server->socket->data) return;
	async_close((uv_handle_t *)server->socket);
	memset(server->socket, 0, sizeof(*server->socket));
}

static void connection(uv_stream_t *const socket) {
	HTTPServerRef const server = socket->data;
	HTTPConnectionRef conn;
	int rc = HTTPConnectionCreateIncoming(socket, &conn);
	if(rc < 0) {
		fprintf(stderr, "HTTP server connection error %s\n", uv_strerror(rc));
		return;
	}

	for(;;) {
		server->listener(server->context, conn);
		int rc = HTTPConnectionDrainMessage(conn);
		if(rc < 0) break;
	}

	HTTPConnectionFree(&conn);
}
static void connection_cb(uv_stream_t *const socket, int const status) {
	async_spawn(STACK_DEFAULT, (void (*)())connection, socket);
}

