#include "common.h"

typedef struct URIList* URIListRef;

URIListRef URIListCreate(void);
void URIListFree(URIListRef const list);
count_t URIListGetCount(URIListRef const list);
strarg_t URIListGetURI(URIListRef const list, index_t const i);
err_t URIListAddURI(URIListRef const list, strarg_t const URI, ssize_t const len);

typedef struct LineParser* LineParserRef;
typedef err_t (*LineParserCB)(void *, strarg_t, size_t);

LineParserRef LineParserCreate(LineParserCB const cb, void *const context);
void LineParserFree(LineParserRef const p);
err_t LineParserWrite(LineParserRef const p, byte_t const *const buf, size_t const len);
err_t LineParserEnd(LineParserRef const p);
void LineParserReset(LineParserRef const p);

typedef struct URIListParser* URIListParserRef;

URIListParserRef URIListParserCreate(void);
void URIListParserFree(URIListParserRef const lp);
void URIListParserWrite(URIListParserRef const lp, byte_t const *const buf, size_t const len);
URIListRef URIListParserEnd(URIListParserRef const lp, bool_t const truncate);

