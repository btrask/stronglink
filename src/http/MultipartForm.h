#include "HTTPMessage.h"

typedef struct MultipartForm* MultipartFormRef;
typedef struct FormPart* FormPartRef;

MultipartFormRef MultipartFormCreate(HTTPMessageRef const msg, strarg_t const type, HeaderField const *const fields, count_t const count);
void MultipartFormFree(MultipartFormRef const form);
FormPartRef MultipartFormGetPart(MultipartFormRef const form);
void *FormPartGetHeaders(FormPartRef const part);
ssize_t FormPartGetBuffer(FormPartRef const part, byte_t const **const buf);

