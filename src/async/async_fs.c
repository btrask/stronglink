// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <stdio.h> /* For debugging */
#include <stdlib.h>
#include <string.h>

#include "async.h"
#include "../util/aasprintf.h"

#define ENTROPY_BYTES 8

static void fs_cb(uv_fs_t *const req) {
	async_switch(req->data);
}

#if 1
#define ASYNC_FS_WRAP(name, args...) \
	async_pool_enter(NULL); \
	uv_fs_t req[1]; \
	int rc = uv_fs_##name(async_loop, req, ##args, NULL); \
	uv_fs_req_cleanup(req); \
	async_pool_leave(NULL); \
	if(rc < 0) return rc; \
	return req->result;
#else
#define ASYNC_FS_WRAP(name, args...) \
	uv_fs_t req[1]; \
	uv_fs_cb cb = NULL; \
	if(async_main) { \
		req->data = async_active(); \
		cb = fs_cb; \
	} \
	int rc = uv_fs_##name(async_loop, req, ##args, cb); \
	if(rc < 0) return rc; \
	if(async_main) { \
		rc = async_yield_cancelable(); \
		if(UV_ECANCELED == rc) { \
			uv_cancel((uv_req_t *)req); \
			uv_fs_req_cleanup(req); \
			return rc; \
		} \
	} \
	uv_fs_req_cleanup(req); \
	if(rc < 0) return rc; \
	return req->result;
#endif

uv_file async_fs_open(const char* path, int flags, int mode) {
	ASYNC_FS_WRAP(open, path, flags, mode)
}
int async_fs_close(uv_file file) {
	ASYNC_FS_WRAP(close, file)
}
ssize_t async_fs_read(uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset) {
	ASYNC_FS_WRAP(read, file, bufs, nbufs, offset)
}
ssize_t async_fs_write(uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset) {
	ASYNC_FS_WRAP(write, file, bufs, nbufs, offset)
}
int async_fs_unlink(const char* path) {
	ASYNC_FS_WRAP(unlink, path)
}
int async_fs_link(const char* path, const char* new_path) {
	ASYNC_FS_WRAP(link, path, new_path)
}
int async_fs_fsync(uv_file file) {
	ASYNC_FS_WRAP(fsync, file)
}
int async_fs_fdatasync(uv_file file) {
	// TODO: Apparently fdatasync(2) is broken on some versions of
	// Linux 3.x when the file size grows. Make sure that either libuv
	// takes care of it or that it doesn't affect us. Cf. MDB changelog.
	ASYNC_FS_WRAP(fdatasync, file)
}
int async_fs_mkdir_nosync(const char* path, int mode) {
	ASYNC_FS_WRAP(mkdir, path, mode)
}
int async_fs_ftruncate(uv_file file, int64_t offset) {
	ASYNC_FS_WRAP(ftruncate, file, offset)
}

int async_fs_symlink(const char* path, const char* new_path, int flags) {
	ASYNC_FS_WRAP(symlink, path, new_path, flags);
}

ssize_t async_fs_readall_simple(uv_file const file, uv_buf_t const *const buf) {
	async_pool_enter(NULL);
	size_t pos = 0;
	ssize_t rc;
	for(;;) {
		uv_buf_t b2 = uv_buf_init(buf->base+pos, buf->len-pos);
		rc = async_fs_read(file, &b2, 1, -1);
		if(rc < 0) break;
		if(0 == rc) { rc = pos; break; }
		pos += rc;
		if(pos >= buf->len) { rc = pos; break; }
	}
	async_pool_leave(NULL);
	return rc;
}
int async_fs_writeall(uv_file const file, uv_buf_t bufs[], unsigned int const nbufs, int64_t const offset) {
	async_pool_enter(NULL);
	int64_t pos = offset;
	unsigned used = 0;
	int rc = 0;
	for(;;) {
		ssize_t len = async_fs_write(file, bufs+used, nbufs-used, pos);
		if(len < 0) {
			rc = len;
			goto cleanup;
		}
		if(pos >= 0) pos += len;
		for(;;) {
			if(used >= nbufs) {
				rc = 0;
				goto cleanup;
			}
			size_t const x = len < bufs[used].len ? len : bufs[used].len;
			bufs[used].base += x;
			bufs[used].len -= x;
			len -= x;
			if(bufs[used].len) break;
			used++;
		}
	}
cleanup:
	async_pool_leave(NULL);
	return rc;
}

int async_fs_fstat(uv_file file, uv_fs_t *const req) {
	async_pool_enter(NULL);
	uv_fs_t _req[1];
	int const err = uv_fs_fstat(async_loop, req ? req : _req, file, NULL);
	uv_fs_req_cleanup(req ? req : _req);
	async_pool_leave(NULL);
	return err;
}
int async_fs_stat_mode(const char* path, uv_fs_t *const req) {
	async_pool_enter(NULL);
	uv_fs_t _req[1];
	int const err = uv_fs_stat(async_loop, req ? req : _req, path, NULL);
	uv_fs_req_cleanup(req ? req : _req);
	async_pool_leave(NULL);
	return err;
}

