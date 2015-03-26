#include <regex.h>
#include "Template.h"

#define TEMPLATE_MAX (1024 * 512)

typedef struct {
	str_t *str;
	size_t len;
	str_t *var;
} TemplateStep;
struct Template {
	count_t count;
	TemplateStep *steps;
};

TemplateRef TemplateCreate(strarg_t const str) {
	count_t size = 10;
	TemplateRef t = calloc(1, sizeof(struct Template));
	if(!t) return NULL;
	t->count = 0;
	t->steps = malloc(sizeof(TemplateStep) * size);
	if(!t->steps) {
		TemplateFree(&t);
		return NULL;
	}

	regex_t exp[1];
	regcomp(exp, "\\{\\{[a-zA-Z0-9]+\\}\\}", REG_EXTENDED);
	strarg_t pos = str;
	for(;;) {
		if(t->count >= size) {
			size *= 2;
			t->steps = realloc(t->steps, sizeof(TemplateStep) * size);
			if(!t->steps) {
				regfree(exp);
				TemplateFree(&t);
				return NULL;
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

	return t;
}
TemplateRef TemplateCreateFromPath(strarg_t const path) {
	uv_file const file = async_fs_open(path, O_RDONLY, 0000);
	if(file < 0) return NULL;
	uv_fs_t req;
	if(async_fs_fstat(file, &req) < 0) {
		async_fs_close(file);
		return NULL;
	}
	int64_t const size = req.statbuf.st_size;
	if(size > TEMPLATE_MAX) {
		async_fs_close(file);
		return NULL;
	}
	str_t *str = malloc((size_t)size+1);
	uv_buf_t info = uv_buf_init(str, size);
	async_fs_read(file, &info, 1, 0); // TODO: Loop
	async_fs_close(file);
	str[size] = '\0';
	TemplateRef const t = TemplateCreate(str);
	FREE(&str);
	return t;
}
void TemplateFree(TemplateRef *const tptr) {
	TemplateRef t = *tptr;
	if(!t) return;
	for(index_t i = 0; i < t->count; ++i) {
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
	int64_t pos = 0;
	for(index_t i = 0; i < t->count; ++i) {
		TemplateStep const *const s = &t->steps[i];
		strarg_t const sstr = s->str;
		str_t *astr = NULL;
		size_t const slen = s->len;
		ssize_t alen = 0;
		if(s->var) {
			astr = cbs->lookup(actx, s->var);
			alen = astr ? strlen(astr) : 0;
		}

		if(0 == slen + alen) {
			if(s->var && cbs->free) cbs->free(actx, s->var, &astr);
			continue;
		}
		uv_buf_t info[] = {
			uv_buf_init((char *)sstr, slen),
			uv_buf_init((char *)astr, alen),
		};
		int const err = writev(wctx, info, numberof(info), pos);
		if(s->var && cbs->free) cbs->free(actx, s->var, &astr);
		if(err < 0) return err;
		pos += slen + alen;
	}
	return 0;
}
int TemplateWriteHTTPChunk(TemplateRef const t, TemplateArgCBs const *const cbs, void const *const actx, HTTPConnectionRef const conn) {
	return TemplateWrite(t, cbs, actx, (TemplateWritev)HTTPConnectionWriteChunkv, conn);
}
static int async_fs_write_wrapper(uv_file const *const fdptr, uv_buf_t const parts[], unsigned int const count, int64_t const offset) {
	return async_fs_write(*fdptr, parts, count, offset);
}
int TemplateWriteFile(TemplateRef const t, TemplateArgCBs const *const cbs, void const *const actx, uv_file const file) {
	assertf(sizeof(void *) >= sizeof(file), "Can't cast uv_file (%ld) to void * (%ld)", (long)sizeof(file), (long)sizeof(void *));
	return TemplateWrite(t, cbs, actx, (TemplateWritev)async_fs_write_wrapper, (uv_file *)&file);
}

static str_t *TemplateStaticLookup(void const *const ptr, strarg_t const var) {
	TemplateStaticArg const *args = ptr;
	assertf(args, "TemplateStaticLookup args required");
	while(args->var) {
		if(0 == strcmp(args->var, var)) return args->val;
		args++;
	}
	return NULL;
}
TemplateArgCBs const TemplateStaticCBs = {
	.lookup = TemplateStaticLookup,
	.free = NULL,
};

str_t *htmlenc(strarg_t const str) {
	if(!str) return NULL;
	size_t total = 0;
	for(size_t i = 0; str[i]; ++i) switch(str[i]) {
		case '<': total += 4; break;
		case '>': total += 4; break;
		case '&': total += 5; break;
		case '"': total += 6; break;
		default: total += 1; break;
	}
	str_t *enc = malloc(total+1);
	if(!enc) return NULL;
	for(size_t i = 0, j = 0; str[i]; ++i) switch(str[i]) {
		case '<': memcpy(enc+j, "&lt;", 4); j += 4; break;
		case '>': memcpy(enc+j, "&gt;", 4); j += 4; break;
		case '&': memcpy(enc+j, "&amp;", 5); j += 5; break;
		case '"': memcpy(enc+j, "&quot;", 6); j += 6; break;
		default: enc[j++] = str[i]; break;
	}
	enc[total] = '\0';
	return enc;
}

