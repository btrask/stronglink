#include "HTTPServer.h"

typedef struct MultipartForm* MultipartFormRef;
typedef struct FormPart* FormPartRef;

#include "../deps/multipart-parser-c/multipart_parser.h"

#define FIELD_MAX 80

struct FormPart {
	MultipartFormRef form;
	str_t *field;
	index_t valueIndex;
	str_t **headers;
	byte_t const *chunk;
	size_t chunkLength;
	bool_t eof;
};
struct MultipartForm {
	HTTPConnectionRef conn;
	multipart_parser *parser;
	HeaderFieldList const *fields;
	byte_t const *buf;
	size_t len;
	struct FormPart part;
};

static multipart_parser_settings const callbacks;

MultipartFormRef MultipartFormCreate(HTTPConnectionRef const conn, strarg_t const type, HeaderFieldList const *const fields) {
	if(!conn) return NULL;
	if(!type) return NULL;
	off_t boff = prefix("multipart/form-data; boundary=", type);
	if(!boff) return NULL;
	strarg_t const boundary = type + boff;
	MultipartFormRef const form = calloc(1, sizeof(struct MultipartForm));
	form->conn = conn;
	form->parser = multipart_parser_init(bounary, &callbacks);
	form->fields = fields;
	FormPartRef const part = &form->part;
	part->form = form;
	part->field = malloc(FIELD_MAX);
	part->headers = calloc(fields->count, sizeof(str_t *));
	part->eof = 1;
	multipart_parser_set_data(parser, part);
	return form;
}
void MultipartFormFree(MultipartFormRef const form) {
	if(!form) return;
	FormPartRef const part = &form->part;
	FREE(&part->field);
	for(index_t i = 0; i < form->fields->count; ++i) {
		FREE(&part->headers[i]);
	}
	FREE(&part->headers);
	multipart_parser_free(form->parser);
	free(form);
}
FormPartRef MultipartFormGetPart(MultipartFormRef const form) {
	if(!form) return NULL;
	FormPartRef const part = &form->part;
	if(!part->eof) {
		do {
			part->chunk = NULL;
			part->chunkLength = 0;
		} while(readOnce(part) >= 0);
	}
	part->field[0] = '\0';
	for(index_t i = 0; i < form->fields->count; ++i) {
		FREE(&part->headers[i]);
	}
	part->eof = 0;
	for(;;) {
		if(-1 == readOnce(part)) return NULL;
		if(part->chunkLength || part->eof) break;
	}
	return part;
}
void *FormPartGetHeaders(FormPartRef const part) {
	if(!part) return NULL;
	return part->headers;
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
	if(part->eof) return -1;
	if(!form->len) {
		form->len = HTTPConnectionGetBuffer(form->conn, &form->buf);
		if(form->len <= 0) return -1;
	}
	size_t const plen = multipart_parser_execute(form->parser, (char const *)buf, len);
	// TODO: Detect parse errors.
	form->buf += plen;
	form->len -= plen;
	return 0;
}

// TODO: Copy-pasted from HTTPServer.c.
static size_t append(str_t *const dst, size_t const dsize, strarg_t const src, size_t const slen) {
	size_t const olen = strlen(dst);
	size_t const nlen = MIN(olen + slen, dsize);
	memcpy(dst + olen, src, nlen - olen);
	dst[nlen] = '\0';
	return nlen - olen;
}

static int on_header_field(multipart_parser *const parser, strarg_t const at, size_t const len) {
	FormPartRef const part = multipart_parser_get_data(parser);
	append(part->field, FIELD_MAX, at, len);
	return 0;
}
static int on_header_value(multipart_parser *const parser, strarg_t const at, size_t const len) {
	FormPartRef const part = multipart_parser_get_data(parser);
	// TODO: Copy-pasted from HTTPServer.c, with minor modifications.
	HeaderFieldList const *const fields = part->form->fields;
	if(part->field[0]) {
		part->valueIndex = fields->count; // Mark as invalid.
		for(index_t i = 0; i < fields->count; ++i) {
			if(0 != strcasecmp(part->field, fields->items[i].name)) continue;
			if(part->headers[i]) continue; // Use separate slots for duplicate headers, if available.
			part->valueIndex = i;
			part->headers[i] = malloc(fields->items[i].size+1);
			part->headers[i][0] = '\0';
			break;
		}
		part->field[0] = '\0';
	}
	if(part->valueIndex < fields->count) {
		index_t const i = part->valueIndex;
		append(part->headers[i], fields->items[i].size, at, len);
	}
	return 0;
}
static int on_part_data(multipart_parser *const parser, strarg_t const at, size_t const len) {
	FormPartRef const part = multipart_parser_get_data(parser);
	BTAssert(!part->chunkLength, "Form part already has chunk");
	BTAssert(!part->eof, "Form part already ended");
	part->chunk = at;
	part->chunkLength = len;
	return -1; // Always stop after one chunk.
}
static int on_part_data_end(multipart_parser *const parser) {
	FormPartRef const part = multipart_parser_get_data(parser);
	BTAssert(!part->eof, "Form part duplicate end of file");
	part->eof = 1;
	return -1; // Always stop after the end of a part.
}
static multipart_parser_settings const callbacks = {
	.on_header_field = on_header_field,
	.on_header_value = on_header_value,
	.on_part_data = on_part_data,
	.on_part_data_end = on_part_data_end,
};

