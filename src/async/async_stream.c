#include <assert.h>
#include <stdlib.h>
#include "async.h"

static void alloc_cb(uv_handle_t *const handle, size_t const suggested_size, uv_buf_t *const buf) {
	buf->len = 1024 * 8; // suggested_size is hardcoded at 64k, shich seems large
	buf->base = malloc(buf->len);
}
static void read_cb(uv_stream_t *const stream, ssize_t const nread, uv_buf_t const *const buf) {
	async_read_t *const req = stream->data;
	if(nread < 0) {
		free(buf->base);
		req->buf->base = NULL;
		req->buf->len = 0;
		req->status = nread;
	} else {
		req->buf->base = buf->base;
		req->buf->len = nread;
		req->status = 0;
	}
	async_switch(req->thread);
}
int async_read(async_read_t *const req, uv_stream_t *const stream) {
	assert(req);
	req->thread = async_active();
	*req->buf = uv_buf_init(NULL, 0);
	req->status = 0;
	stream->data = req;
	int rc = uv_read_start(stream, alloc_cb, read_cb);
	if(rc < 0) {
		async_read_cleanup(req);
		req->status = rc;
		return rc;
	}
	async_yield();
	req->thread = NULL;
	rc = uv_read_stop(stream);
	if(rc < 0) {
		async_read_cleanup(req);
		req->status = rc;
		return rc;
	}
	return req->status;
}
void async_read_cleanup(async_read_t *const req) {
	assert(req);
	free(req->buf->base);
	req->thread = NULL;
	*req->buf = uv_buf_init(NULL, 0);
	req->status = 0;
}
void async_read_cancel(async_read_t *const req) {
	assert(req);
	free(req->buf->base);
	*req->buf = uv_buf_init(NULL, 0);
	req->status = UV_ECANCELED;
	async_t *const thread = req->thread;
	req->thread = NULL;
	if(thread) async_wakeup(thread);
}

static void write_cb(uv_write_t *const req, int const status) {
	async_state *const state = req->data;
	state->status = status;
	async_switch(state->thread);
}
int async_write(uv_stream_t *const stream, uv_buf_t const bufs[], unsigned const nbufs) {
	async_state state[1];
	state->thread = async_active();
	uv_write_t req[1];
	req->data = &state;
	int rc = uv_write(req, stream, bufs, nbufs, write_cb);
	if(rc < 0) return rc;
	async_yield();
	return state->status;
}


static void connect_cb(uv_connect_t *const req, int const status) {
	async_state *const state = req->data;
	state->status = status;
	async_switch(state->thread);
}
int async_tcp_connect(uv_tcp_t *const stream, struct sockaddr const *const addr) {
	async_state state[1];
	state->thread = async_active();
	uv_connect_t req[1];
	req->data = state;
	int rc = uv_tcp_connect(req, stream, addr, connect_cb);
	if(rc < 0) return rc;
	async_yield();
	return state->status;
}

