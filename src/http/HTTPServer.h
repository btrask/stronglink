#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include "../common.h"
#include "HTTPMessage.h"

typedef struct HTTPServer* HTTPServerRef;

typedef void (*HTTPListener)(void *const context, HTTPConnectionRef const conn);


HTTPServerRef HTTPServerCreate(HTTPListener const listener, void *const context);
void HTTPServerFree(HTTPServerRef *const serverptr);
int HTTPServerListen(HTTPServerRef const server, strarg_t const port, uint32_t const type);
void HTTPServerClose(HTTPServerRef const server);

#endif
