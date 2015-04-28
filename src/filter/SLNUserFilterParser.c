#include <assert.h>
#include <ctype.h>
#include "../StrongLink.h"

static bool issep(char const c) {
	assert('\0' != c); // NUL is neither sep nor non-sep, handle it specially.
	if('(' == c || ')' == c) return true;
	if('"' == c || '\'' == c) return true;
	if('=' == c) return true;
	if(isspace(c)) return true;
	return false;
}

static strarg_t read_term(strarg_t *const query, size_t *const outlen) {
	strarg_t q = *query;
	char const x = *q++;
	strarg_t term;
	size_t len;
	if('"' == x || '\'' == x) {
		for(; '\0' != *q && x != *q; q++);
		term = *query + 1;
		len = q - term;
		if(x == *q) q++;
	} else {
		for(; '\0' != *q && !issep(*q); q++);
		term = *query;
		len = q - term;
	}
	if(!len) term = NULL; // Still consume empty quotes.
	*query = q;
	if(outlen) *outlen = len;
	return term;
}
static bool read_string(strarg_t *const query, strarg_t const str) {
	strarg_t q = *query;
	size_t const len = strlen(str);
	if(0 != strncasecmp(q, str, len)) return false;
	q += len;
	if('\0' != *q && !issep(*q)) return false;
	*query = q;
	return true;
}
static bool read_space(strarg_t *const query) {
	strarg_t q = *query;
	for(; isspace(*q); ++q);
	if(q == *query) return false;
	*query = q;
	return true;
}


// TODO: HACK
static SLNFilterRef createfilter(SLNFilterType const type) {
	SLNFilterRef filter;
	int rc = SLNFilterCreate((SLNSessionRef)-1, type, &filter);
	assert(DB_SUCCESS == rc);
	return filter;
}

static SLNFilterRef parse_term(strarg_t *const query) {
	strarg_t q = *query;
	read_term(&q, NULL);
	size_t const len = q - *query;
	if(0 == len) return NULL;
	// TODO: HACK
	if(sizeof("or")-1 == len && 0 == strncasecmp("or", *query, len)) return NULL;
	if(sizeof("and")-1 == len && 0 == strncasecmp("and", *query, len)) return NULL;
	SLNFilterRef filter = createfilter(SLNFulltextFilterType);
	int rc = SLNFilterAddStringArg(filter, *query, len);
	if(DB_SUCCESS != rc) {
		SLNFilterFree(&filter);
		return NULL;
	}
	*query = q;
	return filter;
}
static SLNFilterRef parse_attr(strarg_t *const query) {
	strarg_t q = *query;
	size_t flen;
	strarg_t const f = read_term(&q, &flen);
	if(!f) return NULL;
	if('=' != *q++) return NULL;
	size_t vlen;
	strarg_t const v = read_term(&q, &vlen);
	if(!v) return NULL;
	SLNFilterRef md = createfilter(SLNMetadataFilterType);
	SLNFilterAddStringArg(md, f, flen);
	SLNFilterAddStringArg(md, v, vlen);
	*query = q;
	return md;
}
static SLNFilterRef parse_link(strarg_t *const query) {
	strarg_t q = *query;
	for(; '\0' != *q && isalpha(*q); q++);
	if(q == *query) return NULL;
	if(':' != *q++) return NULL;
	if('/' != *q++) return NULL;
	if('/' != *q++) return NULL;
	for(; '\0' != *q && !issep(*q); ++q);
	SLNFilterRef const filter = createfilter(SLNURIFilterType);
	SLNFilterAddStringArg(filter, *query, q - *query);
	*query = q;
	return filter;
}
static SLNFilterRef parse_and(strarg_t *const query);
static SLNFilterRef parse_parens(strarg_t *const query) {
	strarg_t q = *query;
	if('(' != *q++) return NULL;
	SLNFilterRef filter = parse_and(&q);
	if(')' == *q) q++;
	else if('\0' != *q) {
		SLNFilterFree(&filter);
		*query = *query + 1; // Skip '('.
		return NULL;
	}
	*query = q;
	return filter;
}
static SLNFilterRef parse_exp(strarg_t *const query);
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
static SLNFilterRef parse_exp(strarg_t *const query) {
	read_space(query);
	SLNFilterRef exp = NULL;
	if(!exp) exp = parse_negation(query);
	if(!exp) exp = parse_parens(query);
	if(!exp) exp = parse_link(query);
	if(!exp) exp = parse_attr(query);
	if(!exp) exp = parse_term(query);
	return exp;
}
static SLNFilterRef parse_or(strarg_t *const query) {
	SLNFilterRef or = NULL;
	for(;;) {
		SLNFilterRef const exp = parse_exp(query);
		if(!exp) break;
		if(!or) or = createfilter(SLNUnionFilterType);
		SLNFilterAddFilterArg(or, exp);
		if(!read_space(query)) break;
		if(!read_string(query, "or")) break;
	}
	return or;
}
static SLNFilterRef parse_and(strarg_t *const query) {
	SLNFilterRef and = NULL;
	for(;;) {
		SLNFilterRef const or = parse_or(query);
		if(!or) break;
		if(!and) and = createfilter(SLNIntersectionFilterType);
		SLNFilterAddFilterArg(and, or);

		// Optional, ignored
		read_space(query);
		read_string(query, "and");
	}
	return and;
}


int SLNUserFilterParse(SLNSessionRef const session, strarg_t const query, SLNFilterRef *const out) {
	if(!SLNSessionHasPermission(session, SLN_RDONLY)) return DB_EACCES;
	if(!query) return DB_EINVAL;
	SLNFilterRef filter;
	// Special case: show all files, including invisible.
	if(0 == strcmp("*", query)) {
		filter = createfilter(SLNAllFilterType);
		if(!filter) return DB_ENOMEM;
		*out = filter;
		return DB_SUCCESS;
	}
	strarg_t q = query;
	filter = parse_and(&q);
	if(!filter) return DB_EINVAL;
	read_space(&q);
	if('\0' != *q) {
		SLNFilterFree(&filter);
		return DB_EINVAL;
	}
	*out = filter;
	return DB_SUCCESS;
}