// Example paths:
// - /asdf -> /
// - / - > (error)
// - "" -> (error)
//- /asdf/ -> /
// - /asdf/asdf -> /asdf
// - asdf/asdf -> asdf
// Doesn't handle "./" or "/./"
// TODO: Unit tests
// TODO: Windows pathnames
static ssize_t dirlen(char const *const path, size_t const len) {
	size_t i = len;
	if(i < 1) return UV_ENOENT;
	i--; // The last character cannot be a "real" path separator.
	while(i--) if('/' == path[i]) return i;
	return UV_ENOENT;
}
int async_fs_open_dirname(const char* path, int flags, int mode) {
	ssize_t dlen = dirlen(path, strlen(path));
	if(dlen < 0) return (int)dlen;
	char *mutable = strndup(path, dlen);
	int rc = async_fs_open(mutable, flags, mode);
	free(mutable); mutable = NULL;
	return rc;
}
int async_fs_sync_dirname(const char* path) {
	async_pool_enter(NULL);
	int rc = async_fs_open_dirname(path, O_RDONLY, 0000);
	if(rc >= 0) {
		uv_file parent = rc;
		rc = async_fs_fdatasync(parent);
		async_fs_close(parent); parent = -1;
	}
	async_pool_leave(NULL);
	return rc;
}
int async_fs_mkdir_sync(const char* path, int mode) {
	async_pool_enter(NULL);
	int rc = async_fs_mkdir_nosync(path, mode);
	if(rc >= 0) {
		rc = async_fs_sync_dirname(path);
	}
	async_pool_leave(NULL);
	return rc;
}

static int mkdirp_internal(char *const path, size_t const len, int const mode) {
	if(0 == len) return 0;
	if(1 == len) {
		if('/' == path[0]) return 0;
		if('.' == path[0]) return 0;
	}
	int result;
	char const old = path[len]; // Generally should be '/' or '\0'.
	path[len] = '\0';
	result = async_fs_mkdir_sync(path, mode);
	path[len] = old;
	if(result >= 0) return 0;
	if(UV_EEXIST == result) return 0;
	if(UV_ENOENT != result) return -1;
	ssize_t const dlen = dirlen(path, len);
	if(dlen < 0) return (int)dlen;
	if(mkdirp_internal(path, dlen, mode) < 0) return -1;
	path[len] = '\0';
	result = async_fs_mkdir_sync(path, mode);
	path[len] = old;
	if(result < 0) return -1;
	return 0;
}
int async_fs_mkdirp(char const *const path, int const mode) {
	size_t const len = strlen(path);
	char *mutable = strndup(path, len);
	int const err = mkdirp_internal(mutable, len, mode);
	free(mutable); mutable = NULL;
	return err;
}
int async_fs_mkdirp_dirname(char const *const path, int const mode) {
	ssize_t dlen = dirlen(path, strlen(path));
	if(dlen < 0) return (int)dlen;
	char *mutable = strndup(path, dlen);
	int const err = mkdirp_internal(mutable, dlen, mode);
	free(mutable); mutable = NULL;
	return err;
}
uv_file async_fs_open_mkdirp(const char* path, int flags, int mode) {
	uv_file file = async_fs_open(path, flags, mode);
	if(UV_ENOENT != file) return file;
	async_fs_mkdirp_dirname(path, 0700);
	return async_fs_open(path, flags, mode);
}
int async_fs_link_mkdirp(const char* path, const char* new_path) {
	int rc = async_fs_link(path, new_path);
	if(UV_ENOENT != rc) return rc;
	async_fs_mkdirp_dirname(new_path, 0700);
	return async_fs_link(path, new_path);
}

// TODO: Put this somewhere.
static char *tohex(char const *const buf, size_t const len) {
	char const map[] = "0123456789abcdef";
	char *const hex = calloc(len*2+1, 1);
	if(!hex) return NULL;
	for(size_t i = 0; i < len; ++i) {
		hex[i*2+0] = map[0xf & (buf[i] >> 4)];
		hex[i*2+1] = map[0xf & (buf[i] >> 0)];
	}
	return hex;
}
char *async_fs_tempnam(char const *dir, char const *prefix) {
	if(!dir) dir = "/tmp"; // TODO: Use ENV
	if(!prefix) prefix = "async";
	char rand[ENTROPY_BYTES];
	if(async_random((unsigned char *)rand, ENTROPY_BYTES) < 0) return NULL;
	char *hex = tohex(rand, ENTROPY_BYTES);
	char *path = aasprintf("%s/%s-%s", dir, prefix, hex);
	free(hex); hex = NULL;
	return path;
}

