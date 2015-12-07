// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include "../../deps/uv/include/uv.h"
#include "../async/async.h"
#include "HTTPServer.h"

struct HTTPServer {
	HTTPListener listener;
	void *context;
	uv_tcp_t socket[1];
	struct tls *secure;
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

int HTTPServerListen(HTTPServerRef const server, strarg_t const address, strarg_t const port) {
	if(!server) return 0;
	assertf(!server->socket->data, "HTTPServer already listening");
	struct addrinfo *info = NULL;
	int rc = 0;

	struct addrinfo const hints = {
		.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_NUMERICSERV | AI_PASSIVE,
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = 0,
	};
	struct addrinfo *each = NULL;
	rc = async_getaddrinfo(address, port, &hints, &info);
	if(rc < 0) goto cleanup;
	for(each = info; each; each = each->ai_next) {
		server->socket->data = server;
		rc = uv_tcp_init(async_loop, server->socket);
		if(rc < 0) goto cleanup;

		rc = uv_tcp_bind(server->socket, each->ai_addr, 0);
		if(rc >= 0) break;

		async_close((uv_handle_t *)server->socket);
	}
	if(!each) {
		if(rc >= 0) rc = UV_EADDRNOTAVAIL;
		goto cleanup;
	}
	rc = uv_listen((uv_stream_t *)server->socket, 511, connection_cb);
	if(rc < 0) goto cleanup;

cleanup:
	uv_freeaddrinfo(info); info = NULL;
	if(rc < 0) HTTPServerClose(server);
	return rc;
}
int HTTPServerListenSecure(HTTPServerRef const server, strarg_t const address, strarg_t const port, struct tls **const tlsptr) {
	if(!server) return 0;
	int rc = HTTPServerListen(server, address, port);
	if(rc < 0) return rc;
	server->secure = *tlsptr; *tlsptr = NULL;
	return 0;
}
void HTTPServerClose(HTTPServerRef const server) {
	if(!server) return;
	if(server->secure) tls_close(server->secure);
	tls_free(server->secure); server->secure = NULL;
	async_close((uv_handle_t *)server->socket);
	server->socket->data = NULL;
}

static void connection(uv_stream_t *const socket) {
	HTTPServerRef const server = socket->data;
	HTTPConnectionRef conn;
	int rc = HTTPConnectionCreateIncomingSecure(socket, server->secure, 0, &conn);
	if(UV_EOF == rc) return;
	if(rc < 0) {
		alogf("Incoming connection error: %s\n", uv_strerror(rc));
		return;
	}
	assert(conn);

	for(;;) {
		server->listener(server->context, server, conn);
		rc = HTTPConnectionDrainMessage(conn);
		if(rc < 0) break;
	}

	HTTPConnectionFree(&conn);
}
static void connection_cb(uv_stream_t *const socket, int const status) {
	async_spawn(STACK_DEFAULT, (void (*)())connection, socket);
}

