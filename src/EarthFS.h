#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define BTAssert(x, fmt, ...) ({ \
	if(0 == (x)) { \
		fprintf(stderr, "%s:%d: %s assertion failed", __PRETTY_FUNCTION__, __LINE__, #x); \
		fprintf(stderr, fmt, ##__VA_ARGS__); \
		abort(); \
	} \
})
#define BTErrno(x) ({ \
	int const __x = (x); \
	if(-1 == __x) { \
		str_t msg[255+1] = {}; \
		(void)strerror_r(errno, msg, 255); \
		fprintf(stderr, "%s:%d: %s %s", __PRETTY_FUNCTION__, __LINE__, #x, msg); \
	} \
	__x; \
})
#define BTSQLiteErr(x) ({ \
	int const __x = (x); \
	if(SQLITE_OK != __x) { \
		fprintf(stderr, "%s:%d: %s %s", __PRETTY_FUNCTION__, __LINE__, #x, sqlite3_errstr(__x)); \
	} \
	__x; \
})

typedef int fd_t;
typedef unsigned char byte_t;
typedef char str_t; // Generally should be treated as constant.

typedef unsigned int EFSIndex;
typedef unsigned int EFSCount; // TODO: Appropriate types?

typedef struct EFSRepo* EFSRepoRef;
typedef struct EFSSession* EFSSessionRef;
typedef struct EFSSubmission* EFSSubmissionRef;
typedef struct EFSHasher* EFSHasherRef;
typedef struct EFSURIList* EFSURIListRef;

EFSHasherRef EFSHasherCreate(str_t const *const type);
void EFSHasherFree(EFSHasherRef const hasher);
void EFSHasherWrite(EFSHasherRef const hasher, byte_t const *const buf, ssize_t const len);

EFSURIListRef EFSHasherCreateURIList(EFSHasherRef const hasher);
void EFSURIListFree(EFSURIListRef const list);
EFSCount EFSURIListGetCount(EFSURIListRef const list);
str_t const *EFSURIListGetURI(EFSURIListRef const list, EFSIndex const x);

