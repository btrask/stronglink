#include "../common.h"

void QSValuesParse(strarg_t const qs, str_t *values[], strarg_t const fields[], count_t const count);
void QSValuesCleanup(str_t **const values, count_t const count);

str_t *QSUnescape(strarg_t const s, size_t const slen, bool const decodeSpaces);
str_t *QSEscape(strarg_t const s, size_t const slen, bool const encodeSpaces);

