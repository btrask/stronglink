// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <async/async.h>
#include <async/http/HTTPServer.h>
#include "../common.h"

typedef struct Template* TemplateRef;

typedef struct {
	str_t *(*lookup)(void const *const ctx, strarg_t const var);
	void (*free)(void const *const ctx, strarg_t const var, str_t **const val);
} TemplateArgCBs;

typedef int (*TemplateWritev)(void *, uv_buf_t[], unsigned int);

int TemplateCreate(strarg_t const str, TemplateRef *const out);
int TemplateCreateFromPath(strarg_t const path, TemplateRef *const out);
void TemplateFree(TemplateRef *const tptr);
int TemplateWrite(TemplateRef const t, TemplateArgCBs const *const cbs, void const *const actx, TemplateWritev const writev, void *wctx);
int TemplateWriteHTTPChunk(TemplateRef const t, TemplateArgCBs const *const cbs, void const *actx, HTTPConnectionRef const conn);
int TemplateWriteFile(TemplateRef const t, TemplateArgCBs const *const cbs, void const *actx, uv_file const file);

typedef struct {
	strarg_t var;
	strarg_t val;
} TemplateStaticArg;
extern TemplateArgCBs const TemplateStaticCBs;

str_t *htmlenc(strarg_t const str);

