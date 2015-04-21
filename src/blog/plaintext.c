#include "converter.h"

// TODO: This stuff should be cleaned up and moved into converter.h.

#undef numberof /* TODO: HACK */
#undef STR_LEN
#include "../http/QueryString.h" /* TODO: Try to avoid this full dependency */

// TODO: This string is duplicated like 4 times throughout the code base
#define HASH_INFO "Hash URI (right click and choose copy link)"

static int write_link(uv_file const file, char const *const buf, size_t const len) {
	int rc = 0;
	if(0 == strncasecmp(buf, STR_LEN("hash:"))) {
		rc=rc<0?rc: write_html(file, STR_LEN("<a href=\"?q="));

		str_t *str = QSEscape(buf, len, true);
		if(!str) rc = UV_ENOMEM;
		rc=rc<0?rc: write_text(file, str, strlen(str));
		FREE(&str);

		rc=rc<0?rc: write_html(file, STR_LEN("\">"));
		rc=rc<0?rc: write_text(file, buf, len);
		rc=rc<0?rc: write_html(file, STR_LEN("</a><sup>[<a href=\""));
		rc=rc<0?rc: write_text(file, buf, len);
		rc=rc<0?rc: write_html(file, STR_LEN("\" title=\"" HASH_INFO "\">#</a>]</sup>"));
	} else {
		rc=rc<0?rc: write_html(file, STR_LEN("<a href=\""));
		rc=rc<0?rc: write_text(file, buf, len);
		rc=rc<0?rc: write_html(file, STR_LEN("\">"));
		rc=rc<0?rc: write_text(file, buf, len);
		rc=rc<0?rc: write_html(file, STR_LEN("</a>"));
	}
	return rc;
}


TYPE_LIST(plaintext,
	"text/plain; charset=utf-8",
	"text/plain")
CONVERTER(plaintext) {
	if(size > LIMIT_DEFAULT) return UV_EFBIG;

	yajl_gen_string(json, (unsigned char const *)STR_LEN("fulltext"));
	yajl_gen_string(json, (unsigned char const *)buf, size);

	yajl_gen_string(json, (unsigned char const *)STR_LEN("link"));
	yajl_gen_array_open(json);

	// TODO: We aren't cleaning this up when returning early.
	regex_t linkify[1];
	int rc = regcomp(linkify, LINKIFY_RE, REG_ICASE | REG_EXTENDED);
	if(0 != rc) return UV_UNKNOWN;

	rc = write_html(html, STR_LEN("<pre>"));
	if(rc < 0) return rc;

	char const *pos = buf;
	regmatch_t match;
	while(0 == regexec(linkify, pos, 1, &match, 0)) {
		regoff_t const loc = match.rm_so;
		regoff_t const len = match.rm_eo - match.rm_so;

		rc = write_text(html, pos, loc);
		if(rc < 0) return rc;
		rc = write_link(html, pos+loc, len);
		if(rc < 0) return rc;

		yajl_gen_string(json, (unsigned char const *)pos+loc, len);

		pos += loc+len;
	}
	rc = write_text(html, pos, size-(pos-buf));
	if(rc < 0) return rc;
	rc = write_html(html, STR_LEN("</pre>"));
	if(rc < 0) return rc;

	regfree(linkify);

	yajl_gen_array_close(json);

	return 0;
}

