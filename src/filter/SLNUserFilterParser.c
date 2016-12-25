// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <ctype.h>
#include "../StrongLink.h"

typedef struct {
	char const *str;
	size_t len;
} sstring;
#define S_STATIC(str) ((sstring){ STR_LEN(str) })
static sstring s_init(strarg_t const str) {
	if(!str) return (sstring){ NULL, 0 };
	return (sstring){ str, strlen(str) };
}
static char s_peek(sstring const *const s) {
	if(s->len <= 0) return '\0';
	return s->str[0];
}
static char s_pop(sstring *const s) {
	if(s->len <= 0) return '\0';
	char const c = s->str[0];
	s->str++;
	s->len--;
	return c;
}
static void s_skip(sstring *const s, size_t const len) {
	assert(len <= s->len);
	s->str += len;
	s->len -= len;
}
static int s_cmp(sstring const a, sstring const b) {
	if(a.len != b.len) return -1;
	if(a.str == b.str) return 0;
	return strncmp(a.str, b.str, a.len);
}
static int s_casecmp(sstring const a, sstring const b) {
	if(a.len != b.len) return -1;
	return strncasecmp(a.str, b.str, a.len);
}

static bool issep(char const c) {
	assert('\0' != c); // NUL is neither sep nor non-sep, handle it specially.
	if('(' == c || ')' == c) return true;
	// We don't need to treat quotes as punctuation, and it breaks contractions.
//	if('"' == c || '\'' == c) return true;
	if('=' == c) return true;
	if(isspace(c)) return true;
	return false;
}

static sstring read_term(sstring *const query) {
	sstring term;
	sstring q[1] = { *query };
	char const x = s_peek(q);
	if('"' == x || '\'' == x) {
		s_pop(q);
		for(; '\0' != s_peek(q) && x != s_peek(q); s_pop(q));
		term = (sstring){ query->str+1, q->str - query->str - 1 }; // TODO
		if(x == s_peek(q)) s_pop(q);
	} else {
		for(; '\0' != s_peek(q) && !issep(s_peek(q)); s_pop(q));
		term = (sstring){ query->str, q->str - query->str }; // TODO
	}
	*query = *q;
	return term;
}
static bool read_literal(sstring *const query, sstring const *const lit) {
	sstring q[1] = { *query };
	if(q->len < lit->len) return false;
	if(0 != strncasecmp(q->str, lit->str, lit->len)) return false;
	s_skip(q, lit->len);
	if('\0' != s_peek(q) && !issep(s_peek(q))) return false;
	*query = *q;
	return true;
}
static bool read_space(sstring *const query) {
	sstring q[1] = { *query };
	for(; isspace(s_peek(q)); s_pop(q));
	if(q->str == query->str) return false;
	*query = *q;
	return true;
}


static SLNFilterRef createfilter(SLNFilterType const type) {
	return SLNFilterCreateInternal(type);
}

