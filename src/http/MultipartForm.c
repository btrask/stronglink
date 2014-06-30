#define _GNU_SOURCE
#include "../../deps/multipart-parser-c/multipart_parser.h"
#include "MultipartForm.h"

#define FIELD_MAX 80

struct FormPart {
	MultipartFormRef form;
	HeadersRef headers;
	byte_t const *chunk;
	size_t chunkLength;
	bool_t eof;
};
struct MultipartForm {
	HTTPConnectionRef conn;
	multipart_parser *parser;
	byte_t const *buf;
	size_t len;
	bool_t eof;
	struct FormPart part;
};

static multipart_parser_settings const callbacks;

static err_t readOnce(FormPartRef const part);

MultipartFormRef MultipartFormCreate(HTTPConnectionRef const conn, strarg_t const type, HeaderFieldList const *const fields) {
	if(!conn) return NULL;
	if(!type) return NULL;
	// TODO: More robust content-type parsing.
	off_t boff = prefix("multipart/form-data; boundary=", type);
	if(!boff) return NULL;
	str_t *boundary;
	if(asprintf(&boundary, "--%s", type+boff) < 0) return NULL; // Why is the parser making us do this?
	MultipartFormRef const form = calloc(1, sizeof(struct MultipartForm));
	form->conn = conn;
	form->parser = multipart_parser_init(boundary, &callbacks);
	FormPartRef const part = &form->part;
	part->form = form;
	part->headers = HeadersCreate(fields);
	part->eof = 1;
	multipart_parser_set_data(form->parser, part);
	FREE(&boundary);
	return form;
}
void MultipartFormFree(MultipartFormRef const form) {
	if(!form) return;
	FormPartRef const part = &form->part;
	HeadersFree(part->headers); part->headers = NULL;
	multipart_parser_free(form->parser);
	free(form);
}
FormPartRef MultipartFormGetPart(MultipartFormRef const form) {
	if(!form) return NULL;
	if(form->eof) return NULL;
	FormPartRef const part = &form->part;
	if(!part->eof) {
		do {
			part->chunk = NULL;
			part->chunkLength = 0;
		} while(readOnce(part) >= 0);
	}
	HeadersClear(part->headers);
	part->eof = 0;
	for(;;) {
		if(-1 == readOnce(part)) return NULL;
		if(part->chunkLength || part->eof) break;
	}
	return part;
}
void *FormPartGetHeaders(FormPartRef const part) {
	if(!part) return NULL;
	return HeadersGetData(part->headers);
}
ssize_t FormPartGetBuffer(FormPartRef const part, byte_t const **const buf) {
	if(!part) return -1;
	if(!part->chunkLength) {
		if(part->eof) return 0;
		if(-1 == readOnce(part)) return -1;
	}
	size_t const used = part->chunkLength;
	*buf = part->chunk;
	part->chunk = NULL;
	part->chunkLength = 0;
	return used;
}

static err_t readOnce(FormPartRef const part) {
	MultipartFormRef const form = part->form;
	assertf(!part->chunkLength, "Reading when we already have a chunk");
	if(form->eof) return -1;
	if(part->eof) return -1;
	if(!form->len) {
		ssize_t const rlen = HTTPConnectionGetBuffer(form->conn, &form->buf);
		if(!rlen) {
			form->eof = 1;
			part->eof = 1; // Should already be set...
		}
		if(rlen <= 0) return -1;
		form->len = rlen;
	}
	ssize_t plen = multipart_parser_execute(form->parser, (char const *)form->buf, form->len);
	if(plen < 0) {
		fprintf(stderr, "Multipart parse error\n");
		return -1;
	}
	form->buf += plen;
	form->len -= plen;
	return 0;
}

static int on_header_field(multipart_parser *const parser, strarg_t const at, size_t const len) {
	FormPartRef const part = multipart_parser_get_data(parser);
	HeadersAppendFieldChunk(part->headers, at, len);
	return 0;
}
static int on_header_value(multipart_parser *const parser, strarg_t const at, size_t const len) {
	FormPartRef const part = multipart_parser_get_data(parser);
	HeadersAppendValueChunk(part->headers, at, len);
	return 0;
}
static int on_part_data(multipart_parser *const parser, char const *const at, size_t const len) {
	if(!len) return 0;
	FormPartRef const part = multipart_parser_get_data(parser);
	assertf(!part->chunkLength, "Form part already has chunk");
	assertf(!part->eof, "Form part already ended");
	part->chunk = (byte_t const *)at;
	part->chunkLength = len;
	return -1; // Always stop after one chunk.
}
static int on_part_data_end(multipart_parser *const parser) {
	FormPartRef const part = multipart_parser_get_data(parser);
	assertf(!part->eof, "Form part duplicate end of file");
	part->eof = 1;
	return -1; // Always stop after the end of a part.
}
static multipart_parser_settings const callbacks = {
	.on_header_field = on_header_field,
	.on_header_value = on_header_value,
	.on_part_data = on_part_data,
	.on_part_data_end = on_part_data_end,
};

