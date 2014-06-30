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
	TemplateRef const t = calloc(1, sizeof(struct Template));
	t->count = 0;
	t->steps = malloc(sizeof(TemplateStep) * size);

	regex_t exp;
	regcomp(&exp, "{{[a-zA-Z0-9]\\+}}", 0);
	strarg_t pos = str;
	for(;;) {
		if(t->count >= size) {
			size *= 2;
			t->steps = realloc(t->steps, sizeof(TemplateStep) * size);
			if(!t->steps) {
				TemplateFree(t);
				return NULL;
			}
		}

		regmatch_t match;
		if(0 == regexec(&exp, pos, 1, &match, 0)) {
			regoff_t const loc = match.rm_so;
			regoff_t const len = match.rm_eo - loc;
			t->steps[t->count].str = strndup(pos, loc);
			t->steps[t->count].len = loc;
			t->steps[t->count].var = strndup(pos+loc+2, len-4);
			++t->count;
			pos += match.rm_eo;
		} else {
			t->steps[t->count].str = strdup(pos);
			t->steps[t->count].len = strlen(pos);
			t->steps[t->count].var = NULL;
			++t->count;
			break;
		}
	}
	regfree(&exp);

	return t;
}
TemplateRef TemplateCreateFromPath(strarg_t const path) {
	uv_file const file = async_fs_open(path, O_RDONLY, 0000);
	if(file < 0) return NULL;
	uv_stat_t stats;
	if(async_fs_fstat(file, &stats) < 0) {
		async_fs_close(file);
		return NULL;
	}
	int64_t const size = stats.st_size;
	if(!size || size > TEMPLATE_MAX) {
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
void TemplateFree(TemplateRef const t) {
	if(!t) return;
	for(index_t i = 0; i < t->count; ++i) {
		FREE(&t->steps[i].str);
		FREE(&t->steps[i].var);
	}
	FREE(&t->steps);
	free(t);
}
err_t TemplateWrite(TemplateRef const t, TemplateArg const args[], count_t const argc, err_t (*writev)(void *, uv_buf_t[], unsigned int, int64_t), void *ctx) {
	if(!t) return 0;
	int64_t pos = 0;
	for(index_t i = 0; i < t->count; ++i) {
		TemplateStep const *const s = &t->steps[i];
		strarg_t const sstr = s->str;
		strarg_t astr = NULL;
		size_t const slen = s->len;
		ssize_t alen = 0;
		if(s->var) {
			index_t j = 0;
			// TODO: Is binary search even worth it?
			for(; j < argc; ++j) {
				if(0 == strcmp(s->var, args[j].var)) break;
			}
			if(j >= argc) {
				fprintf(stderr, "Unrecognized argument %s\n", s->var);
				return -1;
			}
			astr = args[j].str;
			alen = args[j].len;
			if(alen < 0) alen = astr ? strlen(astr) : 0;
		}

		if(0 == slen + alen) continue;
		uv_buf_t info[] = {
			uv_buf_init((char *)sstr, slen),
			uv_buf_init((char *)astr, alen),
		};
		int const err = writev(ctx, info, numberof(info), pos);
		if(err < 0) return err;
		pos += slen + alen;
	}
	return 0;
}
err_t TemplateWriteHTTPChunk(TemplateRef const t, TemplateArg const args[], count_t const argc, HTTPConnectionRef const conn) {
	return TemplateWrite(t, args, argc, (int (*)())HTTPConnectionWriteChunkv, conn);
}
err_t TemplateWriteFile(TemplateRef const t, TemplateArg const args[], count_t const argc, uv_file const file) {
	assertf(sizeof(void *) >= sizeof(file), "Can't cast uv_file (%d) to void * (%d)", sizeof(file), sizeof(void *));
	return TemplateWrite(t, args, argc, (int (*)())async_fs_write, (void *)file);
}

str_t *htmlenc(strarg_t const str) {
	if(!str) return NULL;
	size_t total = 0;
	for(off_t i = 0; str[i]; ++i) switch(str[i]) {
		case '<': total += 4; break;
		case '>': total += 4; break;
		case '&': total += 5; break;
		case '"': total += 6; break;
		default: total += 1; break;
	}
	str_t *enc = malloc(total+1);
	if(!enc) return NULL;
	for(off_t i = 0, j = 0; str[i]; ++i) switch(str[i]) {
		case '<': memcpy(enc+j, "&lt;", 4); j += 4; break;
		case '>': memcpy(enc+j, "&gt;", 4); j += 4; break;
		case '&': memcpy(enc+j, "&amp;", 5); j += 5; break;
		case '"': memcpy(enc+j, "&quot;", 6); j += 6; break;
		default: enc[j++] = str[i]; break;
	}
	enc[total] = '\0';
	return enc;
}

