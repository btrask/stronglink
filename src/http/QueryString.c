#include "../strndup.h"
#include "QueryString.h"

void *QSValuesCopy(strarg_t const qs, strarg_t const fields[], count_t const count) {
	str_t **const values = calloc(count, sizeof(str_t *));
	if(!values) return NULL;
	strarg_t pos = qs;
	for(;;) {
		if('\0' == pos[0]) break;
		if('?' == pos[0] || '&' == pos[0]) ++pos;

		size_t flen = 0, sep = 0;
		for(;;) {
			char const x = pos[flen];
			if('\0' == x) break;
			if('=' == x) { ++sep; break; }
			if('&' == x) break;
			++flen;
		}
		size_t vlen = 0;
		if(sep) for(;;) {
			char const x = pos[flen+sep+vlen];
			if('\0' == x) break;
			if('&' == x) break;
			++vlen;
		}

		for(index_t i = 0; i < count; ++i) {
			if(!substr(fields[i], pos, flen)) continue;
			if(values[i]) continue;
			if(sep) {
				// TODO: Decode.
				values[i] = strndup(pos+flen+sep, vlen);
			} else {
				values[i] = strdup("true");
			}
		}
		pos += flen+sep+vlen;
	}
	return values;
}
void QSValuesFree(QSValues *const valuesptr, count_t const count) {
	str_t **values = *valuesptr;
	for(index_t i = 0; i < count; ++i) {
		FREE(&values[i]);
	}
	FREE((void **)valuesptr); values = NULL;
}

