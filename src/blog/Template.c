// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <regex.h>
#include "Template.h"
#include "../../deps/libressl-portable/include/compat/stdlib.h"

#define TEMPLATE_MAX (1024 * 512)

typedef struct {
	str_t *str;
	size_t len;
	str_t *var;
} TemplateStep;
struct Template {
	size_t count;
	TemplateStep *steps;
};

int TemplateCreate(strarg_t const str, TemplateRef *const out) {
	TemplateRef t = calloc(1, sizeof(struct Template));
	if(!t) return UV_ENOMEM;
	t->count = 0;
	t->steps = NULL;
	size_t size = 0;

	regex_t exp[1];
	regcomp(exp, "\\{\\{[a-zA-Z0-9]+\\}\\}", REG_EXTENDED);
	strarg_t pos = str;
	for(;;) {
		if(t->count >= size) {
			size = MAX(10, size * 2);
			t->steps = reallocarray(t->steps, size, sizeof(TemplateStep));
			if(!t->steps) {
				regfree(exp);
				TemplateFree(&t);
				return UV_ENOMEM;
			}
		}

		regmatch_t match[1];
		if(0 == regexec(exp, pos, 1, match, 0)) {
			regoff_t const loc = match->rm_so;
			regoff_t const len = match->rm_eo - loc;
			t->steps[t->count].str = strndup(pos, loc);
			t->steps[t->count].len = loc;
			t->steps[t->count].var = strndup(pos+loc+2, len-4);
			++t->count;
			pos += match->rm_eo;
		} else {
			t->steps[t->count].str = strdup(pos);
			t->steps[t->count].len = strlen(pos);
			t->steps[t->count].var = NULL;
			++t->count;
			break;
		}
	}
	regfree(exp);

	*out = t;
	return 0;
}
int TemplateCreateFromPath(strarg_t const path, TemplateRef *const out) {
	int rc = 0;
	str_t *str = NULL;
	uv_file file = async_fs_open(path, O_RDONLY, 0000);
	if(file < 0) rc = (int)file;
	if(rc < 0) goto cleanup;
	uv_fs_t req;
	rc = async_fs_fstat(file, &req);
	if(rc < 0) goto cleanup;
	int64_t const size = req.statbuf.st_size;
	if(size > TEMPLATE_MAX) rc = UV_EFBIG;
	if(rc < 0) goto cleanup;
	str = malloc((size_t)size+1);
	if(!str) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;
	uv_buf_t info = uv_buf_init(str, size);
	ssize_t len = async_fs_readall_simple(file, &info);
	if(len < 0) rc = (int)len;
	if(rc < 0) goto cleanup;
	str[size] = '\0';
	rc = TemplateCreate(str, out);
cleanup:
	if(file >= 0) async_fs_close(file);
	file = -1;
	FREE(&str);
	return rc;
}
void TemplateFree(TemplateRef *const tptr) {
	TemplateRef t = *tptr;
	if(!t) return;
	for(size_t i = 0; i < t->count; ++i) {
		FREE(&t->steps[i].str);
		t->steps[i].len = 0;
		FREE(&t->steps[i].var);
	}
	assert_zeroed(t->steps, t->count);
	FREE(&t->steps);
	t->count = 0;
	assert_zeroed(t, 1);
	FREE(tptr); t = NULL;
}
int TemplateWrite(TemplateRef const t, TemplateArgCBs const *const cbs, void const *const actx, TemplateWritev const writev, void *wctx) {
	if(!t) return 0;

	uv_buf_t *output = calloc(t->count * 2, sizeof(uv_buf_t));
	str_t **vals = calloc(t->count, sizeof(str_t *));
	if(!output || !vals) {
		FREE(&output);
		FREE(&vals);
		return UV_ENOMEM;
	}

	for(size_t i = 0; i < t->count; i++) {
		TemplateStep const *const s = &t->steps[i];
		str_t *const val = s->var ? cbs->lookup(actx, s->var) : NULL;
		size_t const len = val ? strlen(val) : 0;
		output[i*2+0] = uv_buf_init((char *)s->str, s->len);
		output[i*2+1] = uv_buf_init((char *)val, len);
		vals[i] = val;
	}

	int rc = writev(wctx, output, t->count * 2);

	FREE(&output);

	for(size_t i = 0; i < t->count; i++) {
		TemplateStep const *const s = &t->steps[i];
		if(!s->var) continue;
		if(cbs->free) cbs->free(actx, s->var, &vals[i]);
		else vals[i] = NULL;
	}
	assert_zeroed(vals, t->count);
	FREE(&vals);

	return rc;
}
int TemplateWriteHTTPChunk(TemplateRef const t, TemplateArgCBs const *const cbs, void const *const actx, HTTPConnectionRef const conn) {
	return TemplateWrite(t, cbs, actx, (TemplateWritev)HTTPConnectionWriteChunkv, conn);
}
static int async_fs_write_wrapper(uv_file const *const fdptr, uv_buf_t parts[], unsigned int const count) {
	return async_fs_writeall(*fdptr, parts, count, -1);
}
int TemplateWriteFile(TemplateRef const t, TemplateArgCBs const *const cbs, void const *const actx, uv_file const file) {
	assertf(sizeof(void *) >= sizeof(file), "Can't cast uv_file (%ld) to void * (%ld)", (long)sizeof(file), (long)sizeof(void *));
	return TemplateWrite(t, cbs, actx, (TemplateWritev)async_fs_write_wrapper, (uv_file *)&file);
}

static str_t *TemplateStaticLookup(void const *const ptr, strarg_t const var) {
	TemplateStaticArg const *args = ptr;
	assertf(args, "TemplateStaticLookup args required");
	while(args->var) {
		if(0 == strcmp(args->var, var)) return (str_t *)args->val;
		args++;
	}
	return NULL;
}
TemplateArgCBs const TemplateStaticCBs = {
	.lookup = TemplateStaticLookup,
	.free = NULL,
};


#include "../../deps/cmark/src/houdini.h"
#include "../../deps/cmark/src/buffer.h"

str_t *htmlenc(strarg_t const str) {
	if(!str) return NULL;
	cmark_strbuf out = GH_BUF_INIT;
	houdini_escape_html(&out, (uint8_t const *)str, strlen(str));
	return (str_t *)cmark_strbuf_detach(&out);
}

