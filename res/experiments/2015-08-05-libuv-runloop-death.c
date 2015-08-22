// Copyright 2015 Ben Trask
// Provided under the same license as libuv.

/*
Usage:

$ cc -Wall -Wextra -Wno-unused-parameter ./2015-08-05-libuv-runloop-death.c -luv -lpthread -o 2015-08-05-libuv-runloop-death && ./2015-08-05-libuv-runloop-death
EOF - this is fine.
Dead - this should never happen!
2015-08-05-libuv-runloop-death: ./2015-08-05-libuv-runloop-death.c:101: main: Assertion `rc == 0' failed.
Aborted (core dumped)

Note that the assertion failure is only a symptom. Once the runloop stops, something has already gone wrong.

This problem doesn't happen if you comment out the ref and unref calls.

Tested on Fedora 21.

The crucial lines in libuv's src/unix/stream.c:
>     if (events & UV__POLLRDHUP)
>       stream->flags |= UV_STREAM_DISCONNECT;

These lines were introduced in commit 05a003a3f78d07185b7137601fe8e93561855a8d.
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <uv.h>

#define TEST_PORT 8000

#define ASSERT assert
#define STR_LEN(x) (x), (sizeof(x)-1)

static void on_write(uv_write_t* req, int status);


static void on_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
	buf->len = suggested_size;
	buf->base = malloc(suggested_size);
}
static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
	int rc = uv_read_stop(stream);
	ASSERT(rc == 0);

	uv_ref((uv_handle_t*)stream);

	// For this test case, we don't actually care what was written.
	free(buf->base);
	if(UV_EOF == nread) {
		fprintf(stderr, "EOF - this is fine.\n");
		uv_close((uv_handle_t*)stream, (uv_close_cb)free);
		return;
	}
	ASSERT(nread >= 0);

	uv_buf_t out = uv_buf_init(STR_LEN(
		"HTTP/1.1 200 OK\r\n"
		"Content-Length: 0\r\n"
		"Connection: keep-alive\r\n"
		"\r\n"));
	uv_write_t req[1];
	rc = uv_write(req, stream, &out, 1, on_write);
	ASSERT(rc == 0);
}
static void on_write(uv_write_t* req, int status) {
	ASSERT(status == 0);

	uv_unref((uv_handle_t*)req->handle);

	int rc = uv_read_start((uv_stream_t*)req->handle, on_alloc, on_read);
	ASSERT(rc == 0);
}

static void on_connection(uv_stream_t* server, int status) {
	uv_tcp_t* handle;
	int r;

	ASSERT(status == 0);

	handle = malloc(sizeof(*handle));
	ASSERT(handle != NULL);

	r = uv_tcp_init(server->loop, handle);
//	r = uv_tcp_init_ex(server->loop, handle, AF_INET);
	ASSERT(r == 0);

	r = uv_accept(server, (uv_stream_t*)handle);
	ASSERT(r == 0);

	uv_write_t fake[1];
	fake->handle = (uv_stream_t*)handle;
	on_write(fake, 0);
}

static void tcp_listener(uv_loop_t* loop, uv_tcp_t* server) {
	struct sockaddr_in addr;
	int r;

	ASSERT(0 == uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

	r = uv_tcp_init(loop, server);
	ASSERT(r == 0);

	r = uv_tcp_bind(server, (const struct sockaddr*) &addr, 0);
	ASSERT(r == 0);

	r = uv_listen((uv_stream_t*) server, 128, on_connection);
	ASSERT(r == 0);
}


int main(void) {
	uv_loop_t loop[1];
	int rc = uv_loop_init(loop);
	ASSERT(rc == 0);

	uv_tcp_t server[1];
	tcp_listener(loop, server);

	uv_run(loop, UV_RUN_DEFAULT);
	fprintf(stderr, "Dead - this should never happen!\n");

	rc = uv_loop_close(loop);
	ASSERT(rc == 0); // Fails since the server is still open.
	return 0;
}

