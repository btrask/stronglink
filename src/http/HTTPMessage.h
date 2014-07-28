#ifndef HTTPMESSAGE_H
#define HTTPMESSAGE_H

#include "../../deps/uv/include/uv.h"
#include "../../deps/http_parser/http_parser.h"
#include "../common.h"
#include "Headers.h"

typedef enum http_method HTTPMethod;

typedef struct HTTPConnection* HTTPConnectionRef;
typedef struct HTTPMessage* HTTPMessageRef;

HTTPConnectionRef HTTPConnectionCreateIncoming(uv_stream_t *const socket);
HTTPConnectionRef HTTPConnectionCreateOutgoing(strarg_t const domain);
void HTTPConnectionFree(HTTPConnectionRef *const connptr);
err_t HTTPConnectionError(HTTPConnectionRef const conn);

HTTPMessageRef HTTPMessageCreate(HTTPConnectionRef const conn);
void HTTPMessageFree(HTTPMessageRef *const msgptr);

// Message reading
HTTPMethod HTTPMessageGetRequestMethod(HTTPMessageRef const msg);
strarg_t HTTPMessageGetRequestURI(HTTPMessageRef const msg);
uint16_t HTTPMessageGetResponseStatus(HTTPMessageRef const msg);
void *HTTPMessageGetHeaders(HTTPMessageRef const msg, strarg_t const fields[], count_t const count);
ssize_t HTTPMessageRead(HTTPMessageRef const msg, byte_t *const buf, size_t const len);
ssize_t HTTPMessageReadLine(HTTPMessageRef const msg, str_t *const buf, size_t const len);
ssize_t HTTPMessageGetBuffer(HTTPMessageRef const msg, byte_t const **const buf); // Zero-copy version.
void HTTPMessageDrain(HTTPMessageRef const msg);

// Message writing
ssize_t HTTPMessageWrite(HTTPMessageRef const msg, byte_t const *const buf, size_t const len);
ssize_t HTTPMessageWritev(HTTPMessageRef const msg, uv_buf_t const parts[], unsigned int const count);
err_t HTTPMessageWriteRequest(HTTPMessageRef const msg, HTTPMethod const method, strarg_t const requestURI, strarg_t const host);
err_t HTTPMessageWriteResponse(HTTPMessageRef const msg, uint16_t const status, strarg_t const message);
err_t HTTPMessageWriteHeader(HTTPMessageRef const msg, strarg_t const field, strarg_t const value);
err_t HTTPMessageWriteContentLength(HTTPMessageRef const msg, uint64_t const length);
err_t HTTPMessageWriteSetCookie(HTTPMessageRef const msg, strarg_t const field, strarg_t const value, strarg_t const path, uint64_t const maxage);
err_t HTTPMessageBeginBody(HTTPMessageRef const msg);
err_t HTTPMessageWriteFile(HTTPMessageRef const msg, uv_file const file);
err_t HTTPMessageWriteChunkLength(HTTPMessageRef const msg, uint64_t const length);
ssize_t HTTPMessageWriteChunkv(HTTPMessageRef const msg, uv_buf_t const parts[], unsigned int const count);
err_t HTTPMessageWriteChunkFile(HTTPMessageRef const msg, strarg_t const path);
err_t HTTPMessageEnd(HTTPMessageRef const msg);

// Convenience
// TODO: These should return err_t too.
void HTTPMessageSendString(HTTPMessageRef const msg, uint16_t const status, strarg_t const str);
void HTTPMessageSendStatus(HTTPMessageRef const msg, uint16_t const status);
void HTTPMessageSendFile(HTTPMessageRef const msg, strarg_t const path, strarg_t const type, int64_t size);

#endif
