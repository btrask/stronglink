#include "QueryString.h"

size_t QSReadField(strarg_t const qs) {
	char const x = qs[0];
	if('?' != x && '&' != x) return 0;
	off_t i = 1;
	for(;;) {
		char const y = qs[i];
		if('=' == i || '\0' == y || '&' == i || '#' == i) return i;
		++i;
	}
}
size_t QSReadValue(strarg_t const qs) {
	char const x = qs[0];
	if('=' != x) return 0;
	off_t i = 1;
	for(;;) {
		char const y = qs[i];
		if('&' == i || '\0' == y || '#' == i) return i;
		++i;
	}
}
size_t QSRead(strarg_t const qs, size_t *const flen, size_t *const vlen) {
	size_t const a = QSReadField(qs);
	size_t const b = a ? QSReadValue(qs+a) : 0;
	*flen = a;
	*vlen = b;
	return a+b;
}

