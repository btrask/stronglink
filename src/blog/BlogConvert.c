// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <sys/mman.h>
#include <yajl/yajl_gen.h>
#include "Blog.h"

typedef int (*BlogTypeCheck)(strarg_t const type);
typedef int (*BlogConverter)(
	uv_file const html,
	yajl_gen const json,
	char const *const buf,
	size_t const size,
	char const *const type);

// TODO: We need a real plugin system with dynamic loading, etc.
#define CONVERTER(name) blog_types_##name, blog_convert_##name

int blog_types_markdown(strarg_t const type);
int blog_convert_markdown(
	uv_file const html,
	yajl_gen const json,
	char const *const buf,
	size_t const size,
	char const *const type);

int blog_types_plaintext(strarg_t const type);
int blog_convert_plaintext(
	uv_file const html,
	yajl_gen const json,
	char const *const buf,
	size_t const size,
	char const *const type);

static int convert(BlogRef const blog,
                   SLNSessionRef const session,
                   char const *const htmlpath,
                   SLNSubmissionRef *const outmeta,
                   strarg_t const URI,
                   SLNFileInfo const *const src,
                   BlogTypeCheck const types,
                   BlogConverter const converter)
{
	int rc = types(src->type);
	if(rc < 0) return UV_EINVAL;

	str_t *tmp = NULL;
	uv_file html = -1;
	uv_file file = -1;
	char const *buf = NULL;
	SLNSubmissionRef meta = NULL;
	yajl_gen json = NULL;

	tmp = SLNRepoCopyTempPath(blog->repo);
	if(!tmp) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	rc = async_fs_open_mkdirp(tmp, O_CREAT | O_EXCL | O_WRONLY, 0400);
	if(rc < 0) goto cleanup;
	html = rc;

	rc = async_fs_open(src->path, O_RDONLY, 0000);
	if(rc < 0) goto cleanup;
	file = rc;

	// We use size+1 to get nul-termination. Kind of a hack.
	buf = mmap(NULL, src->size+1, PROT_READ, MAP_SHARED, file, 0);
	if(MAP_FAILED == buf) rc = -errno;
	if(rc < 0) goto cleanup;
	if('\0' != buf[src->size]) rc = UV_EIO; // Slightly paranoid.
	if(rc < 0) goto cleanup;

	async_fs_close(file); file = -1;

	if(outmeta) {
		rc = SLNSubmissionCreate(session, NULL, SLN_META_TYPE, &meta);
		if(rc < 0) goto cleanup;
	}

	SLNSubmissionWrite(meta, (byte_t const *)URI, strlen(URI));
	SLNSubmissionWrite(meta, (byte_t const *)STR_LEN("\n\n"));

	json = yajl_gen_alloc(NULL);
	if(!json) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;
	yajl_gen_config(json, yajl_gen_print_callback, (void (*)())SLNSubmissionWrite, meta);
	yajl_gen_config(json, yajl_gen_beautify, (int)true);

	async_pool_enter(NULL);
	yajl_gen_map_open(json);
	rc = converter(html, json, buf, src->size, src->type);
	yajl_gen_map_close(json);
	async_pool_leave(NULL);
	if(rc < 0) goto cleanup;

	rc = async_fs_fdatasync(html);
	if(rc < 0) goto cleanup;

	rc = async_fs_link_mkdirp(tmp, htmlpath);
	if(rc < 0) goto cleanup;

	rc = SLNSubmissionEnd(meta);
	if(rc < 0) goto cleanup;

	if(outmeta) {
		*outmeta = meta; meta = NULL;
	}

cleanup:
	async_fs_unlink(tmp); FREE(&tmp);
	if(html >= 0) { async_fs_close(html); html = -1; }
	if(file >= 0) { async_fs_close(file); file = -1; }
	if(buf) { munmap((void *)buf, src->size+1); buf = NULL; }
	if(json) { yajl_gen_free(json); json = NULL; }
	SLNSubmissionFree(&meta);
	assert(html < 0);
	assert(file < 0);
	return rc;
}
int BlogConvert(BlogRef const blog,
                SLNSessionRef const session,
                strarg_t const html,
                SLNSubmissionRef *const outmeta,
                strarg_t const URI,
                SLNFileInfo const *const src)
{
	int rc = -1;
	rc=rc>=0?rc: convert(blog, session, html, outmeta, URI, src, CONVERTER(markdown));
	rc=rc>=0?rc: convert(blog, session, html, outmeta, URI, src, CONVERTER(plaintext));
	return rc;
}
int BlogGeneric(BlogRef const blog,
                SLNSessionRef const session,
                strarg_t const htmlpath,
                strarg_t const URI,
                SLNFileInfo const *const src)
{
	str_t *tmp = NULL;
	uv_file html = -1;
	int rc = 0;

	tmp = SLNRepoCopyTempPath(blog->repo);
	if(!tmp) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	rc = async_fs_open_mkdirp(tmp, O_CREAT | O_EXCL | O_WRONLY, 0400);
	if(rc < 0) goto cleanup;
	html = rc;

	preview_state const state = {
		.blog = blog,
		.session = session,
		.fileURI = URI,
	};
	rc = TemplateWriteFile(blog->preview, &preview_cbs, &state, html);
	if(rc < 0) goto cleanup;

	rc = async_fs_fdatasync(html);
	if(rc < 0) goto cleanup;

	rc = async_fs_link_mkdirp(tmp, htmlpath);
	if(rc < 0) goto cleanup;

cleanup:
	async_fs_unlink(tmp); FREE(&tmp);
	if(html >= 0) { async_fs_close(html); html = -1; }
	assert(html < 0);
	return rc;
}



