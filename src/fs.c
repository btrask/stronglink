#include "fs.h"

/* Example paths:
- /asdf -> /
- / - > (error)
- "" -> (error)
- /asdf/ -> /
- /asdf/asdf -> /asdf
- asdf/asdf -> asdf
Doesn't handle "./" or "/./"
TODO: Unit tests
TODO: Windows pathnames
*/
ssize_t dirname(strarg_t const path, size_t const len) {
	if(!len) return -1;
	index_t i = len;
	if(0 == i--) return -1; // Ignore trailing slash.
	for(; i >= 0; --i) if('/' == path[i]) return i;
	return -1;
}
err_t mkdirp(str_t *const path, size_t const len, int const mode) {
	if(0 == len) return 0;
	if(1 == len) {
		if('/' == path[0]) return 0;
		if('.' == path[0]) return 0;
	}
	uv_fs_t req = { .data = co_active() };
	char const old = path[len]; // Generally should be '/' or '\0'.
	path[len] = '\0';
	uv_fs_mkdir(loop, &req, path, mode, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	path[len] = old;
	if(req.result >= 0) return 0;
	if(-EEXIST == req.result) return 0;
	if(-ENOENT != req.result) return -1;
	ssize_t const dlen = dirname(path, len);
	if(dlen < 0) return -1;
	if(mkdirp(path, dlen, mode) < 0) return -1;
	path[len] = '\0';
	uv_fs_mkdir(loop, &req, path, mode, async_fs_cb);
	co_switch(yield);
	uv_fs_req_cleanup(&req);
	path[len] = old;
	if(req.result < 0) return -1;
	return 0;
}

