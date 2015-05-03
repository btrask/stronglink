// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include "../common.h"
#include "HTTPConnection.h"

typedef struct HTTPHeaders* HTTPHeadersRef;

int HTTPHeadersCreate(HTTPHeadersRef *const out);
int HTTPHeadersCreateFromConnection(HTTPConnectionRef const conn, HTTPHeadersRef *const out);
void HTTPHeadersFree(HTTPHeadersRef *const hptr);
int HTTPHeadersLoad(HTTPHeadersRef const h, HTTPConnectionRef const conn);
strarg_t HTTPHeadersGet(HTTPHeadersRef const h, strarg_t const field);

