#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include "../common.h"
#include "HTTPMessage.h"

typedef struct HTTPServer* HTTPServerRef;

typedef void (*HTTPListener)(void *const context, HTTPMessageRef const msg);


HTTPServerRef HTTPServerCreate(HTTPListener const listener, void *const context);
void HTTPServerFree(HTTPServerRef const server);
int HTTPServerListen(HTTPServerRef const server, uint16_t const port, strarg_t const address);
void HTTPServerClose(HTTPServerRef const server);

#endif
