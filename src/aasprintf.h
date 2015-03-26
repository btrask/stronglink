// More portable and friendlier replacements for asprintf.

#include <stdarg.h>

static char *vaasprintf(char const *const fmt, va_list ap) {
	va_list ap2;
	va_copy(ap2, ap);
	int rc = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	if(rc < 0) return NULL;
	char *str = malloc(rc+1);
	if(!str) return NULL;
	rc = vsnprintf(str, rc+1, fmt, ap);
	if(rc < 0) {
		free(str);
		return NULL;
	}
	return str;
}
static char *aasprintf(char const *const fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	char *str = vaasprintf(fmt, ap);
	va_end(ap);
	return str;
}

