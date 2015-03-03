// TODO: Portability

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "../deps/sundown/src/markdown.h"
#include "../deps/sundown/html/html.h"

#include "http/QueryString.h"

#define CONVERT_MAX (1024 * 1024 * 1)
#define RETRY(x) ({ ssize_t __x; do __x = (x); while(-1 == __x && EINTR == errno); __x; })

struct markdown_state {
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
	struct buf info = BUF_STATIC("Hash address");
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
}

int markdown_convert(strarg_t const dst, strarg_t const src) {
	int file = -1, fd = -1;
	size_t size = 0;
	byte_t const *in = NULL;
	struct buf *out = NULL;
	struct sd_markdown *parser = NULL;

	file = open(dst, O_CREAT | O_EXCL | O_RDWR, 0400);
	if(file < 0) {
		fprintf(stderr, "Can't create %s: %s\n", dst, strerror(errno));
		goto err;
	}

	fd = open(src, O_RDONLY, 0000);
	if(fd < 0) {
		fprintf(stderr, "Can't open %s: %s\n", src, strerror(errno));
		goto err;
	}

	struct stat stats[1];
	int rc = fstat(fd, stats);
	if(rc < 0) {
		fprintf(stderr, "Can't stat %s: %s\n", src, strerror(errno));
		goto err;
	}
	size = stats->st_size;
	if(size > CONVERT_MAX) {
		fprintf(stderr, "File too large %s: %zu\n", src, size);
		goto err;
	}

	in = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	close(fd); fd = -1;
	if(MAP_FAILED == in) {
		fprintf(stderr, "Can't read %s: %s\n", src, strerror(errno));
		goto err;
	}


#ifdef MARKDOWN_STANDALONE
// TODO: chroot, drop privileges?
#endif

	unsigned int const rflags =
		HTML_ESCAPE |
		HTML_HARD_WRAP |
		0;
	unsigned int const mflags =
		MKDEXT_AUTOLINK |
		MKDEXT_FENCED_CODE |
		MKDEXT_NO_INTRA_EMPHASIS |
		MKDEXT_SUPERSCRIPT |
		MKDEXT_LAX_SPACING |
		0;
	size_t const nesting = 10;

	struct sd_callbacks callbacks[1];
	struct markdown_state state[1];
	sdhtml_renderer(callbacks, &state->opts, rflags);
	state->link = callbacks->link;
	state->autolink = callbacks->autolink;
	callbacks->link = markdown_link;
	callbacks->autolink = markdown_autolink;

	out = bufnew(1024 * 8); // Sundown grows this as needed.
	if(!out) {
		fprintf(stderr, "Can't allocate buffer\n");
		goto err;
	}
	parser = sd_markdown_new(mflags, nesting, callbacks, state);
	if(!parser) {
		fprintf(stderr, "Can't allocate parser\n");
		goto err;
	}
	sd_markdown_render(out, in, size, parser);
	sd_markdown_free(parser); parser = NULL;

	// TODO: How is this supposed to work? Aren't we only writing the first 8K?
	// TODO: Are we even freeing `out`?

	size_t written = 0;
	for(;;) {
		ssize_t const r = RETRY(write(file, out->data+written, out->size-written));
		if(r < 0) {
			fprintf(stderr, "Can't write %s: %s\n", dst, strerror(errno));
			goto err;
		}
		written += (size_t)r;
		if(written >= out->size) break;
	}
	if(fdatasync(file) < 0) {
		fprintf(stderr, "Can't sync %s: %s\n", dst, strerror(errno));
		goto err;
	}

	int const close_err = close(file); file = -1;
	if(close_err < 0) {
		fprintf(stderr, "Error while closing %s: %s\n", dst, strerror(errno));
		goto err;
	}
	munmap((byte_t *)in, size); in = NULL;
	bufrelease(out); out = NULL;
	return 0;

err:
	unlink(dst);
	close(file); file = -1;
	sd_markdown_free(parser); parser = NULL;
	munmap((byte_t *)in, size); in = NULL;
	bufrelease(out); out = NULL;
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

