// Copyright 2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <async/async.h>
#include "strext.h"

char *vaasprintf(char const *const fmt, va_list ap) {
	va_list ap2;
	va_copy(ap2, ap);
	int rc = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	if(rc < 0) return NULL;
	char *str = malloc(rc+1);
	if(!str) return NULL;
	rc = vsnprintf(str, rc+1, fmt, ap);
	if(rc < 0) {
		free(str);
		return NULL;
	}
	return str;
}
char *aasprintf(char const *const fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	char *str = vaasprintf(fmt, ap);
	va_end(ap);
	return str;
}

int time_iso8601(char *const out, size_t const max) {
	assert(max > 0);
	time_t const now = time(NULL);
	struct tm t[1];
	gmtime_r(&now, t); // TODO: Error checking?
	size_t len = strftime(out, max, "%FT%TZ", t);
	if(0 == len) return -1;
	return 0;
}
void valogf(char const *const fmt, va_list ap) {
	async_pool_enter(NULL);
	char t[31+1];
	int rc = time_iso8601(t, sizeof(t));
	assert(rc >= 0);
	flockfile(stderr);
	fprintf(stderr, "%s ", t);
	vfprintf(stderr, fmt, ap);
	funlockfile(stderr);
	async_pool_leave(NULL);
}
void alogf(char const *const fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	valogf(fmt, ap);
	va_end(ap);
}

int uripathcmp(char const *const literal, char const *const input, char const **const qs) {
	size_t const len = strlen(literal);
	assert(len);
	if(0 != strncmp(literal, input, len)) return -1;
	// We don't ignore trailing / because it's significant.
	if('\0' != input[len] && '?' != input[len]) return -1;
	if(qs) *qs = input+len;
	return 0;
}

