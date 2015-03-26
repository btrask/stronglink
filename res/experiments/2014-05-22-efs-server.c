
typedef struct EFSHTTPServer* EFSHTTPServerRef;

typedef struct {
	void (*header)(void *const context, str_t const *const field, str_t const *const value);
	void (*data)(void *const context, byte_t const *const buf, size_t const len);
	void (*end)(void *const context);
	void (*error)(void *const context, int err);
	void *context;
} EFSHTTPCallbacks;

typedef void (*EFSHTTPListener)(void *const context, str_t const *const URI, fd_t const response, EFSHTTPCallbacks *const callbacks);


#include <sys/socket.h>
#include <pthread.h>
#include "../deps/http_parser/http_parser.h"
#include "header_parser.h"

#define THREAD_POOL_SIZE 10

struct EFSHTTPServer {
	EFSHTTPListener listener;
	void *context;
	volatile fd_t socket;
	pthread_t threads[THREAD_POOL_SIZE];
};

static void *EFSHTTPServerThread(EFSHTTPServerRef const server);

EFSHTTPServerRef EFSHTTPServerCreate(EFSHTTPListener const listener, void *const context) {
	BTAssert(listener, "EFSHTTPServer listener required");
	EFSHTTPServerRef const server = calloc(1, sizeof(struct EFSHTTPServer));
	server->listener = listener;
	server->context = context;
	server->socket = -1;
	return server;
}
void EFSHTTPServerFree(EFSHTTPServerRef const server) {
	if(!server) return;
	EFSHTTPServerClose(server);
	free(server);
}

int EFSHTTPServerListen(EFSHTTPServerRef const server, in_port_t const port, in_addr_t const address) {
	if(!server) return;
	BTAssert(-1 == server->socket, "EFSHTTPServer already listening");
	// INADDR_ANY, INADDR_LOOPBACK
	server->socket = BTErrno(socket(PF_INET, SOCK_STREAM, IPPROTO_TCP));
	if(-1 == server->socket) return -1;
	int const yes = 1;
	(void)BTErrno(setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)));
	struct sockaddr_in const addr = {
		.sin_len = sizeof(addr),
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr = {
			.s_addr = htonl(address),
		},
	};
	(void)BTErrno(bind(socket, &addr, sizeof(addr)));

	for(EFSIndex i = 0; i < THREAD_POOL_SIZE; ++i) {
		(void)BTErrno(pthread_create(&server->threads[i], NULL, EFSHTTPServerThread, server));
	}

	return 0;
}
void EFSHTTPServerClose(EFSHTTPServerRef const server) {
	if(-1 == server->socket) return;
	(void)BTErrno(close(server->socket)); server->socket = -1;
	for(EFSIndex i = 0; i < THREAD_POOL_SIZE; ++i) {
		(void)BTErrno(pthread_join(server->threads[i], NULL));
		server->threads[i] = 0;
	}
}

static struct http_parser_settings EFSParserCallbacks = {
	.on_message_begin = NULL,
	.on_url = on_url,
	.on_header_field = on_header_field,
	.on_header_value = on_header_value,
	.on_headers_complete = on_headers_complete,
	.on_body = on_body,
	.on_message_complete = on_message_complete,
};
typedef struct {
	EFSHTTPServerRef server;
	fd_t stream;
	header_parser headerParser;
	EFSHTTPCallbacks callbacks;
} EFSHTTPThreadState;

static void *EFSHTTPServerThread(EFSHTTPServerRef const server) {
	BTAssert(server, "EFSHTTPServer thread requires server");
	EFSHTTPThreadState state;
	struct http_parser parser = { .data = &state; };
	for(;;) {
		fd_t const socket = server->socket;
		if(-1 == socket) break;

		struct sockaddr const *connection = NULL;
		socklen_t socklen = 0;
		fd_t const stream = BTErrno(accept(socket, connection, &socklen));

		memset(&state, 0, sizeof(state));
		state.server = server;
		state.stream = stream;

		http_parser_init(&parser, HTTP_REQUEST);

		for(;;) {
			size_t const readLen = read(stream, buf, BUFFER_SIZE);
			if(-1 == readLen) {
				if(state.callbacks.error) state.callbacks.error(state.callbacks.context, errno);
				else if(EBADF != errno) (void)BTErrno(readLen);
				(void)close(stream);
				break;
			}
			size_t const parseLen = http_parser_execute(&parser, &EFSParserCallbacks, buf, len);
			if(parser.upgrade) {
				if(state.callbacks.error) state.callbacks.error(state.callbacks.context, -1); // TODO: Appropriate error.
				fprintf(stderr, "EFSHTTPServer unsupported upgrade request");
				(void)close(stream);
				break;
			} else if(parseLen < readLen) {
				if(state.callbacks.error) state.callbacks.error(state.callbacks.context, errno);
				fprintf(stderr, "EFSHTTPServer parse error");
				(void)close(stream);
				break;
			} else if(0 == parseLen) {
				break;
			}
		}

	}
}

static int on_url(http_parser *const parser, char const *const at, size_t const len) {
	EFSHTTPThreadState *const state = parser->data;
	str_t *const URI = calloc(len+1, 1);
	memcpy(URI, at, len);
	state->server->listener(state->server->context, URI, state->socket, &state->callbacks);
	free(URI);
	state->headerParser.callback = state->callbacks.header;
	state->headerParser.context = state->callbacks.context;
	return 0;
}
static int on_header_field(http_parser *const parser, char const *const at, size_t const len) {
	header_parse_field(parser->data->headerParser, at, len);
	return 0;
}
static int on_header_value(http_parser *const parser, char const *const at, size_t const len) {
	header_parse_value(parser->data->headerParser, at, len);
	return 0;
}
static int on_headers_complete(http_parser *const parser) {
	header_parse_complete(parser->data->headerParser);
	return 0;
}
static int on_body(http_parser *const parser, char const *const at, size_t const len) {
	EFSHTTPThreadState *const state = parser->data;
	state->callbacks.body(state->callbacks.context, at, len);
	return 0;
}
static int on_message_complete(http_parser *const parser) {
	EFSHTTPThreadState *const state = parser->data;
	state->callbacks.end(state->callbacks.context);
	return 0;
}