static SLNFilterRef parse_term(sstring *const query) {
	sstring q[1] = { *query };
	sstring const term[1] = { read_term(q) };
	if(0 == term->len) return NULL;
	if(0 == s_casecmp(*term, S_STATIC("or"))) return NULL;
	if(0 == s_casecmp(*term, S_STATIC("and"))) return NULL;
	SLNFilterRef filter = createfilter(SLNFulltextFilterType);
	int rc = SLNFilterAddStringArg(filter, term->str, term->len);
	if(rc < 0) {
		SLNFilterFree(&filter);
		return NULL;
	}
	*query = *q;
	return filter;
}
static SLNFilterRef parse_attr(sstring *const query) {
	sstring q[1] = { *query };
	sstring const f[1] = { read_term(q) };
	if(!f->len) return NULL;
	if('=' != s_pop(q)) return NULL;
	sstring const v[1] = { read_term(q) };
	if(!v->len) return NULL;
	SLNFilterRef filter = NULL;
	if(0 == s_casecmp(*f, S_STATIC("target"))) {
		filter = createfilter(SLNTargetURIFilterType);
		SLNFilterAddStringArg(filter, v->str, v->len);
	} else {
		filter = createfilter(SLNMetadataFilterType);
		SLNFilterAddStringArg(filter, f->str, f->len);
		SLNFilterAddStringArg(filter, v->str, v->len);
	}
	*query = *q;
	return filter;
}
static SLNFilterRef parse_link(sstring *const query) {
	sstring q[1] = { *query };
	for(; '\0' != s_peek(q) && isalpha(s_peek(q)); s_pop(q));
	if(0 == s_cmp(*q, *query)) return NULL;
	if(':' != s_pop(q)) return NULL;
	if('/' != s_pop(q)) return NULL;
	if('/' != s_pop(q)) return NULL;
	for(; '\0' != s_peek(q) && !issep(s_peek(q)); s_pop(q));
	SLNFilterRef const filter = createfilter(SLNURIFilterType);
	SLNFilterAddStringArg(filter, query->str, q->str - query->str); // TODO
	*query = *q;
	return filter;
}
static SLNFilterRef parse_and(sstring *const query);
static SLNFilterRef parse_parens(sstring *const query) {
	sstring q[1] = { *query };
	if('(' != s_pop(q)) return NULL;
	read_space(q);
	SLNFilterRef filter = parse_and(q);
	if(')' == s_peek(q)) s_pop(q);
	else if('\0' != s_peek(q)) {
		SLNFilterFree(&filter);
		s_pop(query); // Skip '('.
		return NULL;
	}
	*query = *q;
	return filter;
}
static SLNFilterRef parse_exp(sstring *const query);
static SLNFilterRef parse_negation(sstring *const query) {
	sstring q[1] = { *query };
	if('-' != s_pop(q)) return NULL;
	SLNFilterRef subfilter = parse_exp(q);
	if(!subfilter) return NULL;
	SLNFilterRef const negation = createfilter(SLNNegationFilterType);
	SLNFilterAddFilterArg(negation, &subfilter);
	SLNFilterFree(&subfilter);
	*query = *q;
	return negation;
}
static SLNFilterRef parse_exp(sstring *const query) {
	SLNFilterRef exp = NULL;
	if(!exp) exp = parse_negation(query);
	if(!exp) exp = parse_parens(query);
	if(!exp) exp = parse_link(query);
	if(!exp) exp = parse_attr(query);
	if(!exp) exp = parse_term(query);
	return exp;
}
static SLNFilterRef parse_or(sstring *const query) {
	SLNFilterRef or = NULL;
	sstring const lit[1] = { S_STATIC("or") };
	for(;;) {
		SLNFilterRef exp = parse_exp(query);
		if(!exp) break;
		if(!or) or = createfilter(SLNUnionFilterType);
		SLNFilterAddFilterArg(or, &exp);
		SLNFilterFree(&exp);
		if(!read_space(query)) break;
		if(!read_literal(query, lit)) break;
		if(!read_space(query)) break;
	}
	return or;
}
static SLNFilterRef parse_and(sstring *const query) {
	SLNFilterRef and = NULL;
	sstring const lit[1] = { S_STATIC("and") };
	for(;;) {
		SLNFilterRef or = parse_or(query);
		if(!or) break;
		if(!and) and = createfilter(SLNIntersectionFilterType);
		SLNFilterAddFilterArg(and, &or);
		SLNFilterFree(&or);

		// Optional, ignored
		read_space(query);
		read_literal(query, lit);
		read_space(query);
	}
	return and;
}


int SLNUserFilterParse(SLNSessionRef const session, strarg_t const query, SLNFilterRef *const out) {
	if(!SLNSessionHasPermission(session, SLN_RDONLY)) return KVS_EACCES;
	if(!query) return KVS_EINVAL;
	SLNFilterRef filter;
	// Special case: show all files, including invisible.
	// TODO: This should be blog-specific?
	if(0 == strcmp("*", query)) {
		filter = createfilter(SLNAllFilterType);
		if(!filter) return KVS_ENOMEM;
		*out = filter;
		return 0;
	}
	sstring q[1] = { s_init(query) };
	filter = parse_and(q);
	if(!filter) return KVS_EINVAL;
	read_space(q);
	if('\0' != s_peek(q)) {
		SLNFilterFree(&filter);
		return KVS_EINVAL;
	}
	*out = filter;
	return 0;
}

