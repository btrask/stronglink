#include "Headers.h"

struct Headers {
	str_t *field;
	strarg_t const *fields;

	count_t count;
	index_t current;
	str_t **data;
};

HeadersRef HeadersCreate(strarg_t const fields[], count_t const count) {
	HeadersRef headers = calloc(1, sizeof(struct Headers));
	if(!headers) return NULL;
	headers->fields = fields;
	headers->field = malloc(HEADER_FIELD_MAX+1);
	if(!headers->field) {
		HeadersFree(&headers);
		return NULL;
	}
	headers->field[0] = '\0';
	headers->current = count;
	headers->count = count;
	headers->data = calloc(count, sizeof(str_t *));
	if(!headers->data) {
		HeadersFree(&headers);
		return NULL;
	}
	return headers;
}
void HeadersFree(HeadersRef *const headersptr) {
	HeadersRef headers = *headersptr;
	if(!headers) return;

	FREE(&headers->field);
	headers->fields = NULL;

	for(index_t i = 0; i < headers->count; ++i) {
		FREE(&headers->data[i]);
	}
	assert_zeroed(headers->data, headers->count);
	FREE(&headers->data);
	headers->count = 0;
	headers->current = 0;

	assert_zeroed(headers, 1);
	FREE(headersptr); headers = NULL;
}
int HeadersAppendFieldChunk(HeadersRef const headers, strarg_t const chunk, size_t const len) {
	if(!headers) return 0;
	append(headers->field, HEADER_FIELD_MAX, chunk, len);
	return 0;
}
int HeadersAppendValueChunk(HeadersRef const headers, strarg_t const chunk, size_t const len) {
	if(!headers) return 0;
	strarg_t const *const fields = headers->fields;
	if(headers->field[0]) {
		headers->current = headers->count; // Mark as invalid.
		for(index_t i = 0; i < headers->count; ++i) {
			if(0 != strcasecmp(headers->field, fields[i])) continue;
			if(headers->data[i]) continue; // Use separate slots for duplicate headers, if available.
			headers->current = i;
			headers->data[i] = malloc(HEADER_VALUE_MAX+1);
			if(!headers->data[i]) return -1;
			headers->data[i][0] = '\0';
			break;
		}
		headers->field[0] = '\0';
	}
	if(headers->current < headers->count) {
		index_t const i = headers->current;
		append(headers->data[i], HEADER_VALUE_MAX, chunk, len);
	}
	return 0;
}
void HeadersEnd(HeadersRef const headers) {
	if(!headers) return;
	// No-op.
}
void *HeadersGetData(HeadersRef const headers) {
	if(!headers) return NULL;
	return headers->data;
}
void HeadersClear(HeadersRef const headers) {
	if(!headers) return;
	headers->field[0] = '\0';
	headers->current = headers->count;
	for(index_t i = 0; i < headers->count; ++i) {
		FREE(&headers->data[i]);
	}
}

