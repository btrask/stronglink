#include "../common.h"
#include "HTTPConnection.h"

typedef struct HTTPHeaders* HTTPHeadersRef;

HTTPHeadersRef HTTPHeadersCreate(void);
HTTPHeadersRef HTTPHeadersCreateFromConnection(HTTPConnectionRef const conn);
void HTTPHeadersFree(HTTPHeadersRef *const hptr);
int HTTPHeadersLoad(HTTPHeadersRef const h, HTTPConnectionRef const conn);
strarg_t HTTPHeadersGet(HTTPHeadersRef const h, strarg_t const field);

