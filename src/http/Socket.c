// Copyright 2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include "Socket.h"

#define READ_BUFFER (1024 * 8)
#define WRITE_BUFFER (1024 * 2)

static int sock_read(SocketRef const socket, size_t const size, uv_buf_t *const out);
static int sock_write(SocketRef const socket, uv_buf_t const *const buf);
static int tls_poll(uv_stream_t *const stream, int const event);

struct Socket {
	uv_tcp_t stream[1];
	struct tls *secure;
	char *rdmem;
	uv_buf_t rd[1];
	uv_buf_t wr[1];
	int err;
};

int SocketAccept(uv_stream_t *const sstream, struct tls *const ssecure, SocketRef *const out) {
	SocketRef socket = calloc(1, sizeof(struct Socket));
	if(!socket) return UV_ENOMEM;
	int rc = uv_tcp_init(async_loop, socket->stream);
	if(rc < 0) goto cleanup;
	rc = uv_accept(sstream, (uv_stream_t *)socket->stream);
	if(rc < 0) goto cleanup;
	if(ssecure) {
		uv_os_fd_t fd;
		rc = uv_fileno((uv_handle_t *)socket->stream, &fd);
		if(rc < 0) goto cleanup;
		for(;;) {
			int event = tls_accept_socket(ssecure, &socket->secure, fd);
			if(0 == event) break;
			rc = tls_poll((uv_stream_t *)socket->stream, event);
			if(rc < 0) goto cleanup;
		}
	}
	socket->rdmem = NULL;
	*socket->rd = uv_buf_init(NULL, 0);
	*socket->wr = uv_buf_init(NULL, 0);
	*out = socket; socket = NULL;
cleanup:
	SocketFree(&socket);
	return rc;
}
int SocketConnect(strarg_t const host, strarg_t const port, SocketRef *const out) {
	// TODO
	return UV_ENOSYS;
}
void SocketFree(SocketRef *const socketptr) {
	SocketRef socket = *socketptr;
	if(!socket) return;
	if(socket->secure) tls_close(socket->secure);
	tls_free(socket->secure); socket->secure = NULL;
	async_close((uv_handle_t *)socket->stream);
	FREE(&socket->rd->base); socket->rd->len = 0;
	FREE(&socket->wr->base); socket->wr->len = 0;
	socket->err = 0;
	assert_zeroed(socket, 1);
	FREE(socketptr); socket = NULL;
}
int SocketStatus(SocketRef const socket) {
	if(!socket) return UV_EINVAL;
	return socket->err;
}

int SocketPeek(SocketRef const socket, uv_buf_t *const out) {
	if(!socket) return UV_EINVAL;
	if(0 == socket->rd->len) {
		assert(!socket->rdmem);
		int rc = sock_read(socket, READ_BUFFER, socket->rd);
		if(rc < 0) return rc;
		socket->rdmem = socket->rd->base;
	}
	*out = *socket->rd;
	return 0;
}
void SocketPop(SocketRef const socket, size_t const len) {
	if(!socket) return;
	assert(socket->rdmem);
	assert(len <= socket->rd->len);
	socket->rd->base += len;
	socket->rd->len -= len;
	if(socket->rd->len > 0) return;
	FREE(&socket->rdmem);
	socket->rd->base = NULL;
	socket->rd->len = 0;
}

int SocketWrite(SocketRef const socket, uv_buf_t const *const buf) {
	if(!socket) return UV_EINVAL;
	assert(socket->wr->len < WRITE_BUFFER);
	int rc;
	if(buf->len > WRITE_BUFFER) {
		rc = SocketFlush(socket, false);
		if(rc < 0) return rc;
		rc = sock_write(socket, buf);
		if(rc < 0) return rc;
		return 0;
	}
	if(!socket->wr->base) {
		socket->wr->base = malloc(WRITE_BUFFER);
		if(!socket->wr->base) return UV_ENOMEM;
	}
	size_t const used = MIN(WRITE_BUFFER - socket->wr->len, buf->len);
	size_t const rest = buf->len - used;
	memcpy(socket->wr->base + socket->wr->len, buf->base, used);
	socket->wr->len += used;
	if(WRITE_BUFFER == socket->wr->len) {
		rc = SocketFlush(socket, true);
		if(rc < 0) return rc;
		memcpy(socket->wr->base, buf->base + used, rest);
		socket->wr->len = rest;
		assert(rest < WRITE_BUFFER);
	}
	return 0;
}
int SocketFlush(SocketRef const socket, bool const more) {
	if(!socket) return UV_EINVAL;
	if(0 == socket->wr->len) return 0;
	assert(socket->wr->base);
	int rc = sock_write(socket, socket->wr);
	if(!more) FREE(&socket->wr->base);
	socket->wr->len = 0;
	return rc;
}


static int sock_read(SocketRef const socket, size_t const size, uv_buf_t *const out) {
	if(!socket->secure) {
		int rc = async_read((uv_stream_t *)socket->stream, size, out);
		if(rc >= 0) return rc;
		socket->err = rc;
		return rc;
	}
	out->base = malloc(size);
	if(!out->base) return UV_ENOMEM;
	size_t total = 0;
	for(;;) {
		size_t partial = 0;
		int event = tls_read(socket->secure, out->base+total, size-total, &partial);
		total += partial;
		if(0 == event) break;
		int rc = tls_poll((uv_stream_t *)socket->stream, event);
		if(rc < 0) {
			FREE(&out->base);
			socket->err = rc;
			return rc;
		}
	}
	if(0 == total) {
		FREE(&out->base);
		socket->err = UV_EOF;
		return UV_EOF;
	}
	out->len = total;
	return 0;
}
static int sock_write(SocketRef const socket, uv_buf_t const *const buf) {
	if(!socket->secure) return async_write((uv_stream_t *)socket->stream, buf, 1);
	size_t total = 0;
	for(;;) {
		size_t partial = 0;
		int event = tls_write(socket->secure,
			buf->base + total,
			buf->len - total, &partial);
		total += partial;
		if(0 == event) break;
		int rc = tls_poll((uv_stream_t *)socket->stream, event);
		if(rc < 0) return rc;
	}
	return 0;
}

static int tls_poll(uv_stream_t *const stream, int const event) {
	int rc;
	if(TLS_READ_AGAIN == event) {
		uv_buf_t buf;
		rc = async_read(stream, 0, &buf);
		if(UV_ENOBUFS == rc) rc = 0;
	} else if(TLS_WRITE_AGAIN == event) {
		uv_buf_t buf = uv_buf_init(NULL, 0);
		rc = async_write(stream, &buf, 1);
	} else {
		rc = -errno; // TODO: Might have problems on Windows?
		if(rc >= 0) rc = UV_EIO;
	}
	return rc;
}

