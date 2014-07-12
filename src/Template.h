#include "async.h"
#include "http/HTTPServer.h"

typedef struct Template* TemplateRef;

TemplateRef TemplateCreate(strarg_t const str);
TemplateRef TemplateCreateFromPath(strarg_t const path);
void TemplateFree(TemplateRef *const tptr);
err_t TemplateWrite(TemplateRef const t, strarg_t (*lookup)(void const*, strarg_t), void const *const lctx, err_t (*writev)(void *, uv_buf_t[], unsigned int, int64_t), void *wctx);
err_t TemplateWriteHTTPChunk(TemplateRef const t, strarg_t (*lookup)(void const *, strarg_t), void const *lctx, HTTPMessageRef const msg);
err_t TemplateWriteFile(TemplateRef const t, strarg_t (*lookup)(void const *, strarg_t), void const *lctx, uv_file const file);

typedef struct {
	strarg_t var;
	strarg_t val;
} TemplateStaticArg;
strarg_t TemplateStaticLookup(void const *const ptr, strarg_t const var);

str_t *htmlenc(strarg_t const str);

