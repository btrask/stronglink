
// our goal here is to
// 1. split markdown-related code out of our main blog file
// 2. move the changes we've made out of sundown itself
// 3. make something that can be easily compiled as a stand-alone process or in emscripten
// 4. support privilege-dropping/sandboxing





#include <stdio.h>
#include <unistd.h>

#include "../deps/sundown/src/markdown.h"
#include "../deps/sundown/html/html.h"

#include "http/QueryString.h"

#define CONVERT_MAX (1024 * 1024 * 1)

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
	byte_t const *in = NULL;
	struct buf *out = NULL;

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
	size_t const size = stats->st_size;
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
		0;
	size_t const nesting = 10;

	struct sd_callbacks callbacks[1];
	struct markdown_state state[1];
	sdhtml_renderer(callbacks, &state->opts, rflags);
	state->link = callbacks->link;
	state->autolink = callbacks->autolink;
	callbacks->link = markdown_link;
	callbacks->autolink = markdown_autolink;

	struct sd_markdown *parser = sd_markdown_new(mflags, nesting, callbacks, state);
	out = bufnew(1024 * 8); // Sundown grows this as needed.
	sd_markdown_render(out, in, size, parser);
	sd_markdown_free(parser); parser = NULL;

	// TODO: How is this supposed to work? Aren't we only writing the first 8K?
	// TODO: Are we even freeing `out`?

	size_t written = 0;
	for(;;) {
		ssize_t const r = TEMP_FAILURE_RETRY(write(file, out->data+written, out->size-written));
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





// uh...
// the whole conversion happens on the thread pool anyway
// theres no reason to use async at all in the separate process

// well, one reason is just that async is a nice cross-platform API
// it's a lot nicer than using libuv directly...

// if we use it, we should set the thread pool size to 1...
// wait
// what if we just set up the system so async thinks it's running on a thread?
// like, async_init_sync(), where yield is NULL?

// uh...
// well, one think i know for sure
// coroutines probably arent supported under emscripten
// in fact even threads are probably out

// and yes, even libuv probably doesnt work
// and if it does, it's too much overhead
// we really need the minimum possible thing

// actually these days you could probably translate libco to es6 generators
// or even compile them to earlier versions
// but it doesnt matter because we're not really doing i/o anyway
// we're just translating data in memory

// well actually
// hmm, should the "main" code handle files or not?
// i think so
// depends on the emscripten interface i guess






