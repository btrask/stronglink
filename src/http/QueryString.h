#include "../common.h"

static bool_t pathterm(strarg_t const URI, size_t const len) {
	char const x = URI[len];
	return '\0' == x || '?' == x;
}

typedef str_t** QSValues;

void *QSValuesCopy(strarg_t const qs, strarg_t const fields[], count_t const count);
void QSValuesFree(QSValues *const valuesptr, count_t const count);

str_t *QSUnescape(strarg_t const s, size_t const slen, bool_t const decodeSpaces);

