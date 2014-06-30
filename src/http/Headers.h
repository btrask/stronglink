#ifndef HEADERS_H
#define HEADERS_H

#include "../common.h"

typedef struct {
	strarg_t const name;
	size_t const size;
} HeaderField;
typedef struct {
	count_t const count;
	HeaderField const *const items;
} HeaderFieldList;

typedef struct Headers* HeadersRef;

HeadersRef HeadersCreate(HeaderFieldList const *const fields);
void HeadersFree(HeadersRef const headers);
err_t HeadersAppendFieldChunk(HeadersRef const headers, strarg_t const chunk, size_t const len);
err_t HeadersAppendValueChunk(HeadersRef const headers, strarg_t const chunk, size_t const len);
void HeadersEnd(HeadersRef const headers);
void *HeadersGetData(HeadersRef const headers);
void HeadersClear(HeadersRef const headers);

static size_t append(str_t *const dst, size_t const dsize, strarg_t const src, size_t const slen) {
	size_t const olen = strlen(dst);
	size_t const nlen = MIN(olen + slen, dsize);
	memcpy(dst + olen, src, nlen - olen);
	dst[nlen] = '\0';
	return nlen - olen;
}

#endif
