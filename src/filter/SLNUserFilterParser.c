#include <assert.h>
#include <ctype.h>
#include "../StrongLink.h"

static SLNFilterRef parse_and(strarg_t *const query);
static SLNFilterRef parse_or(strarg_t *const query);
static SLNFilterRef parse_exp(strarg_t *const query);
static SLNFilterRef parse_parens(strarg_t *const query);
static SLNFilterRef parse_link(strarg_t *const query);
static SLNFilterRef parse_quoted(strarg_t *const query);
static SLNFilterRef parse_term(strarg_t *const query);
static bool parse_space(strarg_t *const query);
static bool parse_token(strarg_t *const query, strarg_t const token);

SLNFilterRef SLNUserFilterParse(strarg_t const query) {
	if(!query) return NULL;
	strarg_t pos = query;
	SLNFilterRef filter = parse_and(&pos);
	if(!filter) return NULL;
	parse_space(&pos);
	if('\0' != *pos) {
		SLNFilterFree(&filter);
		return NULL;
	}
	return filter;
}

static SLNFilterRef parse_and(strarg_t *const query) {
	SLNFilterRef and = NULL;
	for(;;) {
		SLNFilterRef const or = parse_or(query);
		if(!or) break;
		if(!and) and = SLNFilterCreate(SLNIntersectionFilterType);
		SLNFilterAddFilterArg(and, or);

		// Optional, ignored
		parse_space(query);
		parse_token(query, "and");
	}
	return and;
}
static SLNFilterRef parse_or(strarg_t *const query) {
	SLNFilterRef or = NULL;
	for(;;) {
		SLNFilterRef const exp = parse_exp(query);
		if(!exp) break;
		if(!or) or = SLNFilterCreate(SLNUnionFilterType);
		SLNFilterAddFilterArg(or, exp);
		if(!parse_space(query)) break;
		if(!parse_token(query, "or")) break;
	}
	return or;
}
static SLNFilterRef parse_exp(strarg_t *const query) {
	parse_space(query);
	SLNFilterRef exp = NULL;
	if(!exp) exp = parse_parens(query);
	if(!exp) exp = parse_link(query);
	if(!exp) exp = parse_quoted(query);
	if(!exp) exp = parse_term(query);
	return exp;
}
static SLNFilterRef parse_parens(strarg_t *const query) {
	strarg_t q = *query;
	if('(' != *q++) return NULL;
	SLNFilterRef filter = parse_and(&q);
	if(')' == *q) q++;
	*query = q;
	return filter;
}
static SLNFilterRef parse_link(strarg_t *const query) {
	strarg_t q = *query;
	for(; isalpha(*q); ++q);
	if(*query == q) return NULL;
	if(':' != *q++) return NULL;
	if('/' != *q++) return NULL;
	if('/' != *q++) return NULL;
	for(; '\0' != *q && !isspace(*q); ++q);
	SLNFilterRef const filter = SLNFilterCreate(SLNMetadataFilterType);
	size_t const len = q - *query;
	SLNFilterAddStringArg(filter, "link", sizeof("link")-1);
	SLNFilterAddStringArg(filter, *query, len);
	*query = q;
	return filter;
}
static SLNFilterRef parse_quoted(strarg_t *const query) {
	strarg_t q = *query;
	char const op = *q++;
	strarg_t const start = q;
	if('"' != op && '\'' != op) return NULL;
	for(;;) {
		if('\0' == *q) break;
		if(op == *q) break;
		q++;
	}
	size_t const len = q - start;
	if(op == *q) q++;
	if(0 == len) {
		*query = q;
		return NULL;
	}
	SLNFilterRef filter = SLNFilterCreate(SLNFulltextFilterType);
	int rc = SLNFilterAddStringArg(filter, start, len);
	if(rc < 0) {
		SLNFilterFree(&filter);
		*query = q;
		return NULL;
	}
	*query = q;
	return filter;
}
static SLNFilterRef parse_term(strarg_t *const query) {
	strarg_t q = *query;
	for(;;) {
		if('\0' == *q) break;
		if(isspace(*q)) break;
		if('(' == *q || ')' == *q) break;
		if('"' == *q || '\'' == *q) break;
		q++;
	}
	size_t const len = q - *query;
	if(0 == len) return NULL;
	if(substr("or", *query, len)) return NULL;
	if(substr("and", *query, len)) return NULL;
	SLNFilterRef filter = SLNFilterCreate(SLNFulltextFilterType);
	int rc = SLNFilterAddStringArg(filter, *query, len);
	if(rc < 0) {
		SLNFilterFree(&filter);
		*query = q;
		return NULL;
	}
	*query = q;
	return filter;
}
static bool parse_space(strarg_t *const query) {
	strarg_t q = *query;
	bool space = false;
	for(; isspace(*q); ++q) space = true;
	*query = q;
	return space;
}
static bool parse_token(strarg_t *const query, strarg_t const token) {
	strarg_t q = *query;
	strarg_t t = token;
	for(;;) {
		if('\0' == *t) break;
		if(tolower(*q++) != *t++) return false;
	}
	*query = q;
	return true;
}

