typedef struct {
	str_t *str;
	size_t len;
	off_t pos;
} PEG;

char next(PEG *const state) {
	if(++state->pos > state->len) {
		
	}
	return state->str[state->pos++];
}
char peek(PEG *const state) {
	return state->str[state->pos];
}

size_t alpha(PEG *const state) {
	if(!isalpha(peek(state))) return 0;
	state->pos++;
	return 1;
}
size_t alphas(PEG *const state) {
	
}

size_t scheme(PEG *const state, size_t *const len) {
	bool_t matched = false;
	while(schemechar(state, len)) matched = true;
	return matched;
}

#define URI_MAX 1024
#define TITLE_MAX 1024

size_t uri(byte_t const *const buf, size_t const len) {

	size_t r = 0;

	while(scheme(src)) append(dst, pop(src));
	if(':' != append(dst, pop(src))) { return 0; }
	if('/' != append(dst, pop(src))) { return 0; }
	if('/' != append(dst, pop(src))) { return 0; }
	while(domain(src)) append(dst, pop(src));
	while(path(src)) append(dst, pop(src));

	return r;

}

void meta_parse(parser) {

	off_t pos = 0;
	size_t urilen = uri(thing, pos);
	if(!urilen) return -1;
	cbs.on_source_uri(context, thing->str + pos, urilen);
	pos += urilen;
	if('\n' != next(thing, pos)) return -1;
	pos += 1;
	size_t titlelen = title(thing, pos);
	if(titlelen) {
		cbs.on_title(context, thing->str + pos, titlelen);
		pos += titlelen;
		if('\n' != next(thing, pos)) return -1;
		pos += 1;
	}
	if('\n' != next(thing, pos)) return -1;
	pos += 1;

	for(;;) {
		pos += spaces(thing, pos);
		consume(spaces());
		urilen = uri(thing, pos);
		if(urilen) {
			cbs.on_target_uri(context, thing->str + pos, urilen);
			pos += urilen;
		}
	}

}


typedef struct {
	int (*on_source_uri)(void *, strarg_t, size_t);
	int (*on_title)(void *, strarg_t, size_t);
	int (*on_target_uri)(void *, strarg_t, size_t);
	int (*on_body)(void *, strarg_t, size_t);
} parser_cbs;

