
// TODO: shared way to import standard headers?
// obviously could just have #include "SLNConverter.h"


// unfortunately we cant automatically cap size for all converters
// or maybe we can, at like 10mb instead of 1mb?
// our conversion system is not designed to convert 2gb moves for half an hour
// but some "larger" files might be legitimate for some converters


// well hang on here
// our converters are expected to synthesize meta-files too
// and they probably have to run during submission, not during preview
// we could return 202 Accepted and then do further processing...


// if we don't generate a preview, does that also mean we don't generate a meta-file?
// should we create the yajl document or let the client do that?
// if we create it that saves boilerplate and they don't need the target uri

// it'd be a good idea to wrap yajl with something we control...
// maybe not for now though


// NOTE: The mapped buffer must be nul-terminated
// on linux this is simple enough by passing len+1 to mmap?

// ugh... why does regexec use nul-terminated strings?


// how to handle type detection?



// TODO
// dont create the meta-file before we know a given converter will work
// need to have sln_types_* function too


// What type is the converter?

// does conversion really need to be two separate steps?
// cant we just call the converter and let it decide whether it supports the type or not?

// the problem is, we kind of want to create the submission after the type is checked
// otherwise, its hard to know we can reuse it and we probably have to throw it out each time
// plus creating the sub in the converter means more duplication




#include <regex.h>
#include "../async/async.h"

#define LIMIT_DEFAULT (1024 * 1024 * 1)
#define LIMIT_SMALL (1024 * 16)
#define LIMIT_LARGE (1024 * 1024 * 10)

#define CONVERTER \
	int blog_convert_##NAME(uv_file const html, \
	                       yajl_gen const meta, \
	                       char const *const buf, \
	                       size_t const size, \
	                       char const *const type)

#define TYPE_FUNC \
	int blog_types_##NAME(char const *const type)
#define TYPE_LIST(types) \
	TYPE_FUNC { \
		static char const *const t[] = { types }; \
		for(size_t i = 0; i < numberof(t); i++) { \
			if(0 == strcasecmp(t[i], type)) return 0; \
		} \
		return -1; \
	}


#define STR_LEN(x) (x), (sizeof(x)-1)
#define uv_buf_lit(str) uv_buf_init(STR_LEN(str))
#define RETRY(x) ({ ssize_t __x; do __x = (x); while(-1 == __x && EINTR == errno); __x; })



// <http://daringfireball.net/2010/07/improved_regex_for_matching_urls>
// Painstakingly ported to POSIX
#define LINKIFY_RE "([a-z][a-z0-9_-]+:(/{1,3}|[a-z0-9%])|www[0-9]{0,3}[.]|[a-z0-9.-]+[.][a-z]{2,4}/)([^[:space:]()<>]+|\\(([^[:space:]()<>]+|(\\([^[:space:]()<>]+\\)))*\\))+(\\(([^[:space:]()<>]+|(\\([^[:space:]()<>]+\\)))*\\)|[^][[:space:]`!(){};:'\".,<>?«»“”‘’])"

static int write_html(uv_file const file, char const *const buf, size_t const len) {
	uv_buf_t x = uv_buf_init(buf, len);
	return async_fs_writeall(file, &x, 1, -1);
}
static int write_text(uv_file const file, char const *const buf, size_t const len) {
	uv_buf_t chunks[30];
	size_t x = 0;
	size_t y = SIZE_MAX;
	int rc;
	for(size_t i = 0; i < len; i++) {
		char const *rep = NULL;
		switch(buf[i]) {
			case '<': chunks[x++] = uv_buf_lit("&lt;"); break;
			case '>': chunks[x++] = uv_buf_lit("&gt;"); break;
			case '&': chunks[x++] = uv_buf_lit("&amp;"); break;
			case '"': chunks[x++] = uv_buf_lit("&quot;"); break;
			case '\'': chunks[x++] = uv_buf_lit("&apos;"); break;
			default:
				if(y != x) chunks[x++] = uv_buf_init(buf+i, 1);
				else chunks[x].len++;
				y = x;
				break;
		}
		if(x >= numberof(chunks)) {
			rc = async_fs_writeall(file, chunks, x, -1);
			if(rc < 0) return rc;
		}
	}
	rc = async_fs_writeall(file, chunks, x, -1);
	if(rc < 0) return rc;
	return 0;
}


#define NAME plaintext
TYPE_LIST(
	"text/plain; charset=utf-8",
	"text/plain")
CONVERTER {
	if(size > LIMIT_DEFAULT) return UV_EFBIG;

	yajl_gen_string(json, (unsigned char const *)STR_LEN("fulltext"));
	yajl_gen_string(json, (unsigned char const *)buf, len);

	yajl_gen_string(json, (unsigned char const *)STR_LEN("link"));
	yajl_gen_array_open(json);

	regex_t linkify[1];
	int rc = regcomp(linkify, LINKIFY_RE, REG_ICASE | REG_EXTENDED);
	assert(0 == rc);

	rc = write_html(html, STR_LEN("<pre>"));
	if(rc < 0) return rc;

	char const *pos = buf;
	regmatch_t match;
	while(0 == regexec(linkify, pos, 1, &match, 0)) {
		regoff_t const loc = match.rm_so;
		regoff_t const len = match.rm_eo - match.rm_so;

		rc = write_text(html, pos, len-(pos-buf));
		if(rc < 0) return rc;
		rc = write_html(html, STR_LEN("<a href=\""));
		if(rc < 0) return rc;
		rc = write_text(html, pos+loc, len);
		if(rc < 0) return rc;
		rc = write_html(html, STR_LEN("\">"));
		if(rc < 0) return rc;
		rc = write_text(html, pos+loc, len);
		if(rc < 0) return rc;
		rc = write_html(html, STR_LEN("</a>"));
		if(rc < 0) return rc;

		yajl_gen_string(json, (unsigned char const *)pos+loc, len);

		pos += loc+len;
	}
	rc = write_text(html, pos, size-(pos-buf));
	if(rc < 0) return rc;
	rc = write_html(html, STR_LEN("</pre>"));
	if(rc < 0) return rc;

	regfree(linkify);

	yajl_gen_array_close(json);

	return 0;
}



