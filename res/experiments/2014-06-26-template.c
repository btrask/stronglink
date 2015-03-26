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
err_t TemplateWriteChunk(TemplateRef const t, TemplateArg const args[], count_t const argc, HTTPConnectionRef const conn);

str_t *htmlenc(strarg_t const str);

#include <regex.h>
#include "async.h"

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

	regex_t *exp;
	regexcomp(exp, "{{[a-zA-Z0-9]+}}", 0);
	strarg_t pos = str;
	for(;;) {
		if(t->count >= size) {
			size *= 2;
			t->steps = realloc(sizeof(TemplateStep) * size);
			if(!t->steps) {
				TemplateFree(t);
				return NULL;
			}
		}

		regmatch_t match;
		regexec(exp, pos, 1, &match, 0);
		if(match.rm_so < 0) {
			t->steps[t->count].str = strdup(pos);
			t->steps[t->count].len = strlen(pos);
			t->steps[t->count].var = NULL;
			++t->count;
			break;
		} else {
			regoff_t const loc = match.rm_so;
			regoff_t const len = match.rm_eo - loc;
			t->steps[t->count].str = strndup(pos, loc);
			t->steps[t->count].len = loc;
			t->steps[t->count].var = strndup(pos+loc+2, len-4);
			++t->count;
			pos += loc;
		}
	}
	regfree(exp);

	return t;
}
TemplateRef TemplateCreateFromPath(strarg_t const path) {
	uv_file const file = async_fs_open(path, O_RDONLY, 0000);
	if(file < 0) return NULL;
	int64_t const size = async_fs_filesize(file);
	if(size < 0 || size > TEMPLATE_MAX) {
		async_fs_close(file);
		return NULL;
	}
	str_t *str = malloc((size_t)size+1);
	uv_fs_t req = { .data = co_active() };
	uv_buf_t info = uv_buf_init(str, size);
	// TODO: async_fs_readall()
	uv_read(loop, &req, file, &info, 1, 0, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	async_fs_close(file);
	str[size] = '\0';
	TemplateRef const t = TemplateCreate(str);
	FREE(&str);
	return t;
}
void TemplateFree(TemplateRef const t) {
	if(!t) return NULL;
	for(index_t i = 0; i < t->count; ++i) {
		FREE(&t->steps[i].str);
		FREE(&t->steps[i].var);
	}
	FREE(&t->steps);
	free(t);
}
err_t TemplateWriteChunk(TemplateRef const t, TemplateArg const args[], count_t const argc, HTTPConnectionRef const conn) {
	if(!t) return 0;
	for(index_t i = 0; i < t->count; ++i) {
		TemplateStep const *const s = &t->steps[i];
		// TODO: Is binary search even worth it?
		for(index_t j = 0; j < argc; ++j) {
			if(0 == strcmp(s->var, args[j].var)) break;
		}
		if(j >= argc) return -1;
		strarg_t const sstr = s->str;
		strarg_t const astr = args[j].str;
		size_t const slen = s->len;
		ssize_t alen = args[j].len;
		if(alen < 0) alen = strlen(astr);

		if(0 == slen + alen) continue;
		HTTPConnectionWriteChunkLength(conn, slen + alen);
		HTTPConnectionWrite(conn, (byte_t const *)sstr, slen);
		HTTPConnectionWrite(conn, (byte_t const *)astr, alen);
		HTTPConnectionWrite(conn, (byte_t const *)"\r\n", 2);
	}
	return 0;
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

