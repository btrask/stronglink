/* FOR REFERENCE:

Content-Type: multipart/form-data; boundary=---------------------------421187570164449984965009262
Content-Length: 302

-----------------------------421187570164449984965009262
Content-Disposition: form-data; name="token"

asdf
-----------------------------421187570164449984965009262
Content-Disposition: form-data; name="markdown"

asdfadsfdsfasdfasdf
-----------------------------421187570164449984965009262--
*/



/*

  multipart_notify_cb on_part_data_begin;
  multipart_data_cb on_header_field;
  multipart_data_cb on_header_value;
  multipart_notify_cb on_headers_complete;
  multipart_data_cb on_part_data;
  multipart_notify_cb on_part_data_end;
  multipart_notify_cb on_body_end;
*/


typedef enum {
	MultipartNothing = 0,
	MultipartPartBegin,
	MultipartHeaderField,
	MultipartHeaderValue,
	MultipartHeadersComplete,
	MultipartPartData,
	MultipartPartEnd,
	MultipartFormEnd,
} MultipartEvent;


// TODO: BAD
static size_t append(str_t *const dst, size_t const dsize, strarg_t const src, size_t const slen) {
	if(!dsize) return 0;
	size_t const olen = strlen(dst);
	size_t const nlen = MIN(olen + slen, dsize-1);
	memcpy(dst + olen, src, nlen - olen);
	dst[nlen] = '\0';
	return nlen - olen;
}


struct MultipartForm {
	HTTPConnectionRef conn;
	multipart_parser *parser;

	MultipartEvent type;
	uv_buf_t out[1];

	unsigned flags;
};

int MultipartBoundaryFromType(strarg_t const type, uv_buf_t *const out) {
	assert(out);
	if(!type) return -1;
	size_t len = prefix("multipart/form-data; boundary=", type); // TODO
	if(!len) return -1;
	out->base = type+len;
	out->len = strlen(out->base);
	if(!out->len) return -1;
	return 0;
}
int MultipartFormCreate(HTTPConnectionRef const conn, uv_buf_t const *const boundary, MultipartFormRef *const out) {
	assert(out);
	if(!conn || !boundary || !len) return UV_EINVAL;
	MultipartFormRef form = calloc(1, sizeof(struct MultipartForm));
	if(!form) return UV_ENOMEM;
	form->conn = conn;
	form->parser = multipart_parser_init(boundary->base, boundary->len, &settings);
	if(!form->parser) {
		MultipartFormFree(&form);
		return UV_ENOMEM;
	}
	multipart_parser_set_data(form->parser, form);
	form->type = MultipartNothing;
	form->out = uv_buf_init(NULL, 0);
	form->flags = 0;
	return 0;
}
void MultipartFormFree(MultipartFormRef *const formptr) {
	MultipartFormRef form = *formptr;
	if(!form) return;
	form->conn = NULL;
	multipart_parser_free(form->parser); form->parser = NULL;
	form->type = MultipartNothing;
	form->out = uv_buf_init(NULL, 0);
	form->flags = 0;
	FREE(formptr); form = NULL;
}

static multipart_parser_settings const callbacks;
int MultipartFormPeek(MultipartFormRef const form, MultipartEvent *const type, uv_buf_t *const buf) {
	if(!form) return UV_EINVAL;
	if(!type) return UV_EINVAL;
	if(!buf) return UV_EINVAL;
	uv_buf_t raw[1];
	HTTPEvent t;
	int rc;
	ssize_t len;
	for(;;) {
		if(MultipartNothing != form->type) break;
		rc = HTTPConnectionPeek(form->conn, &t, raw);
		if(rc < 0) return rc;
		if(HTTPMessageEnd == t) {
			raw = uv_buf_init(NULL, 0);
			t = HTTPBody;
		}
		if(HTTPBody != t) {
			assertf(0, "Multipart unexpected HTTP event: %d\n", t);
			return UV_UNKNOWN;
		}
		len = multipart_parser_execute(form->parser, raw->base, raw->len);
		if(len < 0) {
			fprintf(stderr, "Multipart parse error\n");
			return -1;
		}
		HTTPConnectionPop(form->conn, len);
	}
	assertf(MultipartNothing != form->type, "MultipartFormPeek must return an event");
	*type = form->type;
	*buf = *form->out;
	return 0;
}
void MultipartFormPop(MultipartFormRef const form, size_t const len) {
	if(!form) return;
	assert(len <= form->out->len);
	form->out->base += len;
	form->out->len -= len;
	if(form->out->len) return;
	form->type = MultipartNothing;
	form->out->base = NULL;
}

