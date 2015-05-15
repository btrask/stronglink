// Based on rfc6266 <https://github.com/g2p/rfc6266>
// Licensed under the LGPL
// Copyright 2015 Ben Trask

// Warning: this code doesn't take nearly as many security precautions as it
// should. I'd like to rewrite it using a string library like from cmark.

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h> /* DEBUG */
#include <string.h>
#include <strings.h>
#include "content-disposition.h"

#define STR_LEN(x) (x), (sizeof(x)-1)

static bool separator_char(unsigned char const c) {
	assert('\0' != c);
	return NULL != strchr("()<>@,;:\\\"/[]?={} \t", c);
}
static bool ctrl_char(unsigned char const c) {
	assert('\0' != c);
	return c <= 31 || 127 == c;
}
static bool nontoken_char(unsigned char const c) {
	assert('\0' != c);
	return separator_char(c) || ctrl_char(c);
}
static bool attr_char_nonalnum(unsigned char const c) {
	assert('\0' != c);
	return NULL != strchr("!#$&+-.^_`|~", c);
}
static bool attr_char(unsigned char const c) {
	assert('\0' != c);
	return isalnum(c) || attr_char_nonalnum(c);
}
static bool token_char(unsigned char const c) {
	assert('\0' != c);
	return attr_char(c) || NULL != strchr("*'%", c);
}
static bool qdtext_char(unsigned char const c) {
	assert('\0' != c);
	return '"' != c && !ctrl_char(c);
}
static bool regular_char(unsigned char const c) {
	assert('\0' != c);
	return c <= 127;
}

static bool lit(char const **const src, char const *const str, size_t const len) {
	if(0 != strncasecmp(*src, str, len)) return false;
	*src += len;
	return true;
}

static int hexdigit(unsigned char const c) {
	if('0' <= c && c <= '9') return c - '0';
	if('a' <= c && c <= 'f') return c - 'a';
	if('A' <= c && c <= 'F') return c - 'A';
	return -1;
}
static bool read_hex_utf8(char const **const src, char **const dst) {
	char const *s = *src;
	char *d = *dst;
	if('%' != *s++) return false;
	int const hi = hexdigit(*s++);
	if(hi < 0) return false;
	int const lo = hexdigit(*s++);
	if(lo < 0) return false;
	char const c = hi << 4 | lo << 0;
	if('\0' == c) return false;
	*d++ = c;
	*src = s;
	*dst = d;
	return true;
}
static bool read_hex_iso88591(char const **const src, char **const dst) {
	return false; // TODO
}


static bool read_token(char const **const src, char **const dst) {
	char const *s = *src;
	char *d = *dst;
	while('\0' != *s && token_char(*s)) *d++ = *s++;
	if(d == *dst) return false;
	*src = s;
	*dst = d;
	return true;
}
static bool read_quoted(char const **const src, char **const dst) {
	char const *s = *src;
	char *d = *dst;
	if('"' != *s++) return false;
	for(;;) {
		if('\\' == *s && '\0' != *(s+1) && regular_char(*(s+1))) {
			s++;
			*d++ = *s++;
		} else if('\0' != *s && qdtext_char(*s)) {
			*d++ = *s++;
		} else if('"' == *s) {
			s++;
			*src = s;
			*dst = d;
			return true;
		} else {
			return false;
		}
	}
}
static bool read_value(char const **const src, char **const dst) {
	return read_token(src, dst) || read_quoted(src, dst);
}


static bool read_ext_value(char const **const src, char **const dst) {
	char const *s = *src;
	char *d = *dst;
	bool (*read_hex)(char const **const, char **const) = NULL;
	if(lit(&s, STR_LEN("UTF-8"))) {
		read_hex = read_hex_utf8;
	} else if(lit(&s, STR_LEN("ISO-8859-1"))) {
		read_hex = read_hex_iso88591;
	} else {
		return false;
	}

	if('\'' != *s++) return false;
	// TODO: Parse language attribute
	if('\'' != *s++) return false;

	for(;;) {
		if(read_hex(&s, &d)) {
			// do nothing
		} else if('\0' != *s && attr_char(*s)) {
			*d++ = *s++;
		} else {
			break;
		}
	}

	*src = s;
	*dst = d;
	return true;
}

static bool read_parm(char const **const src, char *const field, char *const value) {
	char *f = field;
	char *v = value;
	if(!read_token(src, &f)) return false;
	*f = '\0';
	if(!lit(src, STR_LEN("="))) return false;
	if('*' == *(f-1)) {
		if(!read_ext_value(src, &v)) return false;
		*v = '\0';
	} else {
		if(!read_value(src, &v)) return false;
		*v = '\0';
	}
	return true;
}

#define BUF_MAX 1024
int ContentDispositionParse(char const *const str, char **const type, char *values[], char const *const fields[], size_t const count) {
	assert(!type || !*type);
	for(size_t i = 0; i < count; i++) assert(!values[i]);

	if(strlen(str) >= BUF_MAX) return -1;
	char const *s = str;
	{
		char buf[BUF_MAX];
		char *d = buf;
		if(!read_token(&s, &d)) return -1;
		if(type) *type = strndup(buf, d-buf);
		if(type && !*type) return -1;
	}
	for(;;) {
		if(';' != *s++) break;
		while(isspace(*s)) s++;
		char field[BUF_MAX];
		char value[BUF_MAX];
		if(!read_parm(&s, field, value)) break;
		for(size_t i = 0; i < count; i++) {
			if(0 != strcasecmp(field, fields[i])) continue;
			if(values[i]) continue;
			values[i] = strdup(value);
			break;
		}
	}
	return 0;
}

