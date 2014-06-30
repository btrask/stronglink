#include "HTTPConnection.h"

typedef struct MultipartForm* MultipartFormRef;
typedef struct FormPart* FormPartRef;

MultipartFormRef MultipartFormCreate(HTTPConnectionRef const conn, strarg_t const type, HeaderFieldList const *const fields);
void MultipartFormFree(MultipartFormRef const form);
FormPartRef MultipartFormGetPart(MultipartFormRef const form);
void *FormPartGetHeaders(FormPartRef const part);
ssize_t FormPartGetBuffer(FormPartRef const part, byte_t const **const buf);

