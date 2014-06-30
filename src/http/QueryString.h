#include "../common.h"

size_t QSReadField(strarg_t const qs);
size_t QSReadValue(strarg_t const qs);
size_t QSRead(strarg_t const qs, size_t *const flen, size_t *const vlen);

static bool_t pathterm(strarg_t const URI, size_t const len) {
	char const x = URI[len];
	return '\0' == x || '?' == x;
}