// TODO
static str_t *preview_metadata(preview_state const *const state, strarg_t const var) {
	int rc;
	strarg_t unsafe = NULL;
	str_t buf[URI_MAX];
	if(0 == strcmp(var, "rawURI")) {
		str_t algo[SLN_ALGO_SIZE]; // SLN_INTERNAL_ALGO
		str_t hash[SLN_HASH_SIZE];
		SLNParseURI(state->fileURI, algo, hash);
		snprintf(buf, sizeof(buf), "/sln/file/%s/%s", algo, hash);
		unsafe = buf;
	}
	if(0 == strcmp(var, "queryURI")) {
		str_t *escaped = QSEscape(state->fileURI, strlen(state->fileURI), true);
		snprintf(buf, sizeof(buf), "/?q=%s", escaped);
		FREE(&escaped);
		unsafe = buf;
	}
	if(0 == strcmp(var, "hashURI")) {
		unsafe = state->fileURI;
	}
	if(0 == strcmp(var, "fileSize")) {
		// TODO: Really, we should already have this info from when
		// we got the URI in the first place.
		SLNFileInfo info[1];
		rc = SLNSessionGetFileInfo(state->session, state->fileURI, info);
		if(rc >= 0) {
			double const size = info->size;
			double base = 1.0;
			strarg_t const units[] = { "B", "KB", "MB", "GB", "TB" };
			size_t i = 0;
			for(; base * 1024.0 <= size && i < numberof(units); i++) base *= 1024.0;
			strarg_t const fmt = (0 == i ? "%.0f %s" : "%.1f %s");
			snprintf(buf, sizeof(buf), fmt, size/base, units[i]);
			unsafe = buf;
			// P.S. Fuck scientific prefixes.
		}
		SLNFileInfoCleanup(info);
	}
	if(unsafe) return htmlenc(unsafe);

	str_t value[1024 * 4];
	rc = SLNSessionGetValueForField(state->session, value, sizeof(value), state->fileURI, var);
	if(rc >= 0 && '\0' != value[0]) unsafe = value;

	if(!unsafe) {
		if(0 == strcmp(var, "thumbnailURI")) unsafe = "/file.png";
		if(0 == strcmp(var, "title")) unsafe = "(no title)";
		if(0 == strcmp(var, "description")) unsafe = "(no description)";
	}

	return htmlenc(unsafe);
}
static void preview_free(preview_state const *const state, strarg_t const var, str_t **const val) {
	FREE(val);
}
TemplateArgCBs const preview_cbs = {
	.lookup = (str_t *(*)())preview_metadata,
	.free = (void (*)())preview_free,
};

