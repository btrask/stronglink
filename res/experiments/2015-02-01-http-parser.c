


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
		assertf(0 == msg->next.len, "Chunk already waiting");
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
	assertf(0 == msg->next.len, "Chunk already waiting");
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

















static int read_raw(HTTPMessageRef const msg, uv_buf_t *const buf, async_read_t *const req);
static int read_http(HTTPMessageRef const msg, uv_buf_t *const buf, async_read_t *const req);

// but we need buffered access...

// "libtls should operate as a filter"...
// ...?


ssize_t HTTPMessageReadLine(msg, async_read_t *const req) {

	size_t pos = 0;
	for(;;) {
		if(!msg->remaining) {
			rc = rebuffer(msg, req);
			if(rc < 0) return rc;
		}
		if('\r' == msg->buf[0]) break;
		if('\n' == str[0]) break;
		if(pos < max-1) out[pos++] = str[0];
		msg->next.at++;
		msg->next.len--;
	}


}

// okay, this is why we're messed up
// we have two "buffers" and two positions and two remaining counts
// first is for the underlying stream
// second is for the parsed http data



struct HTTPMessage {

	uv_buf_t raw[1];
	uv_buf_t parsed[1];

};

// something like that, but its still a mess


// what if the connection fed out the raw data, and the message did the actual parsing?

// not so great because the parser belongs to the connection in the first place


// we've already probably introduced a bug
// because we moved the raw buffer from the connection to the message
// i dont know if its possible but there might be messages that span buffers
// in which case we're corrupting stuff


// on the other hand, 'pos' and 'remaining' were part of the message the whole time
// which would've been a bug under the same conditions



typedef enum {
	http_nothing = 0,
	http_message_begin,
	http_url,
	http_header_field,
	http_header_value,
	http_headers_complete,
	http_body,
	http_message_complete,
} http_type;


http_type http_parser_pull(http_parser *const parser, uv_buf_t *const src, uv_buf_t *const dst);








http_parser_pull(parser, src, dst);


switch() {
case http_message_begin:
case http_url:
case http_header_field:
case http_header_value:
case http_headers_complete:
case http_body:
case http_message_complete:

}







http_type http_parser_pull(http_parser *const parser, uv_buf_t *const src, uv_buf_t *const dst);

// i like this


