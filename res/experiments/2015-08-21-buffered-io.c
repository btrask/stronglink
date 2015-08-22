
// our original plan was to make clever use of writev
// but that never panned out and libtls/openssl doesnt support it anyway
// basically we need buffered i/o

// on the read side we already have peek and pop
// on the write side we need write and flush

// HTTPConnection.c is already 800 lines so i want to split it up
// HTTPConnectionInternal or something...
// or just Socket?

// however we have at least one trick up our sleeve
// for local file access, we can read(2) directly into the buffer
// actually might be better than sendfile(2) in some cases...?



typedef struct Socket *SocketRef;

int SocketAccept(uv_stream_t *const sstream, struct tls *const ssecure, SocketRef *const out);
int SocketConnect(strarg_t const host, strarg_t const port, SocketRef *const out);
void SocketFree(SocketRef *const socketptr);

int SocketPeek(SocketRef const socket, uv_buf_t *const out);
void SocketPop(SocketRef const socket, size_t const len);

int SocketWrite(SocketRef const socket, uv_buf_t const buf);
int SocketFlush(SocketRef const socket);

int SocketWriteFromFile(SocketRef const socket, uv_file const file, size_t const len, int64_t const offset);



#define READ_BUFFER (1024 * 8)
#define WRITE_BUFFER (1024 * 2)
#define FS_BUFFER (1024 * 8)

static int sock_read(SocketRef const socket, size_t const size, uv_buf_t *const out);
static int sock_write(SocketRef const socket, uv_buf_t const buf);

struct Socket {
	uv_tcp_t stream[1];
	struct tls *secure;
	char *rdmem;
	uv_buf_t rd[1];
	uv_buf_t wr[1];
	int state;
};

int SocketAccept(uv_stream_t *const sstream, struct tls *const ssecure, SocketRef *const out) {
	SocketRef socket = calloc(1, sizeof(struct Socket));
	if(!conn) return UV_ENOMEM;
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
	*socket->rd = uv_buf_init(NULL, 0);
	*socket->wr = uv_buf_init(NULL, 0);
	socket->state = 0;
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
	if(conn->secure) tls_close(conn->secure);
	tls_free(conn->secure); conn->secure = NULL;
	async_close((uv_handle_t *)conn->stream);
	FREE(&socket->rd->base); socket->rd->len = 0;
	FREE(&socket->wr->base); socket->wr->len = 0;
	assert_zeroed(socket, 1);
	FREE(socketptr); socket = NULL;
}

int SocketPeek(SocketRef const socket, uv_buf_t *const out) {
	if(!socket) return UV_EINVAL;
	if(socket->state) return socket->state;
	if(0 == socket->rdlen) {
		assert(!socket->rdmem);
		int rc = sock_read(socket, &socket->rd);
		if(rc < 0) return rc;
		socket->rdmem = socket->rd;
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
	if(socket->rdlen > 0) return;
	FREE(&socket->rdmem);
	socket->rd->base = NULL;
	socket->rd->len = 0;
}

int SocketWrite(SocketRef const socket, uv_buf_t const buf) {
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
	}
	return 0;
}
int SocketFlush(SocketRef const socket, bool const more) {
	if(!socket) return UV_EINVAL;
	if(0 == socket->wr->len) return 0;
	assert(socket->wr->base);
	int rc = sock_write(socket, socket->wr);
	if(rc < 0) return rc;
	if(!more) FREE(&socket->wr->base);
	socket->wr->len = 0;
	return 0;
}

int SocketWriteFromFile(SocketRef const socket, uv_file const file, size_t const len, int64_t const offset) {
	int rc;
	size_t used = 0;
	int64_t pos = offset;
	if(socket->wr->len) {
		size_t const part = MIN(len, WRITE_BUFFER - socket->wr->len);
		uv_buf_t buf = uv_buf_init(
			socket->wr->base + socket->wr->len,
			part);
		ssize_t x = async_fs_read(file, &buf, 1, pos);
		if(x < 0) return x;
		if(0 == x) return x;
		
	}
}


static int sock_read(SocketRef const socket, size_t const size, uv_buf_t *const out) {
	if(!socket->secure) return async_read((uv_stream_t *)socket->stream, size, &buf);
	out->base = malloc(size);
	if(!out->base) return UV_ENOMEM;
	size_t total = 0;
	for(;;) {
		size_t partial = 0;
		int event = tls_read(socket->secure, out->base+total, size-total, &partial);
		total += partial;
		if(0 == event) {
			if(0 == total) {
				FREE(&out->base);
				return UV_EOF;
			}
			return 0;
		}
		int rc = tls_poll((uv_stream_t *)socket->stream, event);
		if(rc < 0) {
			FREE(&out->base);
			return rc;
		}
	}
	out->len = total;
	return 0;
}
static int sock_write(SocketRef const socket, uv_buf_t const buf) {
	int rc;
	if(socket->secure) {
		size_t total = 0;
		for(;;) {
			size_t partial = 0;
			int event = tls_write(socket->secure,
				buf->base + total,
				buf->len - total, &partial);
			total += partial;
			if(0 == event) return 0;
			int rc = tls_poll((uv_stream_t *)socket->stream, event);
			if(rc < 0) return rc;
		}
	} else {
		rc = async_write((uv_stream_t *)conn->stream, buf, 1);
		if(rc < 0) return rc;
	}
	return 0;
}

