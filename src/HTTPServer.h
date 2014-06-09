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
int HTTPServerListen(HTTPServerRef const server, uint16_t const port, strarg_t const address);
void HTTPServerClose(HTTPServerRef const server);

// Connection reading
HTTPMethod HTTPConnectionGetRequestMethod(HTTPConnectionRef const conn);
strarg_t HTTPConnectionGetRequestURI(HTTPConnectionRef const conn);
HTTPHeaderList const *HTTPConnectionGetHeaders(HTTPConnectionRef const conn);
ssize_t HTTPConnectionRead(HTTPConnectionRef const conn, byte_t *const buf, size_t const len);
ssize_t HTTPConnectionGetBuffer(HTTPConnectionRef const conn, byte_t const **const buf); // Zero-copy version.

// Connection writing
void HTTPConnectionWrite(HTTPConnectionRef const conn, byte_t const *const buf, size_t const len);
void HTTPConnectionWriteResponse(HTTPConnectionRef const conn, uint16_t const status, strarg_t const message);
void HTTPConnectionWriteHeader(HTTPConnectionRef const conn, strarg_t const field, strarg_t const value);
void HTTPConnectionWriteContentLength(HTTPConnectionRef const conn, size_t const len);
void HTTPConnectionBeginBody(HTTPConnectionRef const conn);
void HTTPConnectionClose(HTTPConnectionRef const conn);

// Convenience
void HTTPConnectionSendMessage(HTTPConnectionRef const conn, uint16_t const status, strarg_t const msg);
void HTTPConnectionSendStatus(HTTPConnectionRef const conn, uint16_t const status);
void HTTPConnectionSendFile(HTTPConnectionRef const conn, strarg_t const path);

