#ifndef COMMON_H
#define COMMON_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int fd_t;
typedef unsigned char byte_t;
typedef char str_t;
typedef str_t const *strarg_t; // A string that belongs to someone else.
typedef signed char bool_t;
typedef off_t index_t;
typedef size_t count_t;
typedef int err_t;

#define numberof(x) (sizeof(x) / sizeof(*x))

// TODO: Where are these officially defined?
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

#define SUB_ZERO(a, b) ({ \
	__typeof__(a) const __a = (a); \
	__typeof__(b) const __b = (b); \
	__a > __b ? __a - __b : 0; \
})

#define BTAssert(x, fmt, ...) ({ \
	if(0 == (x)) { \
		fprintf(stderr, "%s:%d: assertion '%s' failed\n", __PRETTY_FUNCTION__, __LINE__, #x); \
		fprintf(stderr, fmt, ##__VA_ARGS__); \
		fprintf(stderr, "\n"); \
		abort(); \
	} \
})
#define BTErrno(x) ({ \
	int const __x = (x); \
	if(-1 == __x) { \
		str_t msg[255+1] = {}; \
		(void)strerror_r(errno, msg, 255); \
		fprintf(stderr, "%s:%d: %d == %s (%s)\n", __PRETTY_FUNCTION__, __LINE__, __x, #x, msg); \
	} \
	__x; \
})
#define BTSQLiteErr(x) ({ \
	int const __x = (x); \
	if(SQLITE_OK != __x && SQLITE_ROW != __x && SQLITE_DONE != __x) { \
		fprintf(stderr, "%s:%d: %d == %s (%s)\n", __PRETTY_FUNCTION__, __LINE__, __x, #x, sqlite3_errstr(__x)); \
	} \
	__x; \
})
#define BTUVErr(x) ({ \
	int const __x = (x); \
	if(0 != __x) { \
		fprintf(stderr, "%s:%d: %d == %s (%s)\n", __PRETTY_FUNCTION__, __LINE__, __x, #x, uv_strerror(__x)); \
	} \
	__x; \
})

#define FREE(ptrptr) ({ \
	__typeof__(ptrptr) const __x = (ptrptr); \
	free(*__x); \
	*__x = NULL; \
})

// Compares nul-terminated string `a` with substring of `blen` at `b`.
static bool_t substr(strarg_t const a, strarg_t const b, size_t const blen) {
	off_t i = 0;
	for(; i < blen; ++i) {
		if(a[i] != b[i]) return false;
		if(!a[i]) return false; // Terminated early.
	}
	if(a[i]) return false; // Terminated late.
	return true;
}
// Returns strlen(a) if `b` starts with `a`, otherwise 0.
static size_t prefix(strarg_t const a, strarg_t const b) {
	for(off_t i = 0; ; ++i) {
		if(!a[i]) return i;
		if(a[i] != b[i]) return 0;
	}
}

#endif
