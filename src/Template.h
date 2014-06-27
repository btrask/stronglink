#include "async.h"
#include "HTTPServer.h"

typedef struct Template* TemplateRef;

typedef struct {
	strarg_t var;
	strarg_t str;
	ssize_t len;
} TemplateArg;

TemplateRef TemplateCreate(strarg_t const str);
TemplateRef TemplateCreateFromPath(strarg_t const path);
void TemplateFree(TemplateRef const t);
err_t TemplateWrite(TemplateRef const t, TemplateArg const args[], count_t const argc, err_t (*writev)(void *, uv_buf_t[], unsigned int, int64_t), void *ctx);
err_t TemplateWriteHTTPChunk(TemplateRef const t, TemplateArg const args[], count_t const argc, HTTPConnectionRef const conn);
err_t TemplateWriteFile(TemplateRef const t, TemplateArg const args[], count_t const argc, uv_file const file);

str_t *htmlenc(strarg_t const str);