typedef struct {
	http_type type;
	uv_buf_t *dst;
} http_info;
static int on_message_begin(http_parser *const parser) {
	http_info *const info = parser->data;
	info->type = http_message_begin;
	http_parser_pause(&msg->conn->parser, 1);
	return 0;
}
static int on_url(http_parser *const parser, char const *const at, size_t const len) {
	http_info *const info = parser->data;
	info->type = http_url;
	info->dst = uv_buf_init(at, len);
	http_parser_pause(&msg->conn->parser, 1);
	return 0;
}
static int on_header_field(http_parser *const parser, char const *const at, size_t const len) {
	http_info *const info = parser->data;
	info->type = http_header_field;
	info->dst = uv_buf_init(at, len);
	http_parser_pause(&msg->conn->parser, 1);
	return 0;
}
static int on_header_value(http_parser *const parser, char const *const at, size_t const len) {
	http_info *const info = parser->data;
	info->type = http_header_value;
	info->dst = uv_buf_init(at, len);
	http_parser_pause(&msg->conn->parser, 1);
	return 0;
}
static int on_headers_complete(http_parser *const parser) {
	http_info *const info = parser->data;
	info->type = http_headers_complete;
	http_parser_pause(&msg->conn->parser, 1);
	return 0;
}
static int on_body(http_parser *const parser, char const *const at, size_t const len) {
	http_info *const info = parser->data;
	info->type = http_body;
	info->dst = uv_buf_init(at, len);
	http_parser_pause(&msg->conn->parser, 1);
	return 0;
}
static int on_message_complete(http_parser *const parser) {
	http_info *const info = parser->data;
	info->type = http_message_complete;
	http_parser_pause(&msg->conn->parser, 1);
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
ssize_t http_parser_pull(http_parser *const parser, http_type *const type, uv_buf_t *const dst, uv_buf_t const *const src) { // TODO: Update code to match signature
	http_info info[1];
	info->dst = dst;
	parser->data = info;
	http_parser_pause(parser, 0);
	size_t const len = http_parser_parse(parser, &settings, src->base, src->len);
	if(HTTP_PARSER_ERRNO(parser)) return http_error;
	((char *)src->base) += len;
	src->len -= len;
	return info->type;
}

int HTTPConnectionPeek(HTTPConnectionRef const conn, uv_buf_t *const buf, async_read_t *const req);
void HTTPConnectionPop(HTTPConnectionRef const conn, size_t const len);

// above: obsolete...












typedef enum {
	HTTPNothing = 0,
	HTTPMessageBegin,
	HTTPURL,
	HTTPHeaderField,
	HTTPHeaderValue,
	HTTPHeadersComplete,
	HTTPBody,
	HTTPMessageEnd,
} HTTPEvent; // ...HTTP_event?


enum {
	HTTPConnectionMessageIncomplete = 1 << 0,
};
// TODO: Our http_parser callbacks should:
// - Set HTTPConnectionMessageIncomplete after message-being, and clear it on message-end
// - Set or clear type and out as appropriate


struct HTTPConnection {
	uv_tcp_t stream[1];
	http_parser parser[1];

	void *buf;
	uv_buf_t raw[1];

	http_type type;
	uv_buf_t out[1];
	unsigned flags;
};
void HTTPConnectionFree(HTTPConnectionRef *const connptr) {
	HTTPConnectionRef conn = *connptr;
	if(!conn) return;

	async_close((uv_handle_t *)conn->stream);
	memset(conn->stream, 0, sizeof(*conn->stream));
	memset(conn->parser, 0, sizeof(*conn->parser));

	FREE(&conn->buf);
	conn->raw = uv_buf_init(NULL, 0);

	conn->type = http_nothing;
	conn->out = uv_buf_init(NULL, 0);

	assert_zeroed(conn, 1);
	FREE(connptr); conn = NULL;
}
int HTTPConnectionPeek(HTTPConnectionRef const conn, http_type *const type, uv_buf_t *const buf, async_req_t *const inreq) {
	if(!conn) return UV_EINVAL;
	if(!type) return UV_EINVAL;
	if(!buf) return UV_EINVAL;
	int rc;
	size_t len;
	for(;;) {
		if(http_nothing != conn->type) break;
		if(!conn->buf) {
			async_req_t alt[1];
			async_req_t *const req = inreq ? inreq : alt;
			rc = async_read(req, conn->stream);
			if(rc < 0) return rc;
			conn->buf = req->buf->base;
			*conn->raw = *req->buf;
			req->buf->base = NULL;
			async_req_cleanup(req);
		}
		http_parser_pause(conn->parser, 0);
		len = http_parser_execute(conn->parser, &settings, conn->raw->base, conn->raw->len);
		(char *)conn->raw->base += len;
		conn->raw->len -= len;
		if(!conn->raw->len) {
			FREE(&conn->buf);
			conn->raw->base = NULL;
		}
		rc = HTTP_PARSER_ERRNO(conn->parser);
		if(HPE_OK != rc && HPE_PAUSED != rc) {
			fprintf(stderr, "HTTP parse error %s (%d)\n",
				http_errno_name(rc),
				HTTP_PARSER_ERRNO_LINE(conn->parser));
			return -1;
		}
	}
	*type = conn->type;
	*buf = *conn->out;
	return 0;
}
void HTTPConnectionPop(HTTPConnectionRef const conn, size_t const len) {
	if(!conn) return;
	assert(len <= conn->out->len);
	(char *)conn->out->base += len;
	conn->out->len -= len;
	if(!conn->out->len) {
		conn->type = http_nothing;
		conn->out->base = NULL;
	}
}

int HTTPMessageReadRequestURI(HTTPMessageRef const msg, str_t *const out, size_t const max, async_read_t *const req) {
	if(!msg) return UV_EINVAL;
	if(!max) return UV_EINVAL;
	uv_buf_t buf[1];
	int rc;
	http_type type;
	out[0] = '\0';
	for(;;) {
		rc = HTTPConnectionPeek(msg->conn, &type, buf, req);
		if(rc < 0) return rc;
		if(http_header_field == type || http_headers_complete == type) break;
		HTTPConnectionPop(msg->conn, buf->len);
		if(http_message_begin == type) continue;
		if(http_url != type) return -1;
		append(out, max, buf->base, buf->len);
	}
	return 0;
}
int HTTPMessageReadResponseStatus(HTTPMessageRef const msg, async_read_t *const req) {
	if(!msg) return UV_EINVAL;
	uv_buf_t buf[1];
	int rc;
	http_type type;
	for(;;) {
		if(msg->conn->parser->status_code) break;
		rc = HTTPConnectionPeek(msg->conn, &type, buf, req);
		if(rc < 0) return rc;
		if(http_header_field == type || http_headers_complete == type) break;
		if(http_message_begin != type) return -1;
		HTTPConnectionPop(msg->conn, buf->len);
	}
	return msg->conn->parser->status_code;
}

int HTTPMessageReadHeaders(HTTPMessageRef const msg, str_t values[][VALUE_MAX], str_t const fields[][FIELD_MAX], size_t const nfields, async_read_t *const req) {
	if(!msg) return UV_EINVAL;
	uv_buf_t buf[1];
	int rc, n;
	http_type type;
	str_t field[FIELD_MAX];
	for(;;) {
		field[0] = '\0';
		for(;;) {
			rc = HTTPConnectionPeek(msg->conn, &type, buf, req);
			if(rc < 0) return rc;
			if(http_header_value == type) break;
			HTTPConnectionPop(msg->conn, buf->len);
			if(http_headers_complete == type) goto done;
			if(http_header_field != type) return -1;
			append(field, FIELD_MAX, buf->base, buf->len);
		}
		for(n = 0; n < nfields; ++n) {
			if(0 != strcasecmp(field, fields[n])) continue;
			if(values[n][0]) continue;
			break;
		}
		for(;;) {
			rc = HTTPConnectionPeek(msg->conn, &type, buf, req);
			if(rc < 0) return rc;
			if(http_header_field == type) break;
			HTTPConnectionPop(msg->conn, buf->len);
			if(http_headers_complete == type) goto done;
			if(http_header_value != type) return -1;
			if(n >= nfields) continue;
			append(values[n], VALUE_MAX, buf->base, buf->len);
		}
	}
done:
	return 0;
}
int HTTPMessageReadBody(HTTPMessageRef const msg, uv_buf_t *const buf, async_read_t *const req) {
	if(!msg) return UV_EINVAL;
	http_type type;
	int rc = HTTPConnectionPeek(msg->conn, &type, buf, req);
	if(rc < 0) return rc;
	if(http_body != type && http_message_end != type) return -1;
	HTTPConnectionPop(msg->conn, buf->len);
	return 0;
}
int HTTPMessageReadBodyLine(HTTPMessageRef const msg, str_t *const out, size_t const max, async_read_t *const req) {
	if(!msg) return UV_EINVAL;
	if(!max) return UV_EINVAL;
	uv_buf_t buf[1];
	int rc;
	size_t i;
	out[0] = '\0';
	for(;;) {
		rc = HTTPConnectionPeek(msg->conn, &type, buf, req);
		if(rc < 0) return rc;
		if(http_message_end == type) return UV_EOF;
		if(http_body != type) return -1;
		for(i = 0; i < buf->len; ++i) {
			if('\r' == (char *)buf->base[i]) break;
			if('\n' == (char *)buf->base[i]) break;
		}
		append(out, max, buf->base, i);
		HTTPConnectionPop(msg->conn, i);
		if(i < buf->len) break;
	}

	rc = HTTPConnectionPeek(msg->conn, &type, buf, req);
	if(rc < 0) return rc;
	if(http_message_end == type) return 0;
	if(http_body != type) return -1;
	if('\r' == (char *)buf->base[0]) HTTPConnectionPop(msg->conn, 1);

	rc = HTTPConnectionPeek(msg->conn, &type, buf, req);
	if(rc < 0) return rc;
	if(http_message_end == type) return 0;
	if(http_body != type) return -1;
	if('\n' == (char *)buf->base[0]) HTTPConnectionPop(msg->conn, 1);

	return 0;
}


// todo: what's going on with message-end vs eof?














































