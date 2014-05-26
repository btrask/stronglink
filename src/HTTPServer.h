#include <netinet/in.h>
#include "../deps/http_parser/http_parser.h"
#include "common.h"

// TODO: Make these private, offer random access lookup method.
typedef struct {
	str_t *field;
	size_t fsize;
	str_t *value;
	size_t vsize;
} HTTPHeader;
typedef struct {
	count_t count;
	HTTPHeader items[0];
} HTTPHeaderList;

typedef struct HTTPServer* HTTPServerRef;
typedef struct HTTPConnection* HTTPConnectionRef;

typedef void (*HTTPListener)(void *const context, HTTPConnectionRef const conn);

typedef enum http_method HTTPMethod;

HTTPServerRef HTTPServerCreate(HTTPListener const listener, void *const context);
void HTTPServerFree(HTTPServerRef const server);
int HTTPServerListen(HTTPServerRef const server, in_port_t const port, in_addr_t const address);
void HTTPServerClose(HTTPServerRef const server);

// Connection reading
HTTPMethod HTTPConnectionGetRequestMethod(HTTPConnectionRef const conn);
str_t const *HTTPConnectionGetRequestURI(HTTPConnectionRef const conn);
HTTPHeaderList const *HTTPConnectionGetHeaders(HTTPConnectionRef const conn);
ssize_t HTTPConnectionRead(HTTPConnectionRef const conn, byte_t *const buf, size_t const len);

// Connection writing
fd_t HTTPConnectionGetStream(HTTPConnectionRef const conn);
void HTTPConnectionWriteResponse(HTTPConnectionRef const conn, uint16_t const status, str_t const *const message);
void HTTPConnectionWriteHeader(HTTPConnectionRef const conn, str_t const *const field, str_t const *const value);
void HTTPConnectionBeginBody(HTTPConnectionRef const conn);
void HTTPConnectionClose(HTTPConnectionRef const conn);

