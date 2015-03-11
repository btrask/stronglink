// TODO: Portability

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../deps/cmark/src/cmark.h"
#include "../deps/cmark/src/houdini.h"

//#include "http/QueryString.h"

#define CONVERT_MAX (1024 * 1024 * 1) /* TODO */
#define RETRY(x) ({ ssize_t __x; do __x = (x); while(-1 == __x && EINTR == errno); __x; })

/*struct markdown_state {
	struct html_renderopt opts;
	int (*autolink)(struct buf *ob, const struct buf *link, enum mkd_autolink type, void *opaque);
	int (*link)(struct buf *ob, const struct buf *link, const struct buf *title, const struct buf *content, void *opaque);
};
static int markdown_link(struct buf *ob, const struct buf *link, const struct buf *title, const struct buf *content, void *opaque) {
	struct markdown_state *const state = opaque;
	if(0 != bufprefix(link, "hash://")) {
		return state->link(ob, link, title, content, opaque);
	}
	// TODO: Query string escaping
	struct buf *rel = bufnew(strlen("?q=")+link->size);
	bufputs(rel, "?q=");
	bufput(rel, link->data, link->size);
	int const r = state->link(ob, rel, title, content, opaque);
	bufrelease(rel);

	bufputs(ob, "<sup>[");
	struct buf icon = BUF_STATIC("#");
	struct buf info = BUF_STATIC("Hash URI (right click and choose copy link)");
	state->link(ob, link, &info, &icon, opaque);
	bufputs(ob, "]</sup>");

	return r;
}
static int markdown_autolink(struct buf *ob, const struct buf *link, enum mkd_autolink type, void *opaque) {
	struct markdown_state *const state = opaque;
	if(MKDA_NORMAL != type) {
		return state->autolink(ob, link, type, opaque);
	}
	char *decoded = QSUnescape((char const *)link->data, link->size, false);
	struct buf content = BUF_VOLATILE(decoded);
	int const rc = markdown_link(ob, link, NULL, &content, opaque);
	FREE(&decoded);
	return rc;
}*/

static void md_escape(cmark_event_type const event, cmark_node_type const type, cmark_node *const node) {
	if(CMARK_EVENT_ENTER != event) return;
	if(CMARK_NODE_HTML != type && CMARK_NODE_INLINE_HTML != type) return;
	// TODO: HTML nodes end up at the top level instead of wrapped in <p> tags.

	char const *const raw = cmark_node_get_literal(node);
	size_t const len = strlen(raw);
	cmark_strbuf escaped[1] = { GH_BUF_INIT };
	houdini_escape_html(escaped, (uint8_t const *)raw, len);
	cmark_node_set_literal(node, cmark_strbuf_cstr(escaped));
	cmark_strbuf_free(escaped);
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
		CMARK_OPT_SMART |
		0;

	node = cmark_parse_file(s, options);
	fclose(s); s = NULL;
	assert(node); // TODO


	cmark_iter *iter = cmark_iter_new(node);
	assert(iter);
	for(;;) {
		cmark_event_type const event = cmark_iter_next(iter);
		if(CMARK_EVENT_DONE == event) break;
		cmark_node *const node = cmark_iter_get_node(iter);
		cmark_node_type const type = cmark_node_get_type(node);
		md_escape(event, type, node);
	}
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

