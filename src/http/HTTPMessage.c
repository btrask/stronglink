#define _GNU_SOURCE
#include "../async.h"
#include "HTTPMessage.h"
#include "status.h"

#define BUFFER_SIZE (1024 * 8)
#define URI_MAX 1024

struct HTTPConnection {
	uv_tcp_t stream;
	http_parser parser;
	byte_t *buf;
};

HTTPConnectionRef HTTPConnectionCreateIncoming(uv_stream_t *const socket) {
	HTTPConnectionRef const conn = calloc(1, sizeof(struct HTTPConnection));
	if(!conn) return NULL;
	if(
		uv_tcp_init(loop, &conn->stream) < 0 ||
		uv_accept(socket, (uv_stream_t *)&conn->stream) < 0
	) {
		HTTPConnectionFree(conn);
		return NULL;
	}
	http_parser_init(&conn->parser, HTTP_REQUEST);
	conn->buf = malloc(BUFFER_SIZE);
	return conn;
}
void HTTPConnectionFree(HTTPConnectionRef const conn) {
	if(!conn) return;
	conn->stream.data = co_active();
	uv_close((uv_handle_t *)&conn->stream, async_close_cb);
	co_switch(yield);
	conn->stream.data = NULL;
	FREE(&conn->buf);
	free(conn);
}
err_t HTTPConnectionError(HTTPConnectionRef const conn) {
	if(!conn) return HPE_OK;
	return HTTP_PARSER_ERRNO(&conn->parser);
}

struct HTTPMessage {
	HTTPConnectionRef conn;
	off_t pos;
	size_t remaining;

	// Incoming
	str_t *requestURI;
	HeadersRef headers;
	struct {
		char const *at;
		size_t len;
	} next;
	bool_t eof;

	// Outgoing
	// nothing yet...
};

static err_t readOnce(HTTPMessageRef const msg);

HTTPMessageRef HTTPMessageCreateIncoming(HTTPConnectionRef const conn) {
	assertf(conn, "HTTPMessage connection required");
	HTTPMessageRef const msg = calloc(1, sizeof(struct HTTPMessage));
	if(!msg) return NULL;
	msg->conn = conn;
	conn->parser.data = msg;
	msg->requestURI = malloc(URI_MAX+1);
	msg->requestURI[0] = '\0';
	for(;;) {
		if(readOnce(msg) < 0) {
			HTTPMessageFree(msg);
			return NULL;
		}
		if(HPE_PAUSED == HTTPConnectionError(msg->conn)) break;
		if(msg->eof) {
			HTTPMessageFree(msg);
			return NULL;
		}
	}
	assertf(msg->requestURI[0], "No URI in request");
	return msg;
}
void HTTPMessageFree(HTTPMessageRef const msg) {
	if(!msg) return;
	FREE(&msg->requestURI);
	HeadersFree(msg->headers);
	msg->conn->parser.data = NULL;
	msg->conn = NULL;
	free(msg);
}

HTTPMethod HTTPMessageGetRequestMethod(HTTPMessageRef const msg) {
	if(!msg) return 0;
	return msg->conn->parser.method;
}
strarg_t HTTPMessageGetRequestURI(HTTPMessageRef const msg) {
	if(!msg) return NULL;
	return msg->requestURI;
}
void *HTTPMessageGetHeaders(HTTPMessageRef const msg, HeaderField const fields[], count_t const count) {
	if(!msg) return NULL;
	assertf(!msg->headers, "Message headers already read");
	msg->headers = HeadersCreate(fields, count);
	if(msg->next.len) {
		HeadersAppendFieldChunk(msg->headers, msg->next.at, msg->next.len);
		msg->next.at = NULL;
		msg->next.len = 0;
	}
	for(;;) {
		if(readOnce(msg) < 0) return NULL;
		if(HPE_PAUSED == HTTPConnectionError(msg->conn)) break;
		if(msg->eof) return NULL;
	}
	return HeadersGetData(msg->headers);
}
ssize_t HTTPMessageRead(HTTPMessageRef const msg, byte_t *const buf, size_t const len) {
	if(!msg) return -1;
	if(!msg->headers) HTTPMessageGetHeaders(msg, NULL, 0);
	if(!msg->next.len) return msg->eof ? 0 : -1;
	size_t const used = MIN(len, msg->next.len);
	memcpy(buf, msg->next.at, used);
	msg->next.at += used;
	msg->next.len -= used;
	if(!msg->eof && -1 == readOnce(msg)) return -1;
	return used;
}
ssize_t HTTPMessageGetBuffer(HTTPMessageRef const msg, byte_t const **const buf) {
	if(!msg) return -1;
	if(!msg->headers) HTTPMessageGetHeaders(msg, NULL, 0);
	if(!msg->next.len) {
		if(msg->eof) return 0;
		if(readOnce(msg) < 0) return -1;
	}
	size_t const used = msg->next.len;
	*buf = (byte_t const *)msg->next.at;
	msg->next.at = NULL;
	msg->next.len = 0;
	return used;
}
void HTTPMessageDrain(HTTPMessageRef const msg) {
	if(!msg) return;
	if(msg->eof) return;
	do {
		msg->next.at = NULL;
		msg->next.len = 0;
	} while(readOnce(msg) >= 0);
	assertf(msg->eof, "Message drain didn't reach EOF");
}

