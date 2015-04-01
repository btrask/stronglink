// TODO: Portability

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <regex.h>

#include "../../deps/cmark/src/cmark.h"
#include "../../deps/cmark/src/buffer.h"

//#include "http/QueryString.h"

#define CONVERT_MAX (1024 * 1024 * 1) /* TODO */

#define STR_LEN(x) (x), (sizeof(x)-1)
#define RETRY(x) ({ ssize_t __x; do __x = (x); while(-1 == __x && EINTR == errno); __x; })

// Ported to the JS version in markdown.js
// The output should be identical between each version
static void md_escape(cmark_iter *const iter) {
	for(;;) {
		cmark_event_type const event = cmark_iter_next(iter);
		if(CMARK_EVENT_DONE == event) break;
		if(CMARK_EVENT_ENTER != event) continue;
		cmark_node *const node = cmark_iter_get_node(iter);
		if(CMARK_NODE_HTML != cmark_node_get_type(node)) continue;

		char const *const str = cmark_node_get_literal(node);
		cmark_node *p = cmark_node_new(CMARK_NODE_PARAGRAPH);
		cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
		cmark_node_set_literal(text, str);
		cmark_node_append_child(p, text);
		cmark_node_insert_before(node, p);
		cmark_node_free(node);
	}
}
static void md_escape_inline(cmark_iter *const iter) {
	for(;;) {
		cmark_event_type const event = cmark_iter_next(iter);
		if(CMARK_EVENT_DONE == event) break;
		if(CMARK_EVENT_ENTER != event) continue;
		cmark_node *const node = cmark_iter_get_node(iter);
		if(CMARK_NODE_INLINE_HTML != cmark_node_get_type(node)) continue;

		char const *const str = cmark_node_get_literal(node);
		cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
		cmark_node_set_literal(text, str);
		cmark_node_insert_before(node, text);
		cmark_node_free(node);
	}
}
static void md_autolink(cmark_iter *const iter) {
	regex_t linkify[1];
	// <http://daringfireball.net/2010/07/improved_regex_for_matching_urls>
	// Painstakingly ported to POSIX
	int rc = regcomp(linkify, "([a-z][a-z0-9_-]+:(/{1,3}|[a-z0-9%])|www[0-9]{0,3}[.]|[a-z0-9.-]+[.][a-z]{2,4}/)([^[:space:]()<>]+|\\(([^[:space:]()<>]+|(\\([^[:space:]()<>]+\\)))*\\))+(\\(([^[:space:]()<>]+|(\\([^[:space:]()<>]+\\)))*\\)|[^][[:space:]`!(){};:'\".,<>?«»“”‘’])", REG_ICASE | REG_EXTENDED);
	assert(0 == rc);

	for(;;) {
		cmark_event_type const event = cmark_iter_next(iter);
		if(CMARK_EVENT_DONE == event) break;
		if(CMARK_EVENT_ENTER != event) continue;
		cmark_node *const node = cmark_iter_get_node(iter);
		if(CMARK_NODE_TEXT != cmark_node_get_type(node)) continue;

		char const *const str = cmark_node_get_literal(node);
		char const *pos = str;
		regmatch_t match;
		while(0 == regexec(linkify, pos, 1, &match, 0)) {
			regoff_t const loc = match.rm_so;
			regoff_t const len = match.rm_eo - match.rm_so;

			char *a = strndup(pos, loc);
			char *b = strndup(pos+loc, len);
			assert(a);
			assert(b);

			cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
			cmark_node_set_literal(text, a);
			cmark_node *link = cmark_node_new(CMARK_NODE_LINK);
			cmark_node_set_url(link, b);
			cmark_node *face = cmark_node_new(CMARK_NODE_TEXT);
			cmark_node_set_literal(face, b);
			cmark_node_append_child(link, face);
			cmark_node_insert_before(node, text);
			cmark_node_insert_before(node, link);

			free(a); a = NULL;
			free(b); b = NULL;

			pos += loc+len;
		}

		if(str != pos) {
			cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
			cmark_node_set_literal(text, pos);
			cmark_node_insert_before(node, text);
			cmark_node_free(node);
		}

	}
	regfree(linkify);
}
static void md_block_external_images(cmark_iter *const iter) {
	for(;;) {
		cmark_event_type const event = cmark_iter_next(iter);
		if(CMARK_EVENT_DONE == event) break;
		if(CMARK_EVENT_EXIT != event) continue;
		cmark_node *const node = cmark_iter_get_node(iter);
		if(CMARK_NODE_IMAGE != cmark_node_get_type(node)) continue;

		char const *const URI = cmark_node_get_url(node);
		if(URI) {
			if(0 == strncasecmp(URI, STR_LEN("hash:"))) continue;
			if(0 == strncasecmp(URI, STR_LEN("data:"))) continue;
		}

		cmark_node *link = cmark_node_new(CMARK_NODE_LINK);
		cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
		cmark_node_set_url(link, URI);
		for(;;) {
			cmark_node *child = cmark_node_first_child(node);
			if(!child) break;
			cmark_node_append_child(link, child);
		}
		if(cmark_node_first_child(link)) {
			cmark_node_set_literal(text, " (external image)");
		} else {
			cmark_node_set_literal(text, "(external image)");
		}
		cmark_node_append_child(link, text);

		cmark_node_insert_before(node, link);
		cmark_node_free(node);
	}
}
static void md_convert_hashes(cmark_iter *const iter) {
	for(;;) {
		cmark_event_type const event = cmark_iter_next(iter);
		if(CMARK_EVENT_DONE == event) break;
		if(CMARK_EVENT_EXIT != event) continue;
		cmark_node *const node = cmark_iter_get_node(iter);
		cmark_node_type const type = cmark_node_get_type(node);
		if(CMARK_NODE_LINK != type && CMARK_NODE_IMAGE != type) continue;

		char const *const URI = cmark_node_get_url(node);
		if(!URI) continue;
		if(0 != strncasecmp(URI, STR_LEN("hash:"))) continue;

		cmark_node *hashlink = cmark_node_new(CMARK_NODE_LINK);
		cmark_node_set_url(hashlink, URI);
		cmark_node_set_title(hashlink, "Hash URI (right click and choose copy link)");

		cmark_node *sup_open = cmark_node_new(CMARK_NODE_INLINE_HTML);
		cmark_node_set_literal(sup_open, "<sup>[");
		cmark_node *sup_close = cmark_node_new(CMARK_NODE_INLINE_HTML);
		cmark_node_set_literal(sup_close, "]</sup>");
		cmark_node *face = cmark_node_new(CMARK_NODE_TEXT);
		cmark_node_set_literal(face, "#");
		cmark_node_append_child(hashlink, face);

		cmark_node_insert_after(node, sup_open);
		cmark_node_insert_after(sup_open, hashlink);
		cmark_node_insert_after(hashlink, sup_close);

		cmark_iter_reset(iter, sup_close, CMARK_EVENT_EXIT);

		size_t const URILen = strlen(URI);
		cmark_strbuf rel[1];
		char const qpfx[] = "?q=";
		cmark_strbuf_init(rel, sizeof(qpfx)-1+URILen); // TODO: Escaping?
		cmark_strbuf_put(rel, (unsigned char const *)qpfx, sizeof(qpfx)-1);
		cmark_strbuf_put(rel, (unsigned char const *)URI, URILen);
		cmark_node_set_url(node, cmark_strbuf_cstr(rel));
		cmark_strbuf_free(rel);
	}
}

