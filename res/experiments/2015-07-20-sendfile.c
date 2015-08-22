
static void poll_cb(uv_poll_t *const handle, int const status, int const events) {
	async_switch(handle->data);
}
int async_sendfile(uv_stream_t *const outstream, uv_file const infile, int64_t *const offset, size_t const count) {
	int outfd;
	rc = uv_fileno((uv_handle_t *)outstream, &outfd);
	if(rc < 0) return rc;

	uv_poll_t poll[1];
	poll->data = async_active();
	int rc = uv_poll_init_fd(async_loop, poll, outfd);
	if(rc < 0) goto cleanup;
	rc = uv_poll_start(poll, UV_WRITABLE, poll_cb);
	if(rc < 0) goto cleanup;
	async_yield();
	rc = uv_poll_stop(poll);
	if(rc < 0) goto cleanup;

	uv_req_t req[1];
	rc = uv_fs_sendfile(async_loop, req, (uv_file)outfd, infile, offset, count, NULL);

cleanup:
	async_close((uv_handle_t *)poll);
	return rc;
}

// okay, wtf
// poll has to happen on the main thread
// sendfile is non-blocking on the network, but blocking on disk io
// so it should happen on the thread pool

// in theory if the file data is in the buffer cache, it should be non-blocking too
// why cant kernels detect this?

/*

file = -1;
for(;;) {
	poll until writable
	enter pool
	open file (first time)
	sendfile
	leave pool
}
close file

something like this?

*/


