#include <assert.h>
#include <stdlib.h>
#include "async.h"

static void alloc_cb(uv_handle_t *const handle, size_t const suggested_size, uv_buf_t *const buf) {
	buf->len = 1024 * 8; // suggested_size is hardcoded at 64k, shich seems large
	buf->base = malloc(buf->len);
}
static void read_cb(uv_stream_t *const stream, ssize_t const nread, uv_buf_t const *const buf) {
	async_read_t *const req = stream->data;
	*req->buf = *buf;
	req->nread = nread;
	co_switch(req->thread);
}
ssize_t async_read(async_read_t *const req, uv_stream_t *const stream) {
	assert(req);
	req->thread = co_active();
	*req->buf = uv_buf_init(NULL, 0);
	req->nread = 0;
	stream->data = req;
	int rc = uv_read_start(stream, alloc_cb, read_cb);
	if(rc < 0) {
		async_read_cleanup(req);
		req->nread = rc;
		return rc;
	}
	async_yield();
	req->thread = NULL;
	rc = uv_read_stop(stream);
	if(rc < 0) {
		async_read_cleanup(req);
		req->nread = rc;
		return rc;
	}
	return req->nread;
}
void async_read_cleanup(async_read_t *const req) {
	assert(req);
	free(req->buf->base);
	req->thread = NULL;
	*req->buf = uv_buf_init(NULL, 0);
	req->nread = 0;
}
void async_read_cancel(async_read_t *const req) {
	assert(req);
	free(req->buf->base);
	*req->buf = uv_buf_init(NULL, 0);
	req->nread = UV_ECANCELED;
	cothread_t const thread = req->thread;
	req->thread = NULL;
	if(thread) co_switch(thread);
}

