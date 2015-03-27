// TODO: Portability

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "../deps/cmark/src/cmark.h"

//#include "http/QueryString.h"

#define CONVERT_MAX (1024 * 1024 * 1)
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
	str_t *decoded = QSUnescape((strarg_t)link->data, link->size, false);
	struct buf content = BUF_VOLATILE(decoded);
	int const rc = markdown_link(ob, link, NULL, &content, opaque);
	FREE(&decoded);
	return rc;
}*/

int markdown_convert(strarg_t const dst, strarg_t const src) {
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
	str = cmark_render_html(node);
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
	strarg_t const dst = argv[1];
	strarg_t const src = argv[2];

	int rc = markdown_convert(dst, src);
	if(rc < 0) {
		// TODO
		return 1;
	}

	return 0;
}

#endif

