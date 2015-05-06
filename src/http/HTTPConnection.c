// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include "HTTPConnection.h"
#include "status.h"

#define BUFFER_SIZE (1024 * 8)

enum {
	HTTPMessageIncomplete = 1 << 0,
	HTTPStreamEOF = 1 << 1,
};

static http_parser_settings const settings;

// TODO: Copied from Headers.h, which should go away eventually.
// TODO: Replace with strlcat or something.
static size_t append(str_t *const dst, size_t const dsize, strarg_t const src, size_t const slen) {
	if(!dsize) return 0;
	size_t const olen = strlen(dst);
	size_t const nlen = MIN(olen + slen, dsize-1);
	memcpy(dst + olen, src, nlen - olen);
	dst[nlen] = '\0';
	return nlen - olen;
}

static ssize_t readall(uv_file const file, uv_buf_t const *const buf) {
	async_pool_enter(NULL);
	size_t pos = 0;
	ssize_t rc;
	for(;;) {
		uv_buf_t b2 = uv_buf_init(buf->base+pos, buf->len-pos);
		rc = async_fs_read(file, &b2, 1, -1);
		if(rc < 0) break;
		if(0 == rc) { rc = pos; break; }
		pos += rc;
		if(pos >= buf->len) { rc = pos; break; }
	}
	async_pool_leave(NULL);
	return rc;
}



struct HTTPConnection {
	uv_tcp_t stream[1];
	http_parser parser[1];

	void *buf;
	uv_buf_t raw[1];

	HTTPEvent type;
	uv_buf_t out[1];

	unsigned flags;
};

