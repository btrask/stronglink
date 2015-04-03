#include "../common.h"

typedef str_t** QSValues;

void *QSValuesCopy(strarg_t const qs, strarg_t const fields[], count_t const count);
void QSValuesFree(QSValues *const valuesptr, count_t const count);

str_t *QSUnescape(strarg_t const s, size_t const slen, bool const decodeSpaces);
str_t *QSEscape(strarg_t const s, size_t const slen, bool const encodeSpaces);