#include <sys/mman.h>
#include <yajl/yajl_gen.h>
#include "../async/async.h"

typedef int (*BlogTypeCheck)(strarg_t const type);
typedef int (*BlogConverter)(
	uv_file const html,
	yajl_gen const json,
	char const *const buf,
	size_t const size,
	char const *const type);

#define CONVERTER(name) blog_types_##name, blog_convert_##name

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
	uv_file src = -1;
	char const *buf = NULL;
	SLNSubmissionRef meta = NULL;
	yajl_gen json = NULL;

	tmp = SLNRepoCopyTempPath(blog->repo);
	if(!tmp) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	rc = async_fs_open(tmp, O_CREAT | O_EXCL | O_WRONLY, 0400);
	if(UV_ENOENT == rc) {
		async_fs_mkdirp_dirname(tmp, 0700);
		rc = async_fs_open(tmp, O_CREAT | O_EXCL | O_WRONLY, 0400);
	}
	if(rc < 0) goto cleanup;
	html = rc;

	rc = async_fs_open(srcpath, O_RDONLY, 0000);
	if(rc < 0) goto cleanup;
	src = rc;

	// We use size+1 to get nul-termination. Kind of a hack.
	buf = mmap(NULL, src->size+1, PROT_READ, MAP_SHARED, src, 0)
	if(MAP_FAILED == buf) rc = -errno;
	if(rc < 0) goto cleanup;

	async_fs_close(src); src = -1;

	char const *const metatype = "text/efs-meta+json; charset=utf-8";
	meta = SLNSubmissionCreate(session, metatype);
	if(!meta) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	SLNSubmissionWrite(meta, (byte_t const *)URI, strlen(URI));
	SLNSubmissionWrite(meta, (byte_t const *)STR_LEN("\n\n"));

	json = yajl_gen_alloc(NULL);
	if(!json) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;
	yajl_gen_config(json, yajl_gen_print_callback, (void (*)())SLNSubmissionWrite, meta);
	yajl_gen_config(json, yajl_gen_beautify, (int)true);

	yajl_gen_map_open(json);
	yajl_gen_string(json, (unsigned char const *)STR_LEN("type"));
	yajl_gen_string(json, (unsigned char const *)src->type, strlen(src->type));

	
	async_pool_enter(NULL);
	rc = converter(html, json, buf, src->size, src->type);
	async_pool_leave(NULL);
	if(rc < 0) goto cleanup;


	yajl_gen_map_close(json);

	rc = async_fs_fdatasync(html);
	if(rc < 0) goto cleanup;

	rc = async_fs_link(tmp, htmlpath);
	if(UV_ENOENT == rc) {
		async_fs_mkdirp_dirname(htmlpath, 0700);
		rc = async_fs_link(tmp, htmlpath);
	}
	if(rc < 0) goto cleanup;

	rc = SLNSubmissionEnd(meta);
	if(rc < 0) goto cleanup;

	*outmeta = meta; meta = NULL;

cleanup:
	async_fs_unlink(tmp); FREE(&tmp);
	if(html >= 0) { async_fs_close(html); html = -1; }
	if(src >= 0) { async_fs_close(src); src = -1; }
	if(buf) { munmap(buf, src->size+1); buf = NULL; }
	SLNSubmissionFree(&meta);
	if(json) { yajl_gen_free(json); json = NULL; }
	return rc;
}
static int generic(BlogRef const blog,
                   SLNSessionRef const session,
                   strarg_t const htmlpath,
                   SLNSubmissionRef *const outmeta,
                   strarg_t const URI,
                   SLNFileInfo const *const src)
{
	str_t *tmp = NULL;
	uv_file html = -1;
	int rc = 0;
	
	tmp = SLNRepoCopyTempPath(blog->repo);
	if(!tmp) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	rc = async_fs_open(tmp, O_CREAT | O_EXCL | O_WRONLY, 0400);
	if(UV_ENOENT == rc) {
		async_fs_mkdirp_dirname(tmp, 0700);
		rc = async_fs_open(tmp, O_CREAT | O_EXCL | O_WRONLY, 0400);
	}
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

	rc = async_fs_link(tmp, htmlpath);
	if(UV_ENOENT == rc) {
		async_fs_mkdirp_dirname(htmlpath, 0700);
		rc = async_fs_link(tmp, htmlpath);
	}
	if(rc < 0) goto cleanup;

	*outmeta = NULL;

cleanup:
	async_fs_unlink(tmp); FREE(&tmp);
	if(html >= 0) { async_fs_close(html); html = -1; }
	return rc;
}
int BlogConvert(BlogRef const blog,
                SLNSessionRef const session,
                strarg_t const URI,
                strarg_t const html,
                SLNSubmissionRef *const meta)
{
	SLNFileInfo src[1];
	int rc = SLNSessionGetFileInfo(session, URI, src);
	if(rc < 0) return rc;

	rc=-1;
	rc=rc>=0?rc: convert(blog, session, html, meta, URI, src, CONVERTER(markdown));
	rc=rc>=0?rc: convert(blog, session, html, meta, URI, src, CONVERTER(plaintext));
	rc=rc>=0?rc: generic(blog, session, html, meta, URI, src);

	SLNFileInfoCleanup(src);
	return rc;
}