int HTTPConnectionCreateIncoming(uv_stream_t *const socket, HTTPConnectionRef *const out) {
	HTTPConnectionRef conn = calloc(1, sizeof(struct HTTPConnection));
	if(!conn) return UV_ENOMEM;
	int rc = uv_tcp_init(loop, conn->stream);
	if(rc < 0) goto cleanup;
	rc = uv_accept(socket, (uv_stream_t *)conn->stream);
	if(rc < 0) goto cleanup;
	http_parser_init(conn->parser, HTTP_REQUEST);
	conn->parser->data = conn;
	*out = conn; conn = NULL;
cleanup:
	HTTPConnectionFree(&conn);
	return rc;
}
int HTTPConnectionCreateOutgoing(strarg_t const domain, HTTPConnectionRef *const out) {
	str_t host[1023+1];
	str_t service[15+1];
	host[0] = '\0';
	service[0] = '\0';
	int matched = sscanf(domain, "%1023[^:]:%15[0-9]", host, service);
	if(matched < 1) return UV_EINVAL;
	if('\0' == host[0]) return UV_EINVAL;

	static struct addrinfo const hints = {
		.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_NUMERICSERV,
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = 0, // ???
	};
	struct addrinfo *info = NULL;
	HTTPConnectionRef conn = NULL;
	int rc;

	rc = async_getaddrinfo(host, service[0] ? service : "80", &hints, &info);
	if(rc < 0) goto cleanup;

	conn = calloc(1, sizeof(struct HTTPConnection));
	if(!conn) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	rc = UV_EADDRNOTAVAIL;
	for(struct addrinfo *each = info; each; each = each->ai_next) {
		rc = uv_tcp_init(loop, conn->stream);
		if(rc < 0) break;

		rc = async_tcp_connect(conn->stream, each->ai_addr);
		if(rc >= 0) break;

		async_close((uv_handle_t *)conn->stream);
	}
	if(rc < 0) goto cleanup;

	http_parser_init(conn->parser, HTTP_RESPONSE);
	conn->parser->data = conn;
	*out = conn; conn = NULL;

cleanup:
	uv_freeaddrinfo(info); info = NULL;
	HTTPConnectionFree(&conn);
	return rc;
}
void HTTPConnectionFree(HTTPConnectionRef *const connptr) {
	HTTPConnectionRef conn = *connptr;
	if(!conn) return;

	async_close((uv_handle_t *)conn->stream);

	// http_parser does not need to be freed, closed or destroyed.
	memset(conn->parser, 0, sizeof(*conn->parser));

	FREE(&conn->buf);
	*conn->raw = uv_buf_init(NULL, 0);

	conn->type = HTTPNothing;
	*conn->out = uv_buf_init(NULL, 0);

	conn->flags = 0;

	assert_zeroed(conn, 1);
	FREE(connptr); conn = NULL;
}
int HTTPConnectionPeek(HTTPConnectionRef const conn, HTTPEvent *const type, uv_buf_t *const buf) {
	if(!conn) return UV_EINVAL;
	if(!type) return UV_EINVAL;
	if(!buf) return UV_EINVAL;
	size_t len;
	int rc;

	if(HTTPStreamEOF & conn->flags) return UV_EOF;

	// Repeat previous errors.
	rc = HTTP_PARSER_ERRNO(conn->parser);
	if(HPE_OK != rc && HPE_PAUSED != rc) return UV_UNKNOWN;

	for(;;) {
		if(HTTPNothing != conn->type) break;
		if(!conn->raw->len) {
			// It might seem counterintuitive to free the buffer
			// just before we could reuse it, but the one time we
			// don't need it is while blocking. We could free it
			// after a timeout to give us a chance to reuse it,
			// but even the two second timeout Apache uses causes
			// a lot of problems...
			FREE(&conn->buf);
			*conn->raw = uv_buf_init(NULL, 0);
			*conn->out = uv_buf_init(NULL, 0);

			rc = async_read((uv_stream_t *)conn->stream, conn->raw);
			if(UV_EOF == rc) conn->flags |= HTTPStreamEOF;
			if(rc < 0) return rc;
			conn->buf = conn->raw->base;
		}
		http_parser_pause(conn->parser, 0);
		len = http_parser_execute(conn->parser, &settings, conn->raw->base, conn->raw->len);
		rc = HTTP_PARSER_ERRNO(conn->parser);
		conn->raw->base += len;
		conn->raw->len -= len;
		if(HPE_OK != rc && HPE_PAUSED != rc) {
			fprintf(stderr, "HTTP parse error %s (%d)\n",
				http_errno_name(rc),
				HTTP_PARSER_ERRNO_LINE(conn->parser));
//			fprintf(stderr, "%s (%lu)\n", strndup(conn->raw->base, conn->raw->len), conn->raw->len);
			return UV_UNKNOWN;
		}
	}
	assertf(HTTPNothing != conn->type, "HTTPConnectionPeek must return an event");
	*type = conn->type;
	*buf = *conn->out;
	return 0;
}
void HTTPConnectionPop(HTTPConnectionRef const conn, size_t const len) {
	if(!conn) return;
	assert(len <= conn->out->len);
	conn->out->base += len;
	conn->out->len -= len;
	if(conn->out->len) return;
	conn->type = HTTPNothing;
	conn->out->base = NULL;
}


