// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include "HTTPHeaders.h"
#include "../../deps/openbsd-compat/includes.h"

#define HEADERS_MAX 20
#define FIELDS_SIZE 256
#define FIELD_MAX (23+1)
#define VALUE_MAX (511+1)

struct HTTPHeaders {
	str_t *fields;
	str_t *values[HEADERS_MAX];
	uint16_t count;
	uint16_t offset;
};

int HTTPHeadersCreate(HTTPHeadersRef *const out) {
	HTTPHeadersRef h = calloc(1, sizeof(struct HTTPHeaders));
	if(!h) return UV_ENOMEM;
	h->fields = calloc(FIELDS_SIZE, 1);
	if(!h->fields) {
		HTTPHeadersFree(&h);
		return UV_ENOMEM;
	}
	h->count = 0;
	h->offset = 0;
	*out = h; h = NULL;
	return 0;
}
int HTTPHeadersCreateFromConnection(HTTPConnectionRef const conn, HTTPHeadersRef *const out) {
	assert(conn);
	int rc = HTTPHeadersCreate(out);
	if(rc < 0) return rc;
	rc = HTTPHeadersLoad(*out, conn);
	if(rc < 0) {
		HTTPHeadersFree(out);
		return rc;
	}
	return 0;
}
void HTTPHeadersFree(HTTPHeadersRef *const hptr) {
	HTTPHeadersRef h = *hptr;
	if(!h) return;
	FREE(&h->fields);
	for(uint16_t i = 0; i < h->count; i++) FREE(&h->values[i]);
	h->count = 0;
	h->offset = 0;
	assert_zeroed(h, 1);
	FREE(hptr); h = NULL;
}
int HTTPHeadersLoad(HTTPHeadersRef const h, HTTPConnectionRef const conn) {
	if(!h) return 0;
	if(!conn) return UV_EINVAL;
	uv_buf_t buf[1];
	HTTPEvent type;
	str_t field[FIELD_MAX];
	str_t value[VALUE_MAX];
	int rc;
	for(;;) {
		rc = HTTPConnectionPeek(conn, &type, buf);
		if(rc < 0) return rc;
		if(HTTPHeadersComplete == type) {
			HTTPConnectionPop(conn, buf->len);
			break;
		}
		ssize_t const flen = HTTPConnectionReadHeaderField(conn, field, sizeof(field));
		if(flen < 0) return flen;
		ssize_t const vlen = HTTPConnectionReadHeaderValue(conn, value, sizeof(value));
		if(vlen < 0) return vlen;

		if(h->count >= HEADERS_MAX) return UV_EMSGSIZE;
		if(h->offset+flen+1 > FIELDS_SIZE) return UV_EMSGSIZE;
		if(!flen) continue;

		// We could use strlcpy() here, but it doesn't buy us much...
		memcpy(h->fields + h->offset, field, flen+1);
		h->offset += flen+1;
		h->values[h->count] = strndup(value, vlen);
		h->count++;
	}
	return 0;
}
strarg_t HTTPHeadersGet(HTTPHeadersRef const h, strarg_t const field) {
	if(!h) return NULL;
	if(!field) return NULL;
	assert(strlen(field)+1 <= FIELD_MAX);
	uint16_t pos = 0;
	for(uint16_t i = 0; i < h->count; i++) {
		if(0 == strcasecmp(h->fields+pos, field)) {
			return h->values[i];
		}
		pos += strlen(h->fields+pos)+1;
	}
	return NULL;
}

