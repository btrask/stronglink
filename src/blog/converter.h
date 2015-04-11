#include <regex.h>
#include <strings.h>
#include <yajl/yajl_gen.h>
#include "../async/async.h"

#define LIMIT_DEFAULT (1024 * 1024 * 1)
#define LIMIT_SMALL (1024 * 16)
#define LIMIT_LARGE (1024 * 1024 * 10)

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
#define uv_buf_lit(str) uv_buf_init(STR_LEN(str))

// <http://daringfireball.net/2010/07/improved_regex_for_matching_urls>
// Painstakingly ported to POSIX
#define LINKIFY_RE "([a-z][a-z0-9_-]+:(/{1,3}|[a-z0-9%])|www[0-9]{0,3}[.]|[a-z0-9.-]+[.][a-z]{2,4}/)([^[:space:]()<>]+|\\(([^[:space:]()<>]+|(\\([^[:space:]()<>]+\\)))*\\))+(\\(([^[:space:]()<>]+|(\\([^[:space:]()<>]+\\)))*\\)|[^][[:space:]`!(){};:'\".,<>?«»“”‘’])"

static int write_html(uv_file const file, char const *const buf, size_t const len) {
	uv_buf_t x = uv_buf_init((char *)buf, len);
	return async_fs_writeall(file, &x, 1, -1);
}
static int write_text(uv_file const file, char const *const buf, size_t const len) {
	uv_buf_t chunks[30];
	size_t x = 0;
	size_t y = SIZE_MAX;
	int rc;
	for(size_t i = 0; i < len; i++) {
		char const *rep = NULL;
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

// TODO: Clean up version in plaintext.c and expose it here instead.
/*static int write_link(uv_file const file, char const *const buf, size_t const len) {}*/