int HTTPConnectionReadRequest(HTTPConnectionRef const conn, HTTPMethod *const method, str_t *const out, size_t const max) {
	if(!conn) return UV_EINVAL;
	if(!max) return UV_EINVAL;
	uv_buf_t buf[1];
	int rc;
	HTTPEvent type;
	size_t len = 0;
	for(;;) {
		// Use unref because we shouldn't block the server
		// on a request that may never arrive.
		uv_unref((uv_handle_t *)conn->stream);
		rc = HTTPConnectionPeek(conn, &type, buf);
		uv_ref((uv_handle_t *)conn->stream);
		if(rc < 0) return rc;
		if(HTTPHeaderField == type || HTTPHeadersComplete == type) break;
		HTTPConnectionPop(conn, buf->len);
		if(HTTPMessageBegin == type) continue;
		if(HTTPURL != type) {
			assertf(0, "Unexpected HTTP event %d", type);
			return UV_UNKNOWN;
		}
		if(len+buf->len+1 > max) return UV_EMSGSIZE;
		memcpy(out+len, buf->base, buf->len);
		len += buf->len;
		out[len] = '\0';
	}
	*method = conn->parser->method;
	return 0;
}
int HTTPConnectionReadResponseStatus(HTTPConnectionRef const conn) {
	if(!conn) return UV_EINVAL;
	uv_buf_t buf[1];
	int rc;
	HTTPEvent type;
	for(;;) {
		rc = HTTPConnectionPeek(conn, &type, buf);
		if(rc < 0) return rc;
		if(HTTPHeaderField == type || HTTPHeadersComplete == type) break;
		if(HTTPMessageBegin != type) {
			assertf(0, "Unexpected HTTP event %d", type);
			return UV_UNKNOWN;
		}
		HTTPConnectionPop(conn, buf->len);
	}
	return conn->parser->status_code;
}

int HTTPConnectionReadHeaderField(HTTPConnectionRef const conn, str_t field[], size_t const max) {
	if(!conn) return UV_EINVAL;
	uv_buf_t buf[1];
	int rc;
	HTTPEvent type;
	if(max > 0) field[0] = '\0';
	for(;;) {
		rc = HTTPConnectionPeek(conn, &type, buf);
		if(rc < 0) return rc;
		if(HTTPHeaderValue == type) break;
		if(HTTPHeadersComplete == type) break;
		HTTPConnectionPop(conn, buf->len);
		if(HTTPHeaderField != type) {
			assertf(0, "Unexpected HTTP event %d", type);
			return UV_UNKNOWN;
		}
		append(field, max, buf->base, buf->len);
	}
	return 0;
}
int HTTPConnectionReadHeaderValue(HTTPConnectionRef const conn, str_t value[], size_t const max) {
	if(!conn) return UV_EINVAL;
	uv_buf_t buf[1];
	int rc;
	HTTPEvent type;
	if(max > 0) value[0] = '\0';
	for(;;) {
		rc = HTTPConnectionPeek(conn, &type, buf);
		if(rc < 0) return rc;
		if(HTTPHeaderField == type) break;
		if(HTTPHeadersComplete == type) break;
		HTTPConnectionPop(conn, buf->len);
		if(HTTPHeaderValue != type) {
			assertf(0, "Unexpected HTTP event %d", type);
			return UV_UNKNOWN;
		}
		append(value, max, buf->base, buf->len);
	}
	return 0;
}
int HTTPConnectionReadBody(HTTPConnectionRef const conn, uv_buf_t *const buf) {
	if(!conn) return UV_EINVAL;
	HTTPEvent type;
	int rc = HTTPConnectionPeek(conn, &type, buf);
	if(rc < 0) return rc;
	if(HTTPBody != type && HTTPMessageEnd != type) {
		assertf(0, "Unexpected HTTP event %d", type);
		return UV_UNKNOWN;
	}
	HTTPConnectionPop(conn, buf->len);
	return 0;
}
int HTTPConnectionReadBodyLine(HTTPConnectionRef const conn, str_t out[], size_t const max) {
	if(!conn) return UV_EINVAL;
	if(!max) return UV_EINVAL;
	uv_buf_t buf[1];
	int rc;
	size_t i;
	HTTPEvent type;
	out[0] = '\0';
	for(;;) {
		rc = HTTPConnectionPeek(conn, &type, buf);
		if(rc < 0) return rc;
		if(HTTPMessageEnd == type) {
			if(out[0]) return 0;
			HTTPConnectionPop(conn, buf->len);
			return UV_EOF;
		}
		if(HTTPBody != type) {
			assertf(0, "Unexpected HTTP event %d", type);
			return UV_UNKNOWN;
		}
		for(i = 0; i < buf->len; ++i) {
			if('\r' == buf->base[i]) break;
			if('\n' == buf->base[i]) break;
		}
		append(out, max, buf->base, i);
		HTTPConnectionPop(conn, i);
		if(i < buf->len) break;
	}

	rc = HTTPConnectionPeek(conn, &type, buf);
	if(rc < 0) return rc;
	if(HTTPMessageEnd == type) {
		if(out[0]) return 0;
		HTTPConnectionPop(conn, i);
		return UV_EOF;
	}
	if(HTTPBody != type) return UV_UNKNOWN;
	if('\r' == buf->base[0]) HTTPConnectionPop(conn, 1);

	rc = HTTPConnectionPeek(conn, &type, buf);
	if(rc < 0) return rc;
	if(HTTPMessageEnd == type) {
		if(out[0]) return 0;
		HTTPConnectionPop(conn, i);
		return UV_EOF;
	}
	if(HTTPBody != type) return UV_UNKNOWN;
	if('\n' == buf->base[0]) HTTPConnectionPop(conn, 1);

	return 0;
}
ssize_t HTTPConnectionReadBodyStatic(HTTPConnectionRef const conn, byte_t *const out, size_t const max) {
	if(!conn) return UV_EINVAL;
	ssize_t len = 0;
	for(;;) {
		uv_buf_t buf[1];
		int rc = HTTPConnectionReadBody(conn, buf);
		if(rc < 0) return rc;
		if(!buf->len) break;
		if(len+buf->len >= max) return UV_EMSGSIZE;
		memcpy(out, buf->base, buf->len);
		len += buf->len;
	}
	return len;
}
int HTTPConnectionDrainMessage(HTTPConnectionRef const conn) {
	if(!conn) return 0;
	if(HTTPStreamEOF & conn->flags) return UV_EOF;
	if(!(HTTPMessageIncomplete & conn->flags)) return 0;
	uv_buf_t buf[1];
	int rc;
	HTTPEvent type;
	for(;;) {
		rc = HTTPConnectionPeek(conn, &type, buf);
		if(rc < 0) return rc;
		if(HTTPMessageBegin == type) {
			assertf(0, "HTTPConnectionDrainMessage shouldn't start a new message");
			return UV_UNKNOWN;
		}
		HTTPConnectionPop(conn, buf->len);
		if(HTTPMessageEnd == type) break;
	}
	return 0;
}