int MultipartFormReadHeaderField(MultipartFormRef const form, str_t field[], size_t const max) {
	if(!form) return UV_EINVAL;
	uv_buf_t buf[1];
	int rc;
	MultipartEvent type;
	if(max > 0) field[0] = '\0';
	for(;;) {
		rc = MultipartFormPeek(form, &type, buf);
		if(rc < 0) return rc;
		if(MultipartHeaderValue == type) break;
		if(MultipartHeadersComplete == type) break;
		MultipartFormPop(conn, buf->len);
		if(MultipartHeaderField != type) {
			assertf(0, "Unexpected multipart event %d", type);
			return UV_UNKNOWN;
		}
		append(field, max, buf->base, buf->len);
	}
	return 0;
}
int MultipartFormReadHeaderValue(MultipartFormRef const conn, str_t value[], size_t const max) {
	if(!conn) return UV_EINVAL;
	uv_buf_t buf[1];
	int rc;
	MultipartEvent type;
	if(max > 0) value[0] = '\0';
	for(;;) {
		rc = MultipartFormPeek(conn, &type, buf);
		if(rc < 0) return rc;
		if(MultipartHeaderField == type) break;
		if(MultipartHeadersComplete == type) break;
		MultipartFormPop(conn, buf->len);
		if(MultipartHeaderValue != type) {
			assertf(0, "Unexpected multipart event %d", type);
			return UV_UNKNOWN;
		}
		append(value, max, buf->base, buf->len);
	}
	return 0;
}
int MultipartFormReadBody(MultipartFormRef const conn, uv_buf_t *const buf) {
	if(!conn) return UV_EINVAL;
	MultipartEvent type;
	int rc = MultipartFormPeek(conn, &type, buf);
	if(rc < 0) return rc;
	if(MultipartBody != type && MultipartPartEnd != type) {
		assertf(0, "Unexpected multipart event %d", type);
		return UV_UNKNOWN;
	}
	MultipartFormPop(conn, buf->len);
	return 0;
}


static int on_part_begin(multipart_parser *const parser) {
	MultipartFormRef const form = multipart_parser_get_data(parser);
	form->type = MultipartPartBegin;
	form->out = uv_buf_init(NULL, 0);
	return -1;
}
static int on_header_field(multipart_parser *const parser, strarg_t const at, size_t const len) {
	MultipartFormRef const form = multipart_parser_get_data(parser);
	form->type = MultipartHeaderField;
	form->out = uv_buf_init(at, len);
	return -1;
}
static int on_header_value(multipart_parser *const parser, strarg_t const at, size_t const len) {
	MultipartFormRef const form = multipart_parser_get_data(parser);
	form->type = MultipartHeaderValue;
	form->out = uv_buf_init(NULL, 0);
	return -1;
}
static int on_part_headers_complete(multipart_parser *const parser) {
	MultipartFormRef const form = multipart_parser_get_data(parser);
	form->type = MultipartHeadersComplete;
	form->out = uv_buf_init(NULL, 0);
	return -1;
}
static int on_part_data(multipart_parser *const parser, char const *const at, size_t const len) {
	MultipartFormRef const form = multipart_parser_get_data(parser);
	form->type = MultipartPartData;
	form->out = uv_buf_init(NULL, 0);
	return -1;
}
static int on_part_end(multipart_parser *const parser) {
	MultipartFormRef const form = multipart_parser_get_data(parser);
	form->type = MultipartPartEnd;
	form->out = uv_buf_init(NULL, 0);
	return -1;
}
static int on_form_end(multipart_parser *const parser) {
	MultipartFormRef const form = multipart_parser_get_data(parser);
	form->type = MultipartFormEnd;
	form->out = uv_buf_init(NULL, 0);
	return -1;
}
static multipart_parser_settings const callbacks = {
	.on_part_data_begin = on_part_begin,
	.on_header_field = on_header_field,
	.on_header_value = on_header_value,
	.on_headers_complete = on_headers_complete,
	.on_part_data = on_part_data,
	.on_part_data_end = on_part_end,
	.on_body_end = on_form_end,
};


