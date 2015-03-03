



typedef struct {
	int status;
	cothread_t *thread;
} async_req_t;


ssize_t async_read_simple(uv_stream_t *const stream, size_t const max, char **const outbuf);


int async_cancel(uv_req_t *const req);






static void alloc_cb(uv_handle_t *const handle, size_t const suggested_size, uv_buf_t *const buf) {
	struct msg_state *const state = handle->data;
	*buf = uv_buf_init((char *)state->msg->conn->buf, BUFFER_SIZE);
}




uv_alloc_cb alloc_cb


// uh.... hmm
// check to see if they've settled on an official syntax?


/*
typedef void (*uv_read_cb)(uv_read_t* req, int status);

UV_EXTERN int uv_read(uv_read_t* req,
                      uv_stream_t* handle,
                      uv_alloc_cb alloc_cb,
                      uv_read_cb read_cb);
*/

typedef struct {
	cothread_t thread;
	ssize_t nread;
	uv_buf_t *buf;
	async_alloc_cb alloc_cb;
} async_read_t;


/*static void alloc_cb(uv_handle_t *const handle, size_t const suggested_size, uv_buf_t *const buf) {
	
}*/
static void async_read_cb(uv_stream_t *const stream, ssize_t const nread, uv_buf_t const *const buf) {
	async_read_t *const req = stream->data;
	req->nread = nread;
	*req->buf = *buf;
}


ssize_t async_read(async_read_t *const req, uv_stream_t *const stream, uv_alloc_cb const alloc_cb, uv_buf_t *const buf) {
	assert(!req->thread);
	req->thread = co_active();
	stream->data = req;
	uv_read_start(stream, alloc_cb, async_read_cb);
	async_yield();
	uv_read_stop(stream);
	return req->nread;
}



// what if it were all inline without any callbacks?
// async_read_wait(); malloc(); async_read_immediate();


// no, that is obviously too much of a pain
// how about we just allocate something reasonable for the user?





typedef struct {
	cothread_t thread;
	uv_buf_t buf[1];
	ssize_t nread;
} async_read_t;

ssize_t async_read(async_read_t *const req, uv_stream_t *const stream);
void async_read_cleanup(async_read_t *const req);
void async_read_cancel(async_read_t *const req);

// how's that?


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
		req->thread = NULL;
		req->nread = rc;
		return rc;
	}
	async_yield();
	req->thread = NULL;
	rc = uv_read_stop(stream);
	if(rc < 0) {
		req->nread = rc;
		return rc;
	}
	return req->nread;
}
void async_read_cleanup(async_read_t *const req) {
	assert(req);
	free(req->buf.base);
	req->thread = NULL;
	*req->buf = uv_buf_init(NULL, 0);
	req->nread = 0;
}
void async_read_cancel(async_read_t *const req) {
	assert(req);
	free(req->buf.base);
	*req->buf = uv_buf_init(NULL, 0);
	req->nread = UV_ECANCELED;
	cothread_t const thread = req->thread;
	req->thread = NULL;
	if(thread) co_switch(thread);
}





















