

// current
int async_read(async_read_t *const req, uv_stream_t *const stream);

// goal: get rid of request object, now that fibers are directly cancelable


int async_read(uv_stream_t *const stream, uv_buf_t *const out);
// direct translation



