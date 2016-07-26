// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <yajl/yajl_gen.h>
#include <async/async.h>
#include <async/http/QueryString.h>

// TODO: Redefined from StrongLink.h.
#define SLN_META_TYPE "application/vnd.stronglink.meta"

#define LIMIT_DEFAULT (1024 * 1024 * 1)
#define LIMIT_SMALL (1024 * 16)
#define LIMIT_LARGE (1024 * 1024 * 10)

// TODO: This string is also duplicated in a couple of templates.
#define HASH_INFO_MSG "Hash URI (right click and choose copy link)"

#define CONVERTER(name) \
	int blog_convert_##name(uv_file const html, \
	                       yajl_gen const json, \
	                       char const *const buf, \
	                       size_t const size, \
	                       char const *const type)

#define TYPE_FUNC(name) \
	int blog_types_##name(char const *const type)
#define TYPE_LIST(name, types...) \
	TYPE_FUNC(name) { \
		static char const *const t[] = { types }; \
		for(size_t i = 0; i < numberof(t); i++) { \
			if(0 == strcasecmp(t[i], type)) return 0; \
		} \
		return -1; \
	}

#define numberof(x) (sizeof(x)/sizeof(*x))
#define STR_LEN(x) (x), (sizeof(x)-1)
#define uv_buf_lit(str) uv_buf_init((char *)STR_LEN(str))

// <http://daringfireball.net/2010/07/improved_regex_for_matching_urls>
// Painstakingly ported to POSIX
#define LINKIFY_RE "([a-z][a-z0-9_-]+:(/{1,3}|[a-z0-9%])|www[0-9]{0,3}[.]|[a-z0-9.-]+[.][a-z]{2,4}/)([^[:space:]()<>]+|\\(([^[:space:]()<>]+|(\\([^[:space:]()<>]+\\)))*\\))+(\\(([^[:space:]()<>]+|(\\([^[:space:]()<>]+\\)))*\\)|[^][[:space:]`!(){};:'\".,<>?«»“”‘’])"

static int write_html(uv_file const file, char const *const buf, size_t const len) {
	if(0 == len) return 0;
	uv_buf_t x = uv_buf_init((char *)buf, len);
	return async_fs_writeall(file, &x, 1, -1);
}
static int write_text(uv_file const file, char const *const buf, size_t const len) {
	if(0 == len) return 0;
	uv_buf_t chunks[30];
	size_t x = 0;
	size_t y = SIZE_MAX;
	int rc;
	for(size_t i = 0; i < len; i++) {
		switch(buf[i]) {
			case '<': chunks[x++] = uv_buf_lit("&lt;"); break;
			case '>': chunks[x++] = uv_buf_lit("&gt;"); break;
			case '&': chunks[x++] = uv_buf_lit("&amp;"); break;
			case '"': chunks[x++] = uv_buf_lit("&quot;"); break;
			case '\'': chunks[x++] = uv_buf_lit("&apos;"); break;
			default:
				if(y != x) chunks[x++] = uv_buf_init((char *)buf+i, 1);
				else chunks[x-1].len++;
				y = x;
				break;
		}
		if(x >= numberof(chunks)) {
			rc = async_fs_writeall(file, chunks, x, -1);
			if(rc < 0) return rc;
			x = 0;
			y = SIZE_MAX;
		}
	}
	rc = async_fs_writeall(file, chunks, x, -1);
	if(rc < 0) return rc;
	return 0;
}
static int write_link(uv_file const file, char const *const buf, size_t const len) {
	int rc = 0;
	if(0 == strncasecmp(buf, STR_LEN("hash:"))) {
		rc=rc<0?rc: write_html(file, STR_LEN("<a href=\"?q="));

		char *str = QSEscape(buf, len, true);
		if(!str) rc = UV_ENOMEM;
		rc=rc<0?rc: write_text(file, str, strlen(str));
		free(str); str = NULL;

		rc=rc<0?rc: write_html(file, STR_LEN("\">"));
		rc=rc<0?rc: write_text(file, buf, len);
		rc=rc<0?rc: write_html(file, STR_LEN("</a><sup>[<a href=\""));
		rc=rc<0?rc: write_text(file, buf, len);
		rc=rc<0?rc: write_html(file, STR_LEN("\" title=\"" HASH_INFO_MSG "\">#</a>]</sup>"));
	} else {
		rc=rc<0?rc: write_html(file, STR_LEN("<a href=\""));
		rc=rc<0?rc: write_text(file, buf, len);
		rc=rc<0?rc: write_html(file, STR_LEN("\">"));
		rc=rc<0?rc: write_text(file, buf, len);
		rc=rc<0?rc: write_html(file, STR_LEN("</a>"));
	}
	return rc;
}

