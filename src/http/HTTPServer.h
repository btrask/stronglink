// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include "../../deps/libressl-portable/include/tls.h"
#include "../common.h"
#include "HTTPConnection.h"

typedef struct HTTPServer* HTTPServerRef;

typedef void (*HTTPListener)(void *const context, HTTPServerRef const server, HTTPConnectionRef const conn);


HTTPServerRef HTTPServerCreate(HTTPListener const listener, void *const context);
void HTTPServerFree(HTTPServerRef *const serverptr);
int HTTPServerListen(HTTPServerRef const server, strarg_t const address, strarg_t const port);
int HTTPServerListenSecure(HTTPServerRef const server, strarg_t const address, strarg_t const port, struct tls_config *const config);
void HTTPServerClose(HTTPServerRef const server);

#endif
