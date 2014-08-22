#include <assert.h>
#include <ctype.h>
#include "EarthFS.h"

static EFSFilterRef parse_and(strarg_t *const query);
static EFSFilterRef parse_or(strarg_t *const query);
static EFSFilterRef parse_exp(strarg_t *const query);
static EFSFilterRef parse_parens(strarg_t *const query);
static EFSFilterRef parse_link(strarg_t *const query);
static EFSFilterRef parse_term(strarg_t *const query);
static bool_t parse_space(strarg_t *const query);
static bool_t parse_token(strarg_t *const query, strarg_t const token);

EFSFilterRef EFSUserFilterParse(strarg_t const query) {
	if(!query) return NULL;
	strarg_t pos = query;
	EFSFilterRef filter = parse_and(&pos);
	if(!filter) return NULL;
	parse_space(&pos);
	if('\0' != pos[0]) {
		EFSFilterFree(&filter);
		return NULL;
	}
	return filter;
}

static EFSFilterRef parse_and(strarg_t *const query) {
	EFSFilterRef and = NULL;
	for(;;) {
		EFSFilterRef const or = parse_or(query);
		if(!or) break;
		if(!and) and = EFSFilterCreate(EFSIntersectionFilterType);
		EFSFilterAddFilterArg(and, or);

		// Optional, ignored
		parse_space(query);
		parse_token(query, "and");
	}
	return and;
}
static EFSFilterRef parse_or(strarg_t *const query) {
	EFSFilterRef or = NULL;
	for(;;) {
		EFSFilterRef const exp = parse_exp(query);
		if(!exp) break;
		if(!or) or = EFSFilterCreate(EFSUnionFilterType);
		EFSFilterAddFilterArg(or, exp);
		if(!parse_space(query)) break;
		if(!parse_token(query, "or")) break;
	}
	return or;
}
static EFSFilterRef parse_exp(strarg_t *const query) {
	parse_space(query);
	EFSFilterRef exp = NULL;
	if(!exp) exp = parse_parens(query);
	if(!exp) exp = parse_link(query);
	if(!exp) exp = parse_term(query);
	return exp;
}
static EFSFilterRef parse_parens(strarg_t *const query) {
	strarg_t subquery = *query;
	if('(' != *subquery++) return NULL;
	EFSFilterRef filter = parse_and(&subquery);
	if(!filter) return NULL;
	if(')' != *subquery++) {
		EFSFilterFree(&filter);
		return NULL;
	}
	*query = subquery;
	return filter;
}
static EFSFilterRef parse_link(strarg_t *const query) {
	off_t i = 0;
	bool_t has_scheme = false;
	while(isalpha((*query)[i])) { ++i; has_scheme = true; }
	if(!has_scheme) return NULL;
	if(':' != (*query)[i++]) return NULL;
	if('/' != (*query)[i++]) return NULL;
	if('/' != (*query)[i++]) return NULL;
	while('\0' != (*query)[i] && !isspace((*query)[i])) ++i;
	EFSFilterRef const filter = EFSFilterCreate(EFSLinksToFilterType);
	EFSFilterAddStringArg(filter, *query, i);
	*query += i;
	return filter;
}
static EFSFilterRef parse_term(strarg_t *const query) {
	off_t i = 0;
	for(;;) {
		if('\0' == (*query)[i]) break;
		if(isspace((*query)[i])) break;
		if('(' == (*query)[i] || ')' == (*query)[i]) break;
		++i;
	}
	if(0 == i) return NULL;
	if(substr("or", *query, i)) return NULL;
	if(substr("and", *query, i)) return NULL;
	EFSFilterRef const filter = EFSFilterCreate(EFSFullTextFilterType);
	EFSFilterAddStringArg(filter, *query, i);
	*query += i;
	return filter;
}
static bool_t parse_space(strarg_t *const query) {
	bool_t space = false;
	while(isspace((*query)[0])) {
		*query += 1;
		space = true;
	}
	return space;
}
static bool_t parse_token(strarg_t *const query, strarg_t const token) {
	off_t i = 0;
	for(;;) {
		if('\0' == token[i]) {
			*query += i;
			return true;
		}
		if(tolower((*query)[i]) != token[i]) return false;
		++i;
	}
}