int HTTPConnectionWrite(HTTPConnectionRef const conn, byte_t const *const buf, size_t const len) {
	if(!conn) return 0;
	uv_buf_t parts[1] = { uv_buf_init((char *)buf, len) };
	return async_write((uv_stream_t *)conn->stream, parts, numberof(parts));
}
int HTTPConnectionWritev(HTTPConnectionRef const conn, uv_buf_t const parts[], unsigned int const count) {
	if(!conn) return 0;
	return async_write((uv_stream_t *)conn->stream, parts, count);
}
int HTTPConnectionWriteRequest(HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const requestURI, strarg_t const host) {
	if(!conn) return 0;
	strarg_t methodstr = http_method_str(method);
	uv_buf_t parts[] = {
		uv_buf_init((char *)methodstr, strlen(methodstr)),
		uv_buf_init(STR_LEN(" ")),
		uv_buf_init((char *)requestURI, strlen(requestURI)),
		uv_buf_init(STR_LEN(" HTTP/1.1\r\n")),
		uv_buf_init(STR_LEN("Host: ")),
		uv_buf_init((char *)host, strlen(host)),
		uv_buf_init(STR_LEN("\r\n")),
	};
	return HTTPConnectionWritev(conn, parts, numberof(parts));
}

int HTTPConnectionWriteResponse(HTTPConnectionRef const conn, uint16_t const status, strarg_t const message) {
	assertf(status >= 100 && status < 600, "Invalid HTTP status %d", (int)status);
	if(!conn) return 0;

	str_t status_str[4+1];
	int status_len = snprintf(status_str, sizeof(status_str), "%d", status);
	assert(3 == status_len);

	uv_buf_t parts[] = {
		uv_buf_init(STR_LEN("HTTP/1.1 ")),
		uv_buf_init(status_str, status_len),
		uv_buf_init(STR_LEN(" ")),
		uv_buf_init((char *)message, strlen(message)),
		uv_buf_init(STR_LEN("\r\n")),
	};
	return HTTPConnectionWritev(conn, parts, numberof(parts));
}
int HTTPConnectionWriteHeader(HTTPConnectionRef const conn, strarg_t const field, strarg_t const value) {
	assert(field);
	assert(value);
	if(!conn) return 0;
	uv_buf_t parts[] = {
		uv_buf_init((char *)field, strlen(field)),
		uv_buf_init(STR_LEN(": ")),
		uv_buf_init((char *)value, strlen(value)),
		uv_buf_init(STR_LEN("\r\n")),
	};
	return HTTPConnectionWritev(conn, parts, numberof(parts));
}
int HTTPConnectionWriteContentLength(HTTPConnectionRef const conn, uint64_t const length) {
	if(!conn) return 0;
	str_t str[16];
	int const len = snprintf(str, sizeof(str), "%llu", (unsigned long long)length);
	uv_buf_t parts[] = {
		uv_buf_init(STR_LEN("Content-Length: ")),
		uv_buf_init(str, len),
		uv_buf_init(STR_LEN("\r\n")),
	};
	return HTTPConnectionWritev(conn, parts, numberof(parts));
}
int HTTPConnectionWriteSetCookie(HTTPConnectionRef const conn, strarg_t const cookie, strarg_t const path, uint64_t const maxage) {
	assert(cookie);
	assert(path);
	if(!conn) return 0;
	str_t maxage_str[16];
	int const maxage_len = snprintf(maxage_str, sizeof(maxage_str), "%llu", (unsigned long long)maxage);
	uv_buf_t parts[] = {
		uv_buf_init(STR_LEN("Set-Cookie: ")),
		uv_buf_init((char *)cookie, strlen(cookie)),
		uv_buf_init(STR_LEN("; Path=")),
		uv_buf_init((char *)path, strlen(path)),
		uv_buf_init(STR_LEN("; Max-Age=")),
		uv_buf_init(maxage_str, maxage_len),
		uv_buf_init(STR_LEN("; HttpOnly\r\n")),
	};
	return HTTPConnectionWritev(conn, parts, numberof(parts));
}
int HTTPConnectionBeginBody(HTTPConnectionRef const conn) {
	if(!conn) return 0;
	return HTTPConnectionWrite(conn, (byte_t *)STR_LEN(
		"Connection: keep-alive\r\n" // TODO
		"\r\n"));
}
int HTTPConnectionWriteFile(HTTPConnectionRef const conn, uv_file const file) {
	byte_t *buf = malloc(BUFFER_SIZE);
	if(!buf) return UV_ENOMEM;
	uv_buf_t const info = uv_buf_init((char *)buf, BUFFER_SIZE);
	for(;;) {
		ssize_t const len = readall(file, &info);
		if(0 == len) break;
		if(len < 0) {
			FREE(&buf);
			return (int)len;
		}
		uv_buf_t const write = uv_buf_init((char *)buf, len);
		ssize_t written = async_write((uv_stream_t *)conn->stream, &write, 1);
		if(written < 0) {
			FREE(&buf);
			return (int)written;
		}
	}
	FREE(&buf);
	return 0;
}
int HTTPConnectionWriteChunkLength(HTTPConnectionRef const conn, uint64_t const length) {
	if(!conn) return 0;
	str_t str[16];
	int const slen = snprintf(str, sizeof(str), "%llx\r\n", (unsigned long long)length);
	if(slen < 0) return UV_UNKNOWN;
	return HTTPConnectionWrite(conn, (byte_t const *)str, slen);
}
int HTTPConnectionWriteChunkv(HTTPConnectionRef const conn, uv_buf_t const parts[], unsigned int const count) {
	if(!conn) return 0;
	uint64_t total = 0;
	for(size_t i = 0; i < count; i++) total += parts[i].len;
	if(total <= 0) return 0;
	int rc = 0;
	rc = rc < 0 ? rc : HTTPConnectionWriteChunkLength(conn, total);
	rc = rc < 0 ? rc : async_write((uv_stream_t *)conn->stream, parts, count);
	rc = rc < 0 ? rc : HTTPConnectionWrite(conn, (byte_t const *)STR_LEN("\r\n"));
	return rc;
}
int HTTPConnectionWriteChunkFile(HTTPConnectionRef const conn, strarg_t const path) {
	bool worker = false;
	uv_file file = -1;
	byte_t *buf = NULL;
	int rc;

	async_pool_enter(NULL); worker = true;
	rc = async_fs_open(path, O_RDONLY, 0000);
	if(rc < 0) goto cleanup;
	file = rc;

	buf = malloc(BUFFER_SIZE);
	if(!buf) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	uv_buf_t const chunk = uv_buf_init((char *)buf, BUFFER_SIZE);
	ssize_t len = readall(file, &chunk);
	if(len < 0) rc = len;
	if(rc < 0) goto cleanup;

	// Fast path for small files.
	if(len < BUFFER_SIZE) {
		str_t pfx[16];
		int const pfxlen = snprintf(pfx, sizeof(pfx), "%llx\r\n", (unsigned long long)len);
		if(pfxlen < 0) rc = UV_UNKNOWN;
		if(rc < 0) goto cleanup;

		uv_buf_t parts[] = {
			uv_buf_init(pfx, pfxlen),
			uv_buf_init((char *)buf, len),
			uv_buf_init(STR_LEN("\r\n")),
		};
		async_fs_close(file); file = -1;
		async_pool_leave(NULL); worker = false;
		rc = HTTPConnectionWritev(conn, parts, numberof(parts));
		goto cleanup;
	}

	uv_fs_t req[1];
	rc = async_fs_fstat(file, req);
	if(rc < 0) goto cleanup;
	if(0 == req->statbuf.st_size) goto cleanup;

	async_pool_leave(NULL); worker = false;

	// TODO: HACK, WriteFile continues from where we left off
	rc = rc < 0 ? rc : HTTPConnectionWriteChunkLength(conn, req->statbuf.st_size);
	rc = rc < 0 ? rc : HTTPConnectionWritev(conn, &chunk, 1);
	rc = rc < 0 ? rc : HTTPConnectionWriteFile(conn, file);
	rc = rc < 0 ? rc : HTTPConnectionWrite(conn, (byte_t const *)STR_LEN("\r\n"));

cleanup:
	FREE(&buf);
	if(file >= 0) { async_fs_close(file); file = -1; }
	if(worker) { async_pool_leave(NULL); worker = false; }
	assert(file < 0);
	assert(!worker);
	return rc;
}
int HTTPConnectionWriteChunkEnd(HTTPConnectionRef const conn) {
	if(!conn) return 0;
	return HTTPConnectionWrite(conn, (byte_t const *)STR_LEN("0\r\n\r\n"));
}
int HTTPConnectionEnd(HTTPConnectionRef const conn) {
	// We assume keep-alive is enabled.
	return 0;
}

