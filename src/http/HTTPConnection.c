#define _GNU_SOURCE
#include "../async.h"
#include "HTTPConnection.h"
#include "status.h"

#define WRITE_BUFFER_SIZE (1024 * 8)
#define URI_MAX 1024

typedef struct HTTPConnection {
	// Connection
	uv_tcp_t *stream;
	http_parser *parser;
	byte_t *buf;
	size_t len;
	bool_t streamEOF; // Unless you are deciding whether to start a new message, you should pretty much always use messageEOF instead. Perhaps we could get rid of this by using on_message_begin instead.

	// Incoming
	str_t *requestURI;
	HeadersRef headers;
	byte_t const *chunk;
	size_t chunkLength;
	bool_t messageEOF;

	// Outgoing
	// nothing yet...
} HTTPConnection;

static err_t readOnce(HTTPConnectionRef const conn);
static err_t readHeaders(HTTPConnectionRef const conn);
static strarg_t statusstr(uint16_t const status);

HTTPConnectionRef HTTPConnectionCreateIncoming(uv_tcp_t *const stream, http_parser *const parser, HeaderFieldList const *const fields, byte_t *const buf, size_t const len) {
	BTAssert(stream, "HTTPConnection stream required");
	BTAssert(parser, "HTTPConnection parser required");
	HTTPConnectionRef const conn = calloc(1, sizeof(struct HTTPConnection));
	if(!conn) return NULL;
	conn->stream = stream;
	conn->parser = parser;
	conn->parser->data = conn;
	conn->buf = buf;
	conn->len = len;
	conn->requestURI = malloc(URI_MAX+1);
	conn->requestURI[0] = '\0';
	conn->headers = HeadersCreate(fields);
	if(readHeaders(conn) < 0) {
		HTTPConnectionFree(conn);
		return NULL;
	}
	BTAssert(conn->requestURI[0], "No URI in request");
	return conn;
}
void HTTPConnectionFree(HTTPConnectionRef const conn) {
	if(!conn) return;
	FREE(&conn->requestURI);
	HeadersFree(conn->headers);
	conn->parser->data = NULL;
	conn->stream = NULL;
	conn->parser = NULL;
	free(conn);
}

HTTPMethod HTTPConnectionGetRequestMethod(HTTPConnectionRef const conn) {
	if(!conn) return 0;
	return conn->parser->method;
}
strarg_t HTTPConnectionGetRequestURI(HTTPConnectionRef const conn) {
	if(!conn) return NULL;
	return conn->requestURI;
}
void *HTTPConnectionGetHeaders(HTTPConnectionRef const conn) {
	if(!conn) return NULL;
	return HeadersGetData(conn->headers);
}
ssize_t HTTPConnectionRead(HTTPConnectionRef const conn, byte_t *const buf, size_t const len) {
	if(!conn) return -1;
	if(!conn->chunkLength) return conn->messageEOF ? 0 : -1;
	size_t const used = MIN(len, conn->chunkLength);
	memcpy(buf, conn->chunk, used);
	conn->chunk += used;
	conn->chunkLength -= used;
	if(!conn->messageEOF && -1 == readOnce(conn)) return -1;
	return used;
}
ssize_t HTTPConnectionGetBuffer(HTTPConnectionRef const conn, byte_t const **const buf) {
	if(!conn) return -1;
	if(!conn->chunkLength) {
		if(conn->messageEOF) return 0;
		if(readOnce(conn) < 0) return -1;
	}
	size_t const used = conn->chunkLength;
	*buf = conn->chunk;
	conn->chunk = NULL;
	conn->chunkLength = 0;
	return used;
}
void HTTPConnectionDrain(HTTPConnectionRef const conn) {
	if(!conn) return;
	if(conn->messageEOF) return;
	do {
		conn->chunk = NULL;
		conn->chunkLength = 0;
	} while(readOnce(conn) >= 0);
	BTAssert(conn->messageEOF, "Connection drain didn't reach EOF");
}

