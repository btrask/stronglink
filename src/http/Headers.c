#include "Headers.h"

#define FIELD_MAX 80

struct Headers {
	HeaderFieldList const *fields;
	str_t *field;
	index_t current;
	str_t **data;
};

HeadersRef HeadersCreate(HeaderFieldList const *const fields) {
	if(!fields) return NULL;
	HeadersRef const headers = calloc(1, sizeof(struct Headers));
	headers->fields = fields;
	headers->field = malloc(FIELD_MAX+1);
	headers->field[0] = '\0';
	headers->current = fields->count;
	headers->data = calloc(fields->count, sizeof(str_t *));
	return headers;
}
void HeadersFree(HeadersRef const headers) {
	if(!headers) return;
	FREE(&headers->field);
	for(index_t i = 0; i < headers->fields->count; ++i) {
		FREE(&headers->data[i]);
	}
	FREE(headers->data);
	free(headers);
}
err_t HeadersAppendFieldChunk(HeadersRef const headers, strarg_t const chunk, size_t const len) {
	if(!headers) return 0;
	append(headers->field, FIELD_MAX, chunk, len);
	return 0;
}
err_t HeadersAppendValueChunk(HeadersRef const headers, strarg_t const chunk, size_t const len) {
	if(!headers) return 0;
	HeaderFieldList const *const fields = headers->fields;
	if(headers->field[0]) {
		headers->current = fields->count; // Mark as invalid.
		for(index_t i = 0; i < fields->count; ++i) {
			if(0 != strcasecmp(headers->field, fields->items[i].name)) continue;
			if(headers->data[i]) continue; // Use separate slots for duplicate headers, if available.
			headers->current = i;
			headers->data[i] = malloc(fields->items[i].size+1);
			if(!headers->data[i]) return -1;
			headers->data[i][0] = '\0';
			break;
		}
		headers->field[0] = '\0';
	}
	if(headers->current < fields->count) {
		index_t const i = headers->current;
		append(headers->data[i], fields->items[i].size, chunk, len);
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
	headers->current = headers->fields->count;
	for(index_t i = 0; i < headers->fields->count; ++i) {
		FREE(&headers->data[i]);
	}
}