int HTTPConnectionSendMessage(HTTPConnectionRef const conn, uint16_t const status, strarg_t const str) {
	size_t const len = strlen(str);
	int rc = 0;
	rc = rc < 0 ? rc : HTTPConnectionWriteResponse(conn, status, str);
	rc = rc < 0 ? rc : HTTPConnectionWriteHeader(conn, "Content-Type", "text/plain; charset=utf-8");
	rc = rc < 0 ? rc : HTTPConnectionWriteContentLength(conn, len+1);
	rc = rc < 0 ? rc : HTTPConnectionBeginBody(conn);
	// TODO: Check how HEAD responses should look.
	if(HTTP_HEAD != conn->parser->method) { // TODO: Expose method? What?
		rc = rc < 0 ? rc : HTTPConnectionWrite(conn, (byte_t const *)str, len);
		rc = rc < 0 ? rc : HTTPConnectionWrite(conn, (byte_t const *)STR_LEN("\n"));
	}
	rc = rc < 0 ? rc : HTTPConnectionEnd(conn);
//	if(status >= 400) fprintf(stderr, "%s: %d %s\n", HTTPConnectionGetRequestURI(conn), (int)status, str);
	return rc;
}
int HTTPConnectionSendStatus(HTTPConnectionRef const conn, uint16_t const status) {
	strarg_t const str = statusstr(status);
	return HTTPConnectionSendMessage(conn, status, str);
}
int HTTPConnectionSendFile(HTTPConnectionRef const conn, strarg_t const path, strarg_t const type, int64_t size) {
	uv_file const file = async_fs_open(path, O_RDONLY, 0000);
	if(UV_ENOENT == file) return HTTPConnectionSendStatus(conn, 404);
	if(file < 0) return HTTPConnectionSendStatus(conn, 400); // TODO: Error conversion.
	int rc = 0;
	if(size < 0) {
		uv_fs_t req[1];
		rc = async_fs_fstat(file, req);
		if(rc < 0) return HTTPConnectionSendStatus(conn, 400);
		size = req->statbuf.st_size;
	}
	rc = rc < 0 ? rc : HTTPConnectionWriteResponse(conn, 200, "OK");
	rc = rc < 0 ? rc : HTTPConnectionWriteContentLength(conn, size);
	// TODO: Caching and other headers.
	if(type) rc = rc < 0 ? rc : HTTPConnectionWriteHeader(conn, "Content-Type", type);
	rc = rc < 0 ? rc : HTTPConnectionBeginBody(conn);
	rc = rc < 0 ? rc : HTTPConnectionWriteFile(conn, file);
	rc = rc < 0 ? rc : HTTPConnectionEnd(conn);
	async_fs_close(file);
	return rc;
}


