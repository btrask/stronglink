#include <assert.h>
#include <ctype.h>
#include "../StrongLink.h"

static SLNFilterRef parse_and(strarg_t *const query);
static SLNFilterRef parse_or(strarg_t *const query);
static SLNFilterRef parse_exp(strarg_t *const query);
static SLNFilterRef parse_negation(strarg_t *const query);
static SLNFilterRef parse_parens(strarg_t *const query);
static SLNFilterRef parse_link(strarg_t *const query);
static SLNFilterRef parse_attr(strarg_t *const query);
static SLNFilterRef parse_quoted(strarg_t *const query);
static SLNFilterRef parse_term(strarg_t *const query);
static bool parse_space(strarg_t *const query);
static bool parse_token(strarg_t *const query, strarg_t const token);

// TODO: HACK
static SLNFilterRef createfilter(SLNFilterType const type) {
	SLNFilterRef filter;
	int rc = SLNFilterCreate((SLNSessionRef)-1, type, &filter);
	assert(DB_SUCCESS == rc);
	return filter;
}

int SLNUserFilterParse(SLNSessionRef const session, strarg_t const query, SLNFilterRef *const out) {
	if(!SLNSessionHasPermission(session, SLN_RDONLY)) return DB_EACCES;
	if(!query) return DB_EINVAL;
	strarg_t pos = query;
	SLNFilterRef filter = parse_and(&pos);
	if(!filter) return DB_EINVAL;
	parse_space(&pos);
	if('\0' != *pos) {
		SLNFilterFree(&filter);
		return DB_EINVAL;
	}
	*out = filter;
	return DB_SUCCESS;
}

static SLNFilterRef parse_and(strarg_t *const query) {
	SLNFilterRef and = NULL;
	for(;;) {
		SLNFilterRef const or = parse_or(query);
		if(!or) break;
		if(!and) and = createfilter(SLNIntersectionFilterType);
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
		if(!or) or = createfilter(SLNUnionFilterType);
		SLNFilterAddFilterArg(or, exp);
		if(!parse_space(query)) break;
		if(!parse_token(query, "or")) break;
	}
	return or;
}
static SLNFilterRef parse_exp(strarg_t *const query) {
	parse_space(query);
	SLNFilterRef exp = NULL;
	if(!exp) exp = parse_negation(query);
	if(!exp) exp = parse_parens(query);
	if(!exp) exp = parse_link(query);
	if(!exp) exp = parse_attr(query);
	if(!exp) exp = parse_quoted(query);
	if(!exp) exp = parse_term(query);
	return exp;
}
static SLNFilterRef parse_negation(strarg_t *const query) {
	strarg_t q = *query;
	if('-' != *q++) return NULL;
	SLNFilterRef const subfilter = parse_exp(&q);
	if(!subfilter) return NULL;
	SLNFilterRef const negation = createfilter(SLNNegationFilterType);
	SLNFilterAddFilterArg(negation, subfilter);
	*query = q;
	return negation;
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
	SLNFilterRef const filter = createfilter(SLNMetadataFilterType);
	size_t const len = q - *query;
	SLNFilterAddStringArg(filter, "link", sizeof("link")-1);
	SLNFilterAddStringArg(filter, *query, len);
	*query = q;
	return filter;
}
static SLNFilterRef parse_attr(strarg_t *const query) {
	// TODO: Support non alnum characters, quoted fields and values, etc.
	// In fact we should have a reusable read_term that handles everything.
	strarg_t q = *query;
	strarg_t const f = q;
	for(; isalnum(*q); q++);
	size_t const flen = q - f;
	if(0 == flen) return NULL;
	if('=' != *q++) return NULL;
	strarg_t const v = q;
	for(; isalnum(*q); q++);
	size_t const vlen = q - v;
	if(0 == vlen) return NULL;
	SLNFilterRef md = createfilter(SLNMetadataFilterType);
	SLNFilterAddStringArg(md, f, flen);
	SLNFilterAddStringArg(md, v, vlen);
	*query = q;
	return md;
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
	SLNFilterRef filter = createfilter(SLNFulltextFilterType);
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
	SLNFilterRef filter = createfilter(SLNFulltextFilterType);
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
		if('\0' == *t) {
			if('\0' == *q) break;
			if(isspace(*q)) break;
			return false;
		}
		if(tolower(*q++) != *t++) return false;
	}
	*query = q;
	return true;
}

