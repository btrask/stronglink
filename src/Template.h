#include "async.h"
#include "http/HTTPServer.h"

typedef struct Template* TemplateRef;

typedef struct {
	str_t *(*lookup)(void const *const ctx, strarg_t const var);
	void (*free)(void const *const ctx, strarg_t const var, str_t **const val);
} TemplateArgCBs;

typedef err_t (*TemplateWritev)(void *, uv_buf_t[], unsigned int, int64_t);

TemplateRef TemplateCreate(strarg_t const str);
TemplateRef TemplateCreateFromPath(strarg_t const path);
void TemplateFree(TemplateRef *const tptr);
err_t TemplateWrite(TemplateRef const t, TemplateArgCBs const *const cbs, void const *const actx, TemplateWritev const writev, void *wctx);
err_t TemplateWriteHTTPChunk(TemplateRef const t, TemplateArgCBs const *const cbs, void const *actx, HTTPMessageRef const msg);
err_t TemplateWriteFile(TemplateRef const t, TemplateArgCBs const *const cbs, void const *actx, uv_file const file);

typedef struct {
	str_t *var;
	str_t *val;
} TemplateStaticArg;
extern TemplateArgCBs const TemplateStaticCBs;

str_t *htmlenc(strarg_t const str);

