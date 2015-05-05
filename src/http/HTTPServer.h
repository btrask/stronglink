// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include "../common.h"
#include "HTTPConnection.h"

typedef struct HTTPServer* HTTPServerRef;

typedef void (*HTTPListener)(void *const context, HTTPConnectionRef const conn);


HTTPServerRef HTTPServerCreate(HTTPListener const listener, void *const context);
void HTTPServerFree(HTTPServerRef *const serverptr);
int HTTPServerListen(HTTPServerRef const server, strarg_t const address, strarg_t const port);
void HTTPServerClose(HTTPServerRef const server);

#endif
