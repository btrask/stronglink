// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <libgen.h> // basename(3)
#include <limits.h>
#include <signal.h>
#include <unistd.h> // Work around bad includes in libtls
#include <tls.h>
#include <async/http/HTTPServer.h>
#include "../util/fts.h"
#include "../util/raiserlimit.h"
#include "../StrongLink.h"
#include "Blog.h"
#include "RSSServer.h"

#define SERVER_ADDRESS NULL // NULL = public, "localhost" = private
#define SERVER_PORT_RAW 8000 // HTTP default 80, 0 for disabled
#define SERVER_PORT_TLS 0 // HTTPS default 443, 0 for disabled
#define SERVER_LOG_FILE NULL // stdout or NULL for disabled

int SLNServerDispatch(SLNRepoRef const repo, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers);

static strarg_t path = NULL;
static SLNRepoRef repo = NULL;
static RSSServerRef rss = NULL;
static BlogRef blog = NULL;
static HTTPServerRef server_raw = NULL;
static HTTPServerRef server_tls = NULL;
static uv_signal_t sigpipe[1] = {};
static uv_signal_t sigint[1] = {};
static int sig = 0;

static void listener(void *ctx, HTTPServerRef const server, HTTPConnectionRef const conn) {
	assert(server);
	assert(conn);
	HTTPMethod method = 99; // 0 is HTTP_DELETE...
	str_t URI[URI_MAX]; URI[0] = '\0';
	HTTPHeadersRef headers = NULL;
	SLNSessionRef session = NULL;
	ssize_t len = 0;
	int rc = 0;

	len = HTTPConnectionReadRequest(conn, &method, URI, sizeof(URI));
	if(UV_EOF == len) goto cleanup;
	if(UV_ECONNRESET == len) goto cleanup;
	if(len < 0) {
		rc = len;
		alogf("Request error: %s\n", uv_strerror(rc));
		goto cleanup;
	}

	rc = HTTPHeadersCreateFromConnection(conn, &headers);
	if(rc < 0) goto cleanup;

	strarg_t const host = HTTPHeadersGet(headers, "host");
	str_t domain[1023+1]; domain[0] = '\0';
	if(host) sscanf(host, "%1023[^:]", domain);
	// TODO: Verify Host header to prevent DNS rebinding.

	if(SERVER_PORT_TLS && server != server_tls) {
		rc = HTTPConnectionSendSecureRedirect(conn, domain, SERVER_PORT_TLS, URI);
		goto cleanup;
	}

	strarg_t const cookie = HTTPHeadersGet(headers, "cookie");
	SLNSessionCacheRef const cache = SLNRepoGetSessionCache(repo);
	rc = SLNSessionCacheCopyActiveSession(cache, cookie, &session);
	if(rc < 0) goto cleanup;
	// Note: null session is valid (zero permissions).

	rc = -1;
	rc = rc >= 0 ? rc : SLNServerDispatch(repo, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : RSSServerDispatch(rss, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : BlogDispatch(blog, session, conn, method, URI, headers);
	if(rc < 0) rc = 404;
	if(rc > 0) HTTPConnectionSendStatus(conn, rc);

cleanup:
	if(rc < 0) HTTPConnectionSendStatus(conn, HTTPError(rc));
	strarg_t const username = SLNSessionGetUsername(session);
	HTTPConnectionLog(conn, URI, username, headers, SERVER_LOG_FILE);
	SLNSessionRelease(&session);
	HTTPHeadersFree(&headers);
}

static void ignore(uv_signal_t *const signal, int const signum) {
	// Do nothing
}
static void stop(uv_signal_t *const signal, int const signum) {
	sig = signum;
	uv_stop(async_loop);
}

static int init_http(void) {
	if(!SERVER_PORT_RAW) return 0;
	HTTPServerRef server = NULL;
	int rc = HTTPServerCreate((HTTPListener)listener, blog, &server);
	if(rc < 0) goto cleanup;
	rc = HTTPServerListen(server, SERVER_ADDRESS, SERVER_PORT_RAW);
	if(rc < 0) goto cleanup;
	int const port = SERVER_PORT_RAW;
	alogf("StrongLink server running at http://localhost:%d/\n", port);
	server_raw = server; server = NULL;
cleanup:
	HTTPServerFree(&server);
	if(rc < 0) {
		alogf("HTTP server could not be started: %s\n", sln_strerror(rc));
		return -1;
	}
	return 0;
}
static int init_https(void) {
	if(!SERVER_PORT_TLS) return 0;
	HTTPServerRef server = NULL;
	str_t keypath[PATH_MAX];
	str_t crtpath[PATH_MAX];
	int rc;
	rc = snprintf(keypath, sizeof(keypath), "%s/key.pem", path);
	if(rc >= sizeof(keypath)) rc = UV_ENAMETOOLONG;
	if(rc < 0) goto cleanup;
	rc = snprintf(crtpath, sizeof(crtpath), "%s/crt.pem", path);
	if(rc >= sizeof(crtpath)) rc = UV_ENAMETOOLONG;
	if(rc < 0) goto cleanup;

	rc = HTTPServerCreate((HTTPListener)listener, blog, &server);
	if(rc < 0) goto cleanup;
	rc = HTTPServerListenSecurePaths(server, SERVER_ADDRESS, SERVER_PORT_TLS, keypath, crtpath);
	if(rc < 0) goto cleanup;
	int const port = SERVER_PORT_TLS;
	alogf("StrongLink server running at https://localhost:%d/\n", port);
	server_tls = server; server = NULL;
cleanup:
	HTTPServerFree(&server);
	if(rc < 0) {
		alogf("HTTPS server could not be started: %s\n", sln_strerror(rc));
		return -1;
	}
	return 0;
}
static void init(void *const unused) {
	int rc = async_random((byte_t *)&SLNSeed, sizeof(SLNSeed));
	if(rc < 0) {
		alogf("Random seed error\n");
		return;
	}

	uv_signal_init(async_loop, sigpipe);
	uv_signal_start(sigpipe, ignore, SIGPIPE);
	uv_unref((uv_handle_t *)sigpipe);

	str_t *tmp = strdup(path);
	strarg_t const reponame = basename(tmp); // TODO
	rc = SLNRepoCreate(path, reponame, &repo);
	FREE(&tmp);
	if(rc < 0) {
		alogf("Repository could not be opened: %s\n", sln_strerror(rc));
		return;
	}
	blog = BlogCreate(repo);
	if(!blog) {
		alogf("Blog server could not be initialized\n");
		return;
	}
	rc = RSSServerCreate(repo, &rss);
	if(rc < 0) {
		alogf("RSS server error: %s\n", sln_strerror(rc));
		return;
	}

	if(init_http() < 0 || init_https() < 0) {
		HTTPServerClose(server_raw);
		HTTPServerClose(server_tls);
		return;
	}

//	SLNRepoPullsStart(repo);

	uv_signal_init(async_loop, sigint);
	uv_signal_start(sigint, stop, SIGINT);
	uv_unref((uv_handle_t *)sigint);
}
static void term(void *const unused) {
	fprintf(stderr, "\n");
	alogf("Stopping StrongLink server...\n");

	uv_ref((uv_handle_t *)sigint);
	uv_signal_stop(sigint);
	async_close((uv_handle_t *)sigint);

	SLNRepoPullsStop(repo);
	HTTPServerClose(server_raw);
	HTTPServerClose(server_tls);

	async_pool_enter(NULL);
	fflush(NULL); // Everything.
	async_pool_leave(NULL);

	uv_ref((uv_handle_t *)sigpipe);
	uv_signal_stop(sigpipe);
	uv_close((uv_handle_t *)sigpipe, NULL);
}
static void cleanup(void *const unused) {
	HTTPServerFree(&server_raw);
	HTTPServerFree(&server_tls);
	RSSServerFree(&rss);
	BlogFree(&blog);
	SLNRepoFree(&repo);

	async_pool_enter(NULL);
	fflush(NULL); // Everything.
	async_pool_leave(NULL);

	async_pool_destroy_shared();
}

int main(int const argc, char const *const *const argv) {
	// Depending on how async_pool and async_fs are configured, we might be
	// using our own thread pool heavily or not. However, at the minimum,
	// uv_getaddrinfo uses the libuv thread pool, and it blocks on the
	// network, so don't set this number too low.
	if(!getenv("UV_THREADPOOL_SIZE")) putenv((char *)"UV_THREADPOOL_SIZE=4");

	raiserlimit();
	async_init();

	int rc = tls_init();
	if(rc < 0) {
		fprintf(stderr, "TLS initialization error: %s\n", strerror(errno));
		return 1;
	}

	if(2 != argc || '-' == argv[1][0]) {
		fprintf(stderr, "Usage:\n\t" "%s repo\n", argv[0]);
		return 1;
	}
	path = argv[1];

	// Even our init code wants to use async I/O.
	async_spawn(STACK_DEFAULT, init, NULL);
	uv_run(async_loop, UV_RUN_DEFAULT);

	async_spawn(STACK_DEFAULT, term, NULL);
	uv_run(async_loop, UV_RUN_DEFAULT);

#ifdef SLN_USE_VALGRIND
	// Note: With Valgrind disabled, libasync unrefs HTTP connections
	// while blocking, which lets us terminate promptly. However, those
	// unref'd connections don't actually get closed or freed, so
	// freeing other structures can cause problems. Instead, we just
	// let the process die and the OS clean everything up.
	// 
	// With Valgrind enabled, we wait for all pending connections to close
	// and then free everything properly.
	// 
	// The correct solution here is probably to keep a list of all
	// active connections, and tell them to terminate when we want to quit.

	async_spawn(STACK_DEFAULT, cleanup, NULL);
	uv_run(async_loop, UV_RUN_DEFAULT);

	async_destroy();
#endif

	// TODO: Windows?
	if(sig) raise(sig);

	return 0;
}
