

struct poll_info {
	async_t thread;
	int res;
	int events;
};
void poll_cb(uv_poll_t *const handle, int const status, int const events) {
	struct poll_info *const x = handle->data;
	x->status = status;
	x->events = events;
	async_switch(x->thread);
}
int async_poll(uv_stream_t *const stream, int *const events) {
	assert(events);
	struct poll_info info[1];
	info->thread = async_active();
	info->res = 0;
	info->events = 0;
	uv_poll_t handle[1];
	handle->data = info;

	uv_os_fd_t fd;
	int rc = uv_fileno((uv_handle_t *)stream, &fd);
	if(rc < 0) return rc;
	rc = uv_poll_init(async_loop, handle, (uv_os_sock_t)fd);
	if(rc < 0) return rc;
	rc = uv_poll_start(handle, *events, poll_cb);
	if(rc < 0) goto cleanup;
	async_yield();
	rc = uv_poll_stop(handle);
cleanup:
	async_close((uv_handle_t *)handle);
	if(info->res < 0) return info->res;
	if(rc < 0) return rc;
	*events = info->events;
	return 0;
}























