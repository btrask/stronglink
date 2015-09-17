// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include "HTTP.h"

// HTTPHeaders currently uses linear search of the header fields during
// lookup. The theory is that the total field size is quite small (currently
// 256 bytes) and headers are not accessed too often per request, so this
// might actually be faster than a hash table. That said, no micro-
// benchmarks have been done. (If you want to compare, please include the
// cost of creating the hash table.)

// The miserly constraints below are intended to resist
// abuse (i.e. denial of service) as much as possible.

// Chrome and Firefox both appear to send 9 headers
// by default on my test config.
#define HEADERS_MAX 20
// Chrome sends 99 characters of field names in my test.
// Firefox sends 79.
#define FIELDS_SIZE 256
// The longest common field names are "Accept-Encoding"
// and "Accept-Language" at 16 characters each (w/ nul).
#define FIELD_MAX (23+1)
// VALUE_MAX should be modestly longer than URI_MAX
// in order to handle the Referer.
#define VALUE_MAX (1023+256+1)
// TOTAL_MAX should be less than VALUE_MAX*HEADERS_MAX
// in order to constrain the average value size.
#define TOTAL_MAX (1024*10)

struct HTTPHeaders {
	str_t *fields;
	str_t *values[HEADERS_MAX];
	uint16_t count;
	uint16_t offset;
	size_t total;
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
	h->total = 0;
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
		ssize_t const vlen = HTTPConnectionReadHeaderValue(conn, value, sizeof(value));
		if(UV_EMSGSIZE == flen) continue;
		if(flen < 0) return flen;
		if(vlen < 0) return vlen;

		if(h->count >= HEADERS_MAX) return UV_EMSGSIZE;
		if(h->offset+flen+1 > FIELDS_SIZE) return UV_EMSGSIZE;
		if(h->total+vlen > TOTAL_MAX) return UV_EMSGSIZE;
		if(!flen) continue;

		// We could use strlcpy() here, but it doesn't buy us much...
		memcpy(h->fields + h->offset, field, flen+1);
		h->offset += flen+1;
		h->values[h->count] = strndup(value, vlen);
		h->count++;
		h->total += vlen+1;
	}
	return 0;
}
strarg_t HTTPHeadersGet(HTTPHeadersRef const h, strarg_t const field) {
	if(!h) return NULL;
	if(!field) return NULL;
	assert(strlen(field)+1 <= FIELD_MAX);
	uint16_t pos = 0;
	for(uint16_t i = 0; i < h->count; i++) {
		// If it mattered, more performance could probably be won by
		// normalizing case ahead of time and using memcmp. But I
		// seriously doubt we care.
		if(0 == strcasecmp(h->fields+pos, field)) {
			return h->values[i];
		}
		pos += strlen(h->fields+pos)+1;
	}
	return NULL;
}

