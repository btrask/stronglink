#include "../common.h"

static bool_t pathterm(strarg_t const URI, size_t const len) {
	char const x = URI[len];
	return '\0' == x || '?' == x;
}

void *QSValuesCopy(strarg_t const qs, strarg_t const fields[], count_t const count);
void QSValuesFree(str_t ***const values, count_t const count);

