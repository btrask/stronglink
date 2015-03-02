#include "HTTPHeaders.h"

#define HEADERS_MAX 20
#define FIELDS_SIZE 128
#define FIELD_MAX 24
#define VALUE_MAX 1024
#define VALUES_MIN (1024 * 1)
#define VALUES_MAX (1024 * 16)

struct HTTPHeaders {
	size_t offset;
	size_t count;
	str_t *fields;
	uint16_t *offsets;
	str_t *values;
	size_t values_size;
};

HTTPHeadersRef HTTPHeadersCreate(void) {
	HTTPHeadersRef h = calloc(1, sizeof(struct HTTPHeaders));
	if(!h) return NULL;
	h->offset = 0;
	h->count = 0;
	h->fields = calloc(FIELDS_SIZE, 1);
	h->offsets = calloc(HEADERS_MAX, sizeof(*h->offsets));
	h->values = NULL;
	h->values_size = 0;
	if(!h->fields || !h->offsets) {
		HTTPHeadersFree(&h);
		return NULL;
	}
	return h;
}
HTTPHeadersRef HTTPHeadersCreateFromConnection(HTTPConnectionRef const conn) {
	assert(conn);
	HTTPHeadersRef h = HTTPHeadersCreate();
	if(!h) return NULL;
	int rc = HTTPHeadersLoad(h, conn);
	if(rc < 0) {
		HTTPHeadersFree(&h);
		return NULL;
	}
	return h;
}
void HTTPHeadersFree(HTTPHeadersRef *const hptr) {
	HTTPHeadersRef h = *hptr;
	if(!h) return;
	h->offset = 0;
	h->count = 0;
	FREE(&h->fields);
	FREE(&h->offsets);
	FREE(&h->values);
	h->values_size = 0;
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
		field[0] = '\0';
		value[0] = '\0';
		rc = HTTPConnectionReadHeaderField(conn, field, FIELD_MAX);
		if(rc < 0) return rc;
		rc = HTTPConnectionReadHeaderValue(conn, value, VALUE_MAX);
		if(rc < 0) return rc;

		if(h->count >= HEADERS_MAX) continue;

		size_t const flen = strlen(field);
		size_t const vlen = strlen(value);
		if(!flen || !vlen) continue;
		if(flen >= FIELD_MAX) continue;
		if(vlen >= VALUE_MAX) continue;
		if(h->offset+flen+1 >= FIELDS_SIZE) continue;
		if(h->offsets[h->count]+vlen+1 >= VALUES_MAX) continue;
		if(h->offsets[h->count]+vlen+1 >= h->values_size) {
			h->values_size = MAX(VALUES_MIN, h->values_size * 2);
			h->values_size = MAX(h->values_size, h->offsets[h->count]+vlen+1);
			h->values = realloc(h->values, h->values_size);
			assert(h->values); // TODO
		}

		memcpy(h->fields + h->offset, field, flen+1);
		memcpy(h->values + h->offsets[h->count], value, vlen+1);
		h->offset += flen+1;
		h->offsets[h->count+1] = h->offsets[h->count]+vlen+1;
		h->count++;
	}
	return 0;
}
strarg_t HTTPHeadersGet(HTTPHeadersRef const h, strarg_t const field) {
	if(!h) return NULL;
	if(!field) return NULL;
	assert(strlen(field)+1 <= FIELD_MAX);
	size_t pos = 0;
	for(size_t i = 0; i < h->count; i++) {
		if(0 == strcasecmp(h->fields+pos, field)) {
			return h->values + h->offsets[i];
		}
		pos += strlen(h->fields+pos)+1;
	}
	return NULL;
}

