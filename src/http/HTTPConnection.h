#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <uv.h>
#include "../../deps/http_parser/http_parser.h"
#include "../common.h"
#include "Headers.h"

typedef enum http_method HTTPMethod;

typedef struct HTTPConnection* HTTPConnectionRef;

HTTPConnectionRef HTTPConnectionCreateIncoming(uv_tcp_t *const stream, http_parser *const parser, HeaderFieldList const *const fields, byte_t *const buf, size_t const len);
void HTTPConnectionFree(HTTPConnectionRef const conn);

// Connection reading
HTTPMethod HTTPConnectionGetRequestMethod(HTTPConnectionRef const conn);
strarg_t HTTPConnectionGetRequestURI(HTTPConnectionRef const conn);
void *HTTPConnectionGetHeaders(HTTPConnectionRef const conn);
ssize_t HTTPConnectionRead(HTTPConnectionRef const conn, byte_t *const buf, size_t const len);
ssize_t HTTPConnectionGetBuffer(HTTPConnectionRef const conn, byte_t const **const buf); // Zero-copy version.
void HTTPConnectionDrain(HTTPConnectionRef const conn);

// Connection writing
ssize_t HTTPConnectionWrite(HTTPConnectionRef const conn, byte_t const *const buf, size_t const len);
ssize_t HTTPConnectionWritev(HTTPConnectionRef const conn, uv_buf_t const parts[], unsigned int const count);
err_t HTTPConnectionWriteResponse(HTTPConnectionRef const conn, uint16_t const status, strarg_t const message);
err_t HTTPConnectionWriteHeader(HTTPConnectionRef const conn, strarg_t const field, strarg_t const value);
err_t HTTPConnectionWriteContentLength(HTTPConnectionRef const conn, uint64_t const length);
err_t HTTPConnectionBeginBody(HTTPConnectionRef const conn);
err_t HTTPConnectionWriteFile(HTTPConnectionRef const conn, uv_file const file);
err_t HTTPConnectionWriteChunkLength(HTTPConnectionRef const conn, uint64_t const length);
ssize_t HTTPConnectionWriteChunkv(HTTPConnectionRef const conn, uv_buf_t const parts[], unsigned int const count);
err_t HTTPConnectionWriteChunkFile(HTTPConnectionRef const conn, strarg_t const path);
err_t HTTPConnectionEnd(HTTPConnectionRef const conn);

// Convenience
// TODO: These should return err_t too.
void HTTPConnectionSendMessage(HTTPConnectionRef const conn, uint16_t const status, strarg_t const msg);
void HTTPConnectionSendStatus(HTTPConnectionRef const conn, uint16_t const status);
void HTTPConnectionSendFile(HTTPConnectionRef const conn, strarg_t const path, strarg_t const type, int64_t size);

#endif
