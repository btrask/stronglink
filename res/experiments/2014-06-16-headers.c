
typedef struct {
	strarg_t const name;
	size_t const size;
} HeaderField;
typedef struct {
	count_t const count;
	HeaderField const *const items;
} HeaderFieldList;

typedef struct Headers* HeadersRef;

#define FIELD_MAX 80

struct Headers {
	HeaderFieldList const *fields;
	str_t *field;
	index_t current;
	str_t **data;
};

static size_t append(str_t *const dst, size_t const dsize, strarg_t const src, size_t const slen);

HeadersRef HeadersCreate(HeaderFieldList const *const fields) {
	if(!fields) return NULL;
	HeadersRef const headers = calloc(1, sizeof(struct Headers));
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
	headers->field[0] = '\0';
}

static size_t append(str_t *const dst, size_t const dsize, strarg_t const src, size_t const slen) {
	size_t const olen = strlen(dst);
	size_t const nlen = MIN(olen + slen, dsize);
	memcpy(dst + olen, src, nlen - olen);
	dst[nlen] = '\0';
	return nlen - olen;
}

