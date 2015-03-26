#ifndef COMMON_H
#define COMMON_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aasprintf.h"

typedef unsigned char byte_t;
typedef char str_t;
typedef str_t const *strarg_t; // A string that belongs to someone else.

// Deprecated
typedef size_t index_t;
typedef size_t count_t;

#define numberof(x) (sizeof(x) / sizeof(*(x)))

#define MIN(a, b) ({ \
	__typeof__(a) const __a = (a); \
	__typeof__(b) const __b = (b); \
	__a < __b ? __a : __b; \
})
#define MAX(a, b) ({ \
	__typeof__(a) const __a = (a); \
	__typeof__(b) const __b = (b); \
	__a > __b ? __a : __b; \
})

#ifdef NDEBUG
#define assertf(x, fmt, ...) (void)0
#define assert_zeroed(buf, type) (void)0
#else
#define assertf(x, fmt, ...) ({ \
	if(0 == (x)) { \
		fprintf(stderr, "%s:%d %s: assertion '%s' failed\n", \
			__FILE__, __LINE__, __PRETTY_FUNCTION__, #x); \
		fprintf(stderr, fmt, ##__VA_ARGS__); \
		fprintf(stderr, "\n"); \
		abort(); \
	} \
})
#define assert_zeroed(buf, count) ({ \
	for(index_t i = 0; i < sizeof(*buf) * count; ++i) { \
		if(0 == ((byte_t const *)(buf))[i]) continue; \
		fprintf(stderr, "%s:%d Buffer at %p not zeroed (%ld)\n", \
			__FILE__, __LINE__, (buf), i); \
		abort(); \
	} \
})
#endif

#define UNUSED(x) ((void)(x))

#define FREE(ptrptr) ({ \
	__typeof__(ptrptr) const __x = (ptrptr); \
	free(*__x); \
	*__x = NULL; \
})

// Compares nul-terminated string `a` with substring of `blen` at `b`.
static bool substr(strarg_t const a, strarg_t const b, size_t const blen) {
	size_t i = 0;
	for(; i < blen; ++i) {
		if(a[i] != b[i]) return false;
		if(!a[i]) return false; // Terminated early.
	}
	if(a[i]) return false; // Terminated late.
	return true;
}
// Returns strlen(a) if `b` starts with `a`, otherwise 0.
static size_t prefix(strarg_t const a, strarg_t const b) {
	for(size_t i = 0; ; ++i) {
		if(!a[i]) return i;
		if(a[i] != b[i]) return 0;
	}
}

static str_t *tohex(byte_t const *const buf, size_t const len) {
	str_t const map[] = "0123456789abcdef";
	str_t *const hex = calloc(len*2+1, 1);
	for(size_t i = 0; i < len; ++i) {
		hex[i*2+0] = map[0xf & (buf[i] >> 4)];
		hex[i*2+1] = map[0xf & (buf[i] >> 0)];
	}
	return hex;
}

#endif