static int on_message_begin(http_parser *const parser) {
	HTTPConnectionRef const conn = parser->data;
	assert(!(HTTPMessageIncomplete & conn->flags));
	conn->type = HTTPMessageBegin;
	*conn->out = uv_buf_init(NULL, 0);
	conn->flags |= HTTPMessageIncomplete;
	http_parser_pause(parser, 1);
	return 0;
}
static int on_url(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	conn->type = HTTPURL;
	*conn->out = uv_buf_init((char *)at, len);
	http_parser_pause(parser, 1);
	return 0;
}
static int on_header_field(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	conn->type = HTTPHeaderField;
	*conn->out = uv_buf_init((char *)at, len);
	http_parser_pause(parser, 1);
	return 0;
}
static int on_header_value(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	conn->type = HTTPHeaderValue;
	*conn->out = uv_buf_init((char *)at, len);
	http_parser_pause(parser, 1);
	return 0;
}
static int on_headers_complete(http_parser *const parser) {
	HTTPConnectionRef const conn = parser->data;
	conn->type = HTTPHeadersComplete;
	*conn->out = uv_buf_init(NULL, 0);
	http_parser_pause(parser, 1);
	return 0;
}
static int on_body(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	conn->type = HTTPBody;
	*conn->out = uv_buf_init((char *)at, len);
	http_parser_pause(parser, 1);
	return 0;
}
static int on_message_complete(http_parser *const parser) {
	HTTPConnectionRef const conn = parser->data;
	assert(HTTPMessageIncomplete & conn->flags);
	conn->type = HTTPMessageEnd;
	*conn->out = uv_buf_init(NULL, 0);
	conn->flags &= ~HTTPMessageIncomplete;
	http_parser_pause(parser, 1);
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