ssize_t HTTPMessageWrite(HTTPMessageRef const msg, byte_t const *const buf, size_t const len) {
	if(!msg) return 0;
	uv_buf_t obj = uv_buf_init((char *)buf, len);
	async_state state = { .thread = co_active() };
	uv_write_t req = { .data = &state };
	uv_write(&req, (uv_stream_t *)&msg->conn->stream, &obj, 1, async_write_cb);
	co_switch(yield);
	return state.status;
}
ssize_t HTTPMessageWritev(HTTPMessageRef const msg, uv_buf_t const parts[], unsigned int const count) {
	if(!msg) return 0;
	async_state state = { .thread = co_active() };
	uv_write_t req = { .data = &state };
	uv_write(&req, (uv_stream_t *)&msg->conn->stream, parts, count, async_write_cb);
	co_switch(yield);
	return state.status;
}
err_t HTTPMessageWriteRequest(HTTPMessageRef const msg, HTTPMethod const method, strarg_t const requestURI, strarg_t const host) {
	if(!msg) return 0;
	strarg_t methodstr = http_method_str(method);
	uv_buf_t parts[] = {
		uv_buf_init((char *)methodstr, strlen(methodstr)),
		uv_buf_init(" ", 1),
		uv_buf_init((char *)requestURI, strlen(requestURI)),
		uv_buf_init(" HTTP/1.1\r\n", 11),
		uv_buf_init("Host: ", 6),
		uv_buf_init((char *)host, strlen(host)),
		uv_buf_init("\r\n", 2),
	};
	ssize_t const wlen = HTTPMessageWritev(msg, parts, numberof(parts));
	return wlen < 0 ? -1 : 0;
}
err_t HTTPMessageWriteResponse(HTTPMessageRef const msg, uint16_t const status, strarg_t const message) {
	if(!msg) return 0;
	// TODO: TCP_CORK?
	// TODO: Suppply our own message for known status codes.
	str_t *str;
	int const slen = asprintf(&str, "HTTP/1.1 %d %s\r\n", status, message);
	if(slen < 0) return -1;
	ssize_t const wlen = HTTPMessageWrite(msg, (byte_t *)str, slen);
	FREE(&str);
	return wlen < 0 ? -1 : 0;
}
err_t HTTPMessageWriteHeader(HTTPMessageRef const msg, strarg_t const field, strarg_t const value) {
	if(!msg) return 0;
	uv_buf_t parts[] = {
		uv_buf_init((char *)field, strlen(field)),
		uv_buf_init(": ", 2),
		uv_buf_init((char *)value, strlen(value)),
		uv_buf_init("\r\n", 2),
	};
	ssize_t const wlen = HTTPMessageWritev(msg, parts, numberof(parts));
	return wlen < 0 ? -1 : 0;
}
err_t HTTPMessageWriteContentLength(HTTPMessageRef const msg, uint64_t const length) {
	if(!msg) return 0;
	str_t *str;
	int const slen = asprintf(&str, "Content-Length: %llu\r\n", (unsigned long long)length);
	if(slen < 0) return -1;
	ssize_t const wlen = HTTPMessageWrite(msg, (byte_t *)str, slen);
	FREE(&str);
	return wlen < 0 ? -1 : 0;
}
err_t HTTPMessageWriteSetCookie(HTTPMessageRef const msg, strarg_t const field, strarg_t const value, strarg_t const path, uint64_t const maxage) {
	if(!msg) return 0;
	str_t *str;
	unsigned long long const x = maxage;
	int const slen = asprintf(&str, "Set-Cookie: %s=%s; Path=%s; Max-Age=%llu; HttpOnly\r\n", field, value, path, x);
	if(slen < 0) return -1;
	ssize_t const wlen = HTTPMessageWrite(msg, (byte_t *)str, slen);
	FREE(&str);
	return wlen < 0 ? -1 : 0;
}
err_t HTTPMessageBeginBody(HTTPMessageRef const msg) {
	if(!msg) return 0;
	if(HTTPMessageWriteHeader(msg, "Connection", "keep-alive") < 0) return -1; // TODO: Make sure we're doing this right.
	if(HTTPMessageWrite(msg, (byte_t const *)"\r\n", 2) < 0) return -1; // TODO: Safe for HEAD requests?
	// TODO: TCP_CORK?
	return 0;
}
err_t HTTPMessageWriteFile(HTTPMessageRef const msg, uv_file const file) {
	// TODO: How do we use uv_fs_sendfile to a TCP stream? Is it impossible?
	cothread_t const thread = co_active();
	uv_fs_t req = { .data = thread };
	async_state state = { .thread = thread };
	uv_write_t wreq = { .data = &state };
	byte_t *buf = malloc(BUFFER_SIZE);
	int64_t pos = 0;
	for(;;) {
		uv_buf_t const read = uv_buf_init((char *)buf, BUFFER_SIZE);
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
		uv_write(&wreq, (uv_stream_t *)&msg->conn->stream, &write, 1, async_write_cb);
		co_switch(yield);
		if(state.status < 0) {
			FREE(&buf);
			return state.status;
		}
	}
	FREE(&buf);
	return 0;
}
err_t HTTPMessageWriteChunkLength(HTTPMessageRef const msg, uint64_t const length) {
	if(!msg) return 0;
	str_t *str;
	int const slen = asprintf(&str, "%llx\r\n", (unsigned long long)length);
	if(slen < 0) return -1;
	ssize_t const wlen = HTTPMessageWrite(msg, (byte_t const *)str, slen);
	FREE(&str);
	return wlen < 0 ? -1 : 0;
}
ssize_t HTTPMessageWriteChunkv(HTTPMessageRef const msg, uv_buf_t const parts[], unsigned int const count) {
	if(!msg) return 0;
	uint64_t total = 0;
	for(index_t i = 0; i < count; ++i) total += parts[i].len;
	HTTPMessageWriteChunkLength(msg, total);
	async_state state = { .thread = co_active() };
	uv_write_t req = { .data = &state };
	uv_write(&req, (uv_stream_t *)&msg->conn->stream, parts, count, async_write_cb);
	co_switch(yield);
	// TODO: We have to ensure that uv_write() really wrote everything or else we're messing up the chunked encoding. Returning partial writes doesn't cut it.
	HTTPMessageWrite(msg, (byte_t const *)"\r\n", 2);
	return total;//state.status;
	// TODO: Hack? The status is always zero for me even when it wrote, so is uv_write() guaranteed to write everything?
}
err_t HTTPMessageWriteChunkFile(HTTPMessageRef const msg, strarg_t const path) {
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
		HTTPMessageWriteChunkLength(msg, size);
		HTTPMessageWriteFile(msg, file);
		HTTPMessageWrite(msg, (byte_t const *)"\r\n", 2);
	}

	uv_fs_close(loop, &req, file, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	return 0;
}
err_t HTTPMessageEnd(HTTPMessageRef const msg) {
	if(!msg) return 0;
	if(HTTPMessageWrite(msg, (byte_t *)"", 0) < 0) return -1;
	// TODO: Figure out keep-alive.
	return 0;
}

