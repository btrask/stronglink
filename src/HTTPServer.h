#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include "common.h"
#include "HTTPConnection.h"

typedef struct HTTPServer* HTTPServerRef;

typedef void (*HTTPListener)(void *const context, HTTPConnectionRef const conn);


HTTPServerRef HTTPServerCreate(HTTPListener const listener, void *const context, HeaderFieldList const *const fields);
void HTTPServerFree(HTTPServerRef const server);
int HTTPServerListen(HTTPServerRef const server, uint16_t const port, strarg_t const address);
void HTTPServerClose(HTTPServerRef const server);
HeaderFieldList const *HTTPServerGetHeaderFields(HTTPServerRef const server);

#endif
