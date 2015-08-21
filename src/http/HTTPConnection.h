// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include "../../deps/http_parser/http_parser.h"
#include "Socket.h"

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

int HTTPConnectionCreateIncoming(uv_stream_t *const ssocket, unsigned const flags, HTTPConnectionRef *const out);
int HTTPConnectionCreateIncomingSecure(uv_stream_t *const ssocket, struct tls *const ssecure, unsigned const flags, HTTPConnectionRef *const out);
int HTTPConnectionCreateOutgoing(strarg_t const domain, unsigned const flags, HTTPConnectionRef *const out);
void HTTPConnectionFree(HTTPConnectionRef *const connptr);
int HTTPConnectionPeek(HTTPConnectionRef const conn, HTTPEvent *const type, uv_buf_t *const buf);
void HTTPConnectionPop(HTTPConnectionRef const conn, size_t const len);

// Reading
ssize_t HTTPConnectionReadRequest(HTTPConnectionRef const conn, HTTPMethod *const method, str_t *const out, size_t const max);
int HTTPConnectionReadResponseStatus(HTTPConnectionRef const conn);
ssize_t HTTPConnectionReadHeaderField(HTTPConnectionRef const conn, str_t out[], size_t const max);
ssize_t HTTPConnectionReadHeaderValue(HTTPConnectionRef const conn, str_t out[], size_t const max);
int HTTPConnectionReadBody(HTTPConnectionRef const conn, uv_buf_t *const buf);
int HTTPConnectionReadBodyLine(HTTPConnectionRef const conn, str_t out[], size_t const max);
ssize_t HTTPConnectionReadBodyStatic(HTTPConnectionRef const conn, byte_t *const out, size_t const max);
int HTTPConnectionDrainMessage(HTTPConnectionRef const conn);


// Writing
int HTTPConnectionWrite(HTTPConnectionRef const conn, byte_t const *const buf, size_t const len);
int HTTPConnectionWritev(HTTPConnectionRef const conn, uv_buf_t parts[], unsigned int const count);
int HTTPConnectionWriteRequest(HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const requestURI, strarg_t const host);
int HTTPConnectionWriteResponse(HTTPConnectionRef const conn, uint16_t const status, strarg_t const message);
int HTTPConnectionWriteHeader(HTTPConnectionRef const conn, strarg_t const field, strarg_t const value);
int HTTPConnectionWriteContentLength(HTTPConnectionRef const conn, uint64_t const length);
int HTTPConnectionWriteSetCookie(HTTPConnectionRef const conn, strarg_t const cookie, strarg_t const path, uint64_t const maxage);
int HTTPConnectionBeginBody(HTTPConnectionRef const conn);
int HTTPConnectionWriteFile(HTTPConnectionRef const conn, uv_file const file);
int HTTPConnectionWriteChunkLength(HTTPConnectionRef const conn, uint64_t const length);
int HTTPConnectionWriteChunkv(HTTPConnectionRef const conn, uv_buf_t parts[], unsigned int const count);
int HTTPConnectionWriteChunkFile(HTTPConnectionRef const conn, strarg_t const path);
int HTTPConnectionWriteChunkEnd(HTTPConnectionRef const conn);
int HTTPConnectionEnd(HTTPConnectionRef const conn);
int HTTPConnectionFlush(HTTPConnectionRef const conn);

// Convenience
int HTTPConnectionSendString(HTTPConnectionRef const conn, uint16_t const status, strarg_t const str);
int HTTPConnectionSendStatus(HTTPConnectionRef const conn, uint16_t const status);
int HTTPConnectionSendRedirect(HTTPConnectionRef const conn, uint16_t const status, strarg_t const location);
int HTTPConnectionSendFile(HTTPConnectionRef const conn, strarg_t const path, strarg_t const type, int64_t size);


// TODO: Get rid of this.
static size_t append_buf_to_string(str_t *const dst, size_t const dsize, strarg_t const src, size_t const slen) {
	if(!dsize) return 0;
	size_t const olen = strlen(dst);
	size_t const nlen = MIN(olen + slen, dsize-1);
	memcpy(dst + olen, src, nlen - olen);
	dst[nlen] = '\0';
	return nlen - olen;
}

#endif // HTTPCONNECTION_H