ssize_t HTTPConnectionWrite(HTTPConnectionRef const conn, byte_t const *const buf, size_t const len) {
	if(!conn) return 0;
	uv_buf_t obj = uv_buf_init((char *)buf, len);
	async_state state = { .thread = co_active() };
	uv_write_t req = { .data = &state };
	uv_write(&req, (uv_stream_t *)conn->stream, &obj, 1, async_write_cb);
	co_switch(yield);
	return state.status;
}
ssize_t HTTPConnectionWritev(HTTPConnectionRef const conn, uv_buf_t const parts[], unsigned int const count) {
	if(!conn) return 0;
	async_state state = { .thread = co_active() };
	uv_write_t req = { .data = &state };
	uv_write(&req, (uv_stream_t *)conn->stream, parts, count, async_write_cb);
	co_switch(yield);
	return state.status;
}
ssize_t HTTPConnectionWriteResponse(HTTPConnectionRef const conn, uint16_t const status, strarg_t const message) {
	if(!conn) return 0;
	// TODO: TCP_CORK?
	// TODO: Suppply our own message for known status codes.
	str_t *str;
	int const slen = asprintf(&str, "HTTP/1.1 %d %s\r\n", status, message);
	if(slen < 0) return -1;
	ssize_t const wlen = HTTPConnectionWrite(conn, (byte_t *)str, slen);
	FREE(&str);
	return wlen < 0 ? -1 : 0;
}
err_t HTTPConnectionWriteHeader(HTTPConnectionRef const conn, strarg_t const field, strarg_t const value) {
	if(!conn) return 0;
	uv_buf_t parts[] = {
		uv_buf_init((char *)field, strlen(field)),
		uv_buf_init(": ", 2),
		uv_buf_init((char *)value, strlen(value)),
		uv_buf_init("\r\n", 2),
	};
	ssize_t const wlen = HTTPConnectionWritev(conn, parts, numberof(parts));
	return wlen < 0 ? -1 : 0;
}
err_t HTTPConnectionWriteContentLength(HTTPConnectionRef const conn, uint64_t const length) {
	if(!conn) return 0;
	str_t *str = NULL;
	int const slen = asprintf(&str, "Content-Length: %llu\r\n", (unsigned long long)length);
	if(slen < 0) return -1;
	ssize_t const wlen = HTTPConnectionWrite(conn, (byte_t *)str, slen);
	FREE(&str);
	return wlen < 0 ? -1 : 0;
}
err_t HTTPConnectionBeginBody(HTTPConnectionRef const conn) {
	if(!conn) return 0;
	if(HTTPConnectionWriteHeader(conn, "Connection", "keep-alive") < 0) return -1; // TODO: Make sure we're doing this right.
	if(HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2) < 0) return -1; // TODO: Safe for HEAD requests?
	// TODO: TCP_CORK?
	return 0;
}
err_t HTTPConnectionWriteFile(HTTPConnectionRef const conn, uv_file const file) {
	// TODO: How do we use uv_fs_sendfile to a TCP stream? Is it impossible?
	cothread_t const thread = co_active();
	uv_fs_t req = { .data = thread };
	async_state state = { .thread = thread };
	uv_write_t wreq = { .data = &state };
	byte_t *buf = malloc(WRITE_BUFFER_SIZE);
	int64_t pos = 0;
	for(;;) {
		uv_buf_t const read = uv_buf_init((char *)buf, WRITE_BUFFER_SIZE);
		uv_fs_read(loop, &req, file, &read, 1, pos, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);
		if(0 == req.result) break;
		if(req.result < 0) { // TODO: EAGAIN, etc?
			FREE(&buf);
			return req.result;
		}
		pos += req.result;
		uv_buf_t const write = uv_buf_init((char *)buf, req.result);
		uv_write(&wreq, (uv_stream_t *)conn->stream, &write, 1, async_write_cb);
		co_switch(yield);
		if(state.status < 0) {
			FREE(&buf);
			return state.status;
		}
	}
	FREE(&buf);
	return 0;
}
err_t HTTPConnectionWriteChunkLength(HTTPConnectionRef const conn, uint64_t const length) {
	if(!conn) return 0;
	str_t *str;
	int const slen = asprintf(&str, "%llx\r\n", (unsigned long long)length);
	if(slen < 0) return -1;
	ssize_t const wlen = HTTPConnectionWrite(conn, (byte_t const *)str, slen);
	FREE(&str);
	return wlen < 0 ? -1 : 0;
}
ssize_t HTTPConnectionWriteChunkv(HTTPConnectionRef const conn, uv_buf_t const parts[], unsigned int const count) {
	if(!conn) return 0;
	uint64_t total = 0;
	for(index_t i = 0; i < count; ++i) total += parts[i].len;
	HTTPConnectionWriteChunkLength(conn, total);
	async_state state = { .thread = co_active() };
	uv_write_t req = { .data = &state };
	uv_write(&req, (uv_stream_t *)conn->stream, parts, count, async_write_cb);
	co_switch(yield);
	// TODO: We have to ensure that uv_write() really wrote everything or else we're messing up the chunked encoding. Returning partial writes doesn't cut it.
	HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2);
	return total;//state.status;
	// TODO: Hack? The status is always zero for me even when it wrote, so is uv_write() guaranteed to write everything?
}
err_t HTTPConnectionWriteChunkFile(HTTPConnectionRef const conn, strarg_t const path) {
	uv_fs_t req = { .data = co_active() };
	uv_fs_open(loop, &req, path, O_RDONLY, 0000, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	if(req.result < 0) {
		return -1;
	}
	uv_file const file = req.result;
	uv_fs_fstat(loop, &req, file, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	if(req.result < 0) {
		uv_fs_close(loop, &req, file, async_fs_cb);
		co_switch(yield);
		uv_fs_req_cleanup(&req);
		return -1;
	}
	uint64_t const size = req.statbuf.st_size;

	if(size) {
		HTTPConnectionWriteChunkLength(conn, size);
		HTTPConnectionWriteFile(conn, file);
		HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2);
	}

	uv_fs_close(loop, &req, file, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	return 0;
}
err_t HTTPConnectionEnd(HTTPConnectionRef const conn) {
	if(!conn) return 0;
	if(HTTPConnectionWrite(conn, (byte_t *)"", 0) < 0) return -1;
	// TODO: Figure out keep-alive. If the client doesn't support it, we should close the connection here.
	return 0;
}

void HTTPConnectionSendMessage(HTTPConnectionRef const conn, uint16_t const status, strarg_t const msg) {
	size_t const len = strlen(msg);
	HTTPConnectionWriteResponse(conn, status, msg);
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/plain; charset=utf-8");
	HTTPConnectionWriteContentLength(conn, len+1);
	HTTPConnectionBeginBody(conn);
	// TODO: Check how HEAD responses should look.
	if(HTTP_HEAD != HTTPConnectionGetRequestMethod(conn)) {
		HTTPConnectionWrite(conn, (byte_t const *)msg, len);
		HTTPConnectionWrite(conn, (byte_t const *)"\n", 1);
	}
	HTTPConnectionEnd(conn);
//	if(status >= 400) fprintf(stderr, "%s: %d %s\n", HTTPConnectionGetRequestURI(conn), (int)status, msg);
}
void HTTPConnectionSendStatus(HTTPConnectionRef const conn, uint16_t const status) {
	strarg_t const msg = statusstr(status);
	HTTPConnectionSendMessage(conn, status, msg);
}
void HTTPConnectionSendFile(HTTPConnectionRef const conn, strarg_t const path, strarg_t const type, int64_t size) {
	uv_fs_t req = { .data = co_active() };
	uv_fs_open(loop, &req, path, O_RDONLY, 0600, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	uv_file const file = req.result;
	if(file < 0) return HTTPConnectionSendStatus(conn, 400); // TODO: Error conversion.
	if(size < 0) {
		uv_fs_fstat(loop, &req, file, async_fs_cb);
		co_switch(yield);
		if(req.result < 0) {
			uv_fs_req_cleanup(&req);
			return HTTPConnectionSendStatus(conn, 400);
		}
		size = req.statbuf.st_size;
	}
	uv_fs_req_cleanup(&req);
	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteContentLength(conn, size);
	if(type) HTTPConnectionWriteHeader(conn, "Content-Type", type);
	HTTPConnectionBeginBody(conn);
	HTTPConnectionWriteFile(conn, file);
	HTTPConnectionEnd(conn);
	uv_fs_close(loop, &req, file, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
}


// INTERNAL


static int on_message_begin(http_parser *const parser) {
	return 0;
}
static int on_url(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	append(conn->requestURI, URI_MAX, at, len);
	return 0;
}
static int on_header_field(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	HeadersAppendFieldChunk(conn->headers, at, len);
	return 0;
}
static int on_header_value(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	HeadersAppendValueChunk(conn->headers, at, len);
	return 0;
}
static int on_headers_complete(http_parser *const parser) {
	HTTPConnectionRef const conn = parser->data;
	HeadersEnd(conn->headers);
	return 0;
}
static int on_body(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	BTAssert(conn->requestURI[0], "Body chunk received out of order");
	BTAssert(!conn->chunkLength, "Chunk already waiting");
	BTAssert(!conn->messageEOF, "Message already complete");
	conn->chunk = (byte_t const *)at;
	conn->chunkLength = len;
	return 0;
}
static int on_message_complete(http_parser *const parser) {
	HTTPConnectionRef const conn = parser->data;
	conn->messageEOF = true;
	return 0;
}
static http_parser_settings const settings = {
	.on_message_begin = on_message_begin,
	.on_url = on_url,
	.on_header_field = on_header_field,
	.on_header_value = on_header_value,
	.on_headers_complete = on_headers_complete,
	.on_body = on_body,
	.on_message_complete = on_message_complete,
};

struct conn_state {
	cothread_t thread;
	HTTPConnectionRef conn;
	ssize_t nread;
};
static void alloc_cb(uv_handle_t *const handle, size_t const suggested_size, uv_buf_t *const buf) {
	struct conn_state *const state = handle->data;
	*buf = uv_buf_init((char *)state->conn->buf, state->conn->len);
}
static void read_cb(uv_stream_t *const stream, ssize_t const nread, const uv_buf_t *const buf) {
	struct conn_state *const state = stream->data;
	state->nread = nread;
	co_switch(state->thread);
}
static ssize_t readOnce(HTTPConnectionRef const conn) {
	if(conn->messageEOF) return -1;
	struct conn_state state = {
		.thread = co_active(),
		.conn = conn,
	};
	conn->stream->data = &state;
	for(;;) {
		uv_read_start((uv_stream_t *)conn->stream, alloc_cb, read_cb);
		co_switch(yield);
		uv_read_stop((uv_stream_t *)conn->stream);
		if(state.nread) break;
	}
	if(UV_EOF == state.nread) {
		conn->streamEOF = true;
		state.nread = 0;
	}
	if(state.nread < 0) return -1;
	size_t const plen = http_parser_execute(conn->parser, &settings, (char const *)conn->buf, state.nread);
	if(plen != state.nread) return -1;
	return plen;
}
static err_t readHeaders(HTTPConnectionRef const conn) {
	for(;;) {
		if(readOnce(conn) < 0) return -1;
		if(conn->chunkLength || conn->messageEOF) return 0;
		if(conn->streamEOF) return -1;
	}
}

