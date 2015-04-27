#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include "../async/async.h"
#include "../../deps/http_parser/http_parser.h"
#include "../common.h"

typedef enum http_method HTTPMethod;
typedef enum {
	HTTPNothing = 0,
	HTTPMessageBegin,
	HTTPURL,
	HTTPHeaderField,
	HTTPHeaderValue,
	HTTPHeadersComplete,
	HTTPBody,
	HTTPMessageEnd,
} HTTPEvent;

typedef struct HTTPConnection* HTTPConnectionRef;

int HTTPConnectionCreateIncoming(uv_stream_t *const socket, HTTPConnectionRef *const out);
int HTTPConnectionCreateOutgoing(strarg_t const domain, HTTPConnectionRef *const out);
void HTTPConnectionFree(HTTPConnectionRef *const connptr);
int HTTPConnectionPeek(HTTPConnectionRef const conn, HTTPEvent *const type, uv_buf_t *const buf);
void HTTPConnectionPop(HTTPConnectionRef const conn, size_t const len);

// Reading
int HTTPConnectionReadRequest(HTTPConnectionRef const conn, HTTPMethod *const method, str_t *const out, size_t const max);
int HTTPConnectionReadResponseStatus(HTTPConnectionRef const conn);
int HTTPConnectionReadHeaderField(HTTPConnectionRef const conn, str_t field[], size_t const max);
int HTTPConnectionReadHeaderValue(HTTPConnectionRef const conn, str_t value[], size_t const max);
int HTTPConnectionReadBody(HTTPConnectionRef const conn, uv_buf_t *const buf);
int HTTPConnectionReadBodyLine(HTTPConnectionRef const conn, str_t out[], size_t const max);
ssize_t HTTPConnectionReadBodyStatic(HTTPConnectionRef const conn, byte_t *const out, size_t const max);
int HTTPConnectionDrainMessage(HTTPConnectionRef const conn);


// Writing
int HTTPConnectionWrite(HTTPConnectionRef const conn, byte_t const *const buf, size_t const len);
int HTTPConnectionWritev(HTTPConnectionRef const conn, uv_buf_t const parts[], unsigned int const count);
int HTTPConnectionWriteRequest(HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const requestURI, strarg_t const host);
int HTTPConnectionWriteResponse(HTTPConnectionRef const conn, uint16_t const status, strarg_t const message);
int HTTPConnectionWriteHeader(HTTPConnectionRef const conn, strarg_t const field, strarg_t const value);
int HTTPConnectionWriteContentLength(HTTPConnectionRef const conn, uint64_t const length);
int HTTPConnectionWriteSetCookie(HTTPConnectionRef const conn, strarg_t const cookie, strarg_t const path, uint64_t const maxage);
int HTTPConnectionBeginBody(HTTPConnectionRef const conn);
int HTTPConnectionWriteFile(HTTPConnectionRef const conn, uv_file const file);
int HTTPConnectionWriteChunkLength(HTTPConnectionRef const conn, uint64_t const length);
int HTTPConnectionWriteChunkv(HTTPConnectionRef const conn, uv_buf_t const parts[], unsigned int const count);
int HTTPConnectionWriteChunkFile(HTTPConnectionRef const conn, strarg_t const path);
int HTTPConnectionWriteChunkEnd(HTTPConnectionRef const conn);
int HTTPConnectionEnd(HTTPConnectionRef const conn);

// Convenience
int HTTPConnectionSendString(HTTPConnectionRef const conn, uint16_t const status, strarg_t const str);
int HTTPConnectionSendStatus(HTTPConnectionRef const conn, uint16_t const status);
int HTTPConnectionSendFile(HTTPConnectionRef const conn, strarg_t const path, strarg_t const type, int64_t size);

#endif