void HTTPMessageSendMessage(HTTPMessageRef const msg, uint16_t const status, strarg_t const str) {
	size_t const len = strlen(str);
	HTTPMessageWriteResponse(msg, status, str);
	HTTPMessageWriteHeader(msg, "Content-Type", "text/plain; charset=utf-8");
	HTTPMessageWriteContentLength(msg, len+1);
	HTTPMessageBeginBody(msg);
	// TODO: Check how HEAD responses should look.
	if(HTTP_HEAD != HTTPMessageGetRequestMethod(msg)) {
		HTTPMessageWrite(msg, (byte_t const *)str, len);
		HTTPMessageWrite(msg, (byte_t const *)"\n", 1);
	}
	HTTPMessageEnd(msg);
//	if(status >= 400) fprintf(stderr, "%s: %d %s\n", HTTPMessageGetRequestURI(msg), (int)status, str);
}
void HTTPMessageSendStatus(HTTPMessageRef const msg, uint16_t const status) {
	strarg_t const str = statusstr(status);
	HTTPMessageSendMessage(msg, status, str);
}
void HTTPMessageSendFile(HTTPMessageRef const msg, strarg_t const path, strarg_t const type, int64_t size) {
	uv_fs_t req = { .data = co_active() };
	uv_fs_open(loop, &req, path, O_RDONLY, 0600, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	uv_file const file = req.result;
	if(file < 0) return HTTPMessageSendStatus(msg, 400); // TODO: Error conversion.
	if(size < 0) {
		uv_fs_fstat(loop, &req, file, async_fs_cb);
		co_switch(yield);
		if(req.result < 0) {
			uv_fs_req_cleanup(&req);
			return HTTPMessageSendStatus(msg, 400);
		}
		size = req.statbuf.st_size;
	}
	uv_fs_req_cleanup(&req);
	HTTPMessageWriteResponse(msg, 200, "OK");
	HTTPMessageWriteContentLength(msg, size);
	if(type) HTTPMessageWriteHeader(msg, "Content-Type", type);
	HTTPMessageBeginBody(msg);
	HTTPMessageWriteFile(msg, file);
	HTTPMessageEnd(msg);
	uv_fs_close(loop, &req, file, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
}


// INTERNAL


static int on_message_begin(http_parser *const parser) {
	return 0;
}
static int on_url(http_parser *const parser, char const *const at, size_t const len) {
	HTTPMessageRef const msg = parser->data;
	append(msg->requestURI, URI_MAX, at, len);
	return 0;
}
static int on_header_field(http_parser *const parser, char const *const at, size_t const len) {
	HTTPMessageRef const msg = parser->data;
	if(msg->headers) {
		if(msg->next.at) {
			HeadersAppendFieldChunk(msg->headers, msg->next.at, msg->next.len);
			msg->next.at = NULL;
			msg->next.len = 0;
		}
		HeadersAppendFieldChunk(msg->headers, at, len);
	} else {
		assertf(!msg->next.len, "Chunk already waiting");
		msg->next.at = at;
		msg->next.len = len;
		http_parser_pause(parser, 1);
	}
	return 0;
}
static int on_header_value(http_parser *const parser, char const *const at, size_t const len) {
	HTTPMessageRef const msg = parser->data;
	HeadersAppendValueChunk(msg->headers, at, len);
	return 0;
}
static int on_headers_complete(http_parser *const parser) {
	HTTPMessageRef const msg = parser->data;
	HeadersEnd(msg->headers);
	http_parser_pause(parser, 1);
	return 0;
}
static int on_body(http_parser *const parser, char const *const at, size_t const len) {
	HTTPMessageRef const msg = parser->data;
	assertf(msg->requestURI[0], "Body chunk received out of order");
	assertf(!msg->next.len, "Chunk already waiting");
	assertf(!msg->eof, "Message already complete");
	msg->next.at = at;
	msg->next.len = len;
	http_parser_pause(parser, 1);
	return 0;
}
static int on_message_complete(http_parser *const parser) {
	HTTPMessageRef const msg = parser->data;
	msg->eof = true;
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

struct msg_state {
	cothread_t thread;
	HTTPMessageRef msg;
	ssize_t nread;
};
static void alloc_cb(uv_handle_t *const handle, size_t const suggested_size, uv_buf_t *const buf) {
	struct msg_state *const state = handle->data;
	*buf = uv_buf_init((char *)state->msg->conn->buf, BUFFER_SIZE);
}
static void read_cb(uv_stream_t *const stream, ssize_t const nread, const uv_buf_t *const buf) {
	struct msg_state *const state = stream->data;
	state->nread = nread;
	co_switch(state->thread);
}
static ssize_t readOnce(HTTPMessageRef const msg) {
	assertf(!msg->next.len, "Existing unused chunk");
	if(msg->eof) return -1;
	if(!msg->remaining) {
		struct msg_state state = {
			.thread = co_active(),
			.msg = msg,
		};
		msg->conn->stream.data = &state;
		for(;;) {
			uv_read_start((uv_stream_t *)&msg->conn->stream, alloc_cb, read_cb);
			co_switch(yield);
			uv_read_stop((uv_stream_t *)&msg->conn->stream);
			if(state.nread) break;
		}
		if(UV_EOF == state.nread) {
			msg->eof = true;
			state.nread = 0;
		}
		if(state.nread < 0) return -1;
		msg->pos = 0;
		msg->remaining = state.nread;
	}
	http_parser_pause(&msg->conn->parser, 0);
	size_t const plen = http_parser_execute(&msg->conn->parser, &settings, (char const *)msg->conn->buf + msg->pos, msg->remaining);
	if(plen != msg->remaining && HPE_PAUSED != HTTPConnectionError(msg->conn)) {
		fprintf(stderr, "HTTP parse error %s (%d)\n",
			http_errno_name(HTTPConnectionError(msg->conn)),
			HTTP_PARSER_ERRNO_LINE(&msg->conn->parser));
		msg->eof = true; // Make sure we don't read anymore. HTTPMessageDrain() expects this too.
		return -1;
	}
	msg->pos += plen;
	msg->remaining -= plen;
	return 0;
}

