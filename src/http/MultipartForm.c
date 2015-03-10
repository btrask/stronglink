#include "../../deps/multipart-parser-c/multipart_parser.h"
#include "MultipartForm.h"

// TODO: BAD
static size_t append(str_t *const dst, size_t const dsize, strarg_t const src, size_t const slen) {
	if(!dsize) return 0;
	size_t const olen = strlen(dst);
	size_t const nlen = MIN(olen + slen, dsize-1);
	memcpy(dst + olen, src, nlen - olen);
	dst[nlen] = '\0';
	return nlen - olen;
}


static multipart_parser_settings const callbacks;

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
	out->base = (char *)type+len;
	out->len = strlen(out->base);
	if(!out->len) return -1;
	return 0;
}
int MultipartFormCreate(HTTPConnectionRef const conn, uv_buf_t const *const boundary, MultipartFormRef *const out) {
	assert(out);
	if(!conn || !boundary->base || !boundary->len) return UV_EINVAL;
	MultipartFormRef form = calloc(1, sizeof(struct MultipartForm));
	if(!form) return UV_ENOMEM;
	form->conn = conn;
	form->parser = multipart_parser_init(boundary->base, boundary->len, &callbacks);
	if(!form->parser) {
		MultipartFormFree(&form);
		return UV_ENOMEM;
	}
	multipart_parser_set_data(form->parser, form);
	form->type = MultipartNothing;
	*form->out = uv_buf_init(NULL, 0);
	form->flags = 0;
	*out = form;
	return 0;
}
void MultipartFormFree(MultipartFormRef *const formptr) {
	MultipartFormRef form = *formptr;
	if(!form) return;
	form->conn = NULL;
	multipart_parser_free(form->parser); form->parser = NULL;
	form->type = MultipartNothing;
	*form->out = uv_buf_init(NULL, 0);
	form->flags = 0;
	FREE(formptr); form = NULL;
}

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
			*raw = uv_buf_init(NULL, 0);
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
		MultipartFormPop(form, buf->len);
		if(MultipartHeaderField != type) {
			assertf(0, "Unexpected multipart event %d", type);
			return UV_UNKNOWN;
		}
		append(field, max, buf->base, buf->len);
	}
	return 0;
}
int MultipartFormReadHeaderValue(MultipartFormRef const form, str_t value[], size_t const max) {
	if(!form) return UV_EINVAL;
	uv_buf_t buf[1];
	int rc;
	MultipartEvent type;
	if(max > 0) value[0] = '\0';
	for(;;) {
		rc = MultipartFormPeek(form, &type, buf);
		if(rc < 0) return rc;
		if(MultipartHeaderField == type) break;
		if(MultipartHeadersComplete == type) break;
		MultipartFormPop(form, buf->len);
		if(MultipartHeaderValue != type) {
			assertf(0, "Unexpected multipart event %d", type);
			return UV_UNKNOWN;
		}
		append(value, max, buf->base, buf->len);
	}
	return 0;
}
int MultipartFormReadStaticHeaders(MultipartFormRef const form, uv_buf_t values[], strarg_t const fields[], size_t const count) {
	if(!form) return UV_EINVAL;
	uv_buf_t buf[1];
	int rc;
	MultipartEvent type;
	str_t field[MULTIPART_FIELD_MAX];
	uv_buf_t value[1];
	size_t i;
	for(i = 0; i < count; i++) if(values[i].len > 0) values[i].base[0] = '\0';
	for(;;) {
		rc = MultipartFormPeek(form, &type, buf);
		if(rc < 0) return rc;
		if(MultipartPartBegin == type) {
			MultipartFormPop(form, buf->len);
			continue;
		}
		if(MultipartHeadersComplete == type) {
			MultipartFormPop(form, buf->len);
			break;
		}
		if(MultipartHeaderField != type) {
			assertf(0, "Unexpected multipart event %d", type);
			return UV_UNKNOWN;
		}
		rc = MultipartFormReadHeaderField(form, field, MULTIPART_FIELD_MAX);
		if(rc < 0) return rc;

		*value = uv_buf_init(NULL, 0);
		for(i = 0; i < count; i++) {
			if(0 != strcasecmp(field, fields[i])) continue;
			*value = values[i];
			break;
		}

		rc = MultipartFormReadHeaderValue(form, value->base, value->len);
		if(rc < 0) return rc;
	}
	return 0;
}
int MultipartFormReadData(MultipartFormRef const form, uv_buf_t *const buf) {
	if(!form) return UV_EINVAL;
	MultipartEvent type;
	int rc = MultipartFormPeek(form, &type, buf);
	if(rc < 0) return rc;
	if(MultipartPartData != type && MultipartPartEnd != type) {
		assertf(0, "Unexpected multipart event %d", type);
		return UV_UNKNOWN;
	}
	MultipartFormPop(form, buf->len);
	return 0;
}


static int on_part_begin(multipart_parser *const parser) {
	MultipartFormRef const form = multipart_parser_get_data(parser);
	form->type = MultipartPartBegin;
	*form->out = uv_buf_init(NULL, 0);
	return -1;
}
static int on_header_field(multipart_parser *const parser, strarg_t const at, size_t const len) {
	MultipartFormRef const form = multipart_parser_get_data(parser);
	form->type = MultipartHeaderField;
	*form->out = uv_buf_init((char *)at, len);
	return -1;
}
static int on_header_value(multipart_parser *const parser, strarg_t const at, size_t const len) {
	MultipartFormRef const form = multipart_parser_get_data(parser);
	form->type = MultipartHeaderValue;
	*form->out = uv_buf_init((char *)at, len);
	return -1;
}
static int on_headers_complete(multipart_parser *const parser) {
	MultipartFormRef const form = multipart_parser_get_data(parser);
	form->type = MultipartHeadersComplete;
	*form->out = uv_buf_init(NULL, 0);
	return -1;
}
static int on_part_data(multipart_parser *const parser, char const *const at, size_t const len) {
	if(!len) return 0;
	MultipartFormRef const form = multipart_parser_get_data(parser);
	form->type = MultipartPartData;
	*form->out = uv_buf_init((char *)at, len);
	return -1;
}
static int on_part_end(multipart_parser *const parser) {
	MultipartFormRef const form = multipart_parser_get_data(parser);
	form->type = MultipartPartEnd;
	*form->out = uv_buf_init(NULL, 0);
	return -1;
}
static int on_form_end(multipart_parser *const parser) {
	MultipartFormRef const form = multipart_parser_get_data(parser);
	form->type = MultipartFormEnd;
	*form->out = uv_buf_init(NULL, 0);
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

