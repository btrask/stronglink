#ifndef COMMON_H
#define COMMON_H

#include <errno.h>
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
		fprintf(stderr, "%s:%d: %s %s\n", __PRETTY_FUNCTION__, __LINE__, #x, msg); \
	} \
	__x; \
})
#define BTSQLiteErr(x) ({ \
	int const __x = (x); \
	if(SQLITE_OK != __x) { \
		fprintf(stderr, "%s:%d: %s %s\n", __PRETTY_FUNCTION__, __LINE__, #x, sqlite3_errstr(__x)); \
	} \
	__x; \
})

#define FREE(ptrptr) ({ \
	__typeof__(ptrptr) const __x = (ptrptr); \
	free(*__x); \
	*__x = NULL; \
})

#endif
