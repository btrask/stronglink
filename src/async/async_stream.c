// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdlib.h>
#include <string.h> /* DEBUG */
#include "async.h"

typedef struct {
	async_t *thread;
	int status;
	uv_buf_t buf[1];
} async_state;

static void alloc_cb(uv_handle_t *const handle, size_t const suggested_size, uv_buf_t *const buf) {
	buf->len = 1024 * 8; // suggested_size is hardcoded at 64k, which seems large
	buf->base = malloc(buf->len);
}
static void read_cb(uv_stream_t *const stream, ssize_t const nread, uv_buf_t const *const buf) {
	async_state *const state = stream->data;
	if(nread < 0) {
		free(buf->base);
		state->buf->base = NULL;
		state->buf->len = 0;
		state->status = nread;
	} else {
		state->buf->base = buf->base;
		state->buf->len = nread;
		state->status = 0;
	}
	async_switch(state->thread);
}
int async_read(uv_stream_t *const stream, uv_buf_t *const out) {
	if(!stream) return UV_EINVAL;
	if(!out) return UV_EINVAL;
	async_state state[1];
	state->thread = async_active();
	state->status = 0;
	*state->buf = uv_buf_init(NULL, 0);
	stream->data = state;
	int rc = uv_read_start(stream, alloc_cb, read_cb);
	if(rc < 0) return rc;
	rc = async_yield_cancelable();
	uv_read_stop(stream);
	if(rc < 0) {
		free(state->buf->base);
		return rc;
	}
	out->base = state->buf->base;
	out->len = state->buf->len;
	return state->status;
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