int markdown_convert(char const *const dst, char const *const src) {
	int d = -1;
	FILE *s = NULL;
	cmark_node *node = NULL;
	char *str = NULL;
	size_t len = 0;

	d = open(dst, O_CREAT | O_EXCL | O_RDWR, 0400);
	if(d < 0) {
		fprintf(stderr, "Can't create %s: %s\n", dst, strerror(errno));
		goto err;
	}

	s = fopen(src, "r");
	assert(s); // TODO

#ifdef MARKDOWN_STANDALONE
// TODO: chroot, drop privileges?
#endif

	int const options =
		CMARK_OPT_DEFAULT |
		CMARK_OPT_HARDBREAKS |
		CMARK_OPT_NORMALIZE |
		CMARK_OPT_SMART |
		0;

	node = cmark_parse_file(s, options);
	fclose(s); s = NULL;
	assert(node); // TODO

	// TODO: We should be able to reset the iterator, but resetting to CMARK_EVENT_NONE doesn't work.
	cmark_iter *iter = NULL;
	iter = cmark_iter_new(node);
	assert(iter);
	md_escape(iter);
	cmark_iter_free(iter); iter = NULL;

	iter = cmark_iter_new(node);
	assert(iter);
	md_escape_inline(iter);
	cmark_iter_free(iter); iter = NULL;

	iter = cmark_iter_new(node);
	assert(iter);
	md_autolink(iter);
	cmark_iter_free(iter); iter = NULL;

	iter = cmark_iter_new(node);
	assert(iter);
	md_block_external_images(iter);
	cmark_iter_free(iter); iter = NULL;

	iter = cmark_iter_new(node);
	assert(iter);
	md_convert_hashes(iter);
	cmark_iter_free(iter); iter = NULL;


	str = cmark_render_html(node, options);
	cmark_node_free(node); node = NULL;
	assert(str); // TODO
	len = strlen(str);
	assert(len); // TODO

	size_t written = 0;
	for(;;) {
		ssize_t const r = RETRY(write(d, str+written, len-written));
		if(r < 0) {
			fprintf(stderr, "Can't write %s: %s\n", dst, strerror(errno));
			goto err;
		}
		written += (size_t)r;
		if(written >= len) break;
	}

	free(str); str = NULL;

	if(fdatasync(d) < 0) {
		fprintf(stderr, "Can't sync %s: %s\n", dst, strerror(errno));
		goto err;
	}

	// We don't need to check for errors here because we already synced.
	close(d); d = -1;
	return 0;

err:
	unlink(dst);
	close(d); d = -1;

	fclose(s); s = NULL;
	cmark_node_free(node); node = NULL;
	free(str); str = NULL;
	return -1;
}

#ifdef MARKDOWN_STANDALONE

int main(int const argc, char const *const argv[]) {
	if(argc <= 2) {
		fprintf(stderr, "Usage: %s [dst] [src]\n", argv[0]);
		return 1;
	}
	char const *const dst = argv[1];
	char const *const src = argv[2];

	int rc = markdown_convert(dst, src);
	if(rc < 0) {
		// TODO
		return 1;
	}

	return 0;
}

#endif

