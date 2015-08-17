// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <libgen.h> /* basename(3) */
#include <limits.h>
#include <signal.h>
#include <tls.h>
#include "../util/fts.h"
#include "../util/raiserlimit.h"
#include "../http/HTTPServer.h"
#include "../StrongLink.h"
#include "Blog.h"

#define SERVER_ADDRESS "localhost" // NULL = public, "localhost" = private
#define SERVER_PORT_RAW "8000" // HTTP default 80
#define SERVER_PORT_TLS "8001" // HTTPS default 443

int SLNServerDispatch(SLNRepoRef const repo, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers);

static strarg_t path = NULL;
static SLNRepoRef repo = NULL;
static BlogRef blog = NULL;
static HTTPServerRef server_raw = NULL;
static HTTPServerRef server_tls = NULL;
static uv_signal_t sigpipe[1] = {};
static uv_signal_t sigint[1] = {};
static int sig = 0;

static int listener0(void *ctx, HTTPServerRef const server, HTTPConnectionRef const conn) {
	HTTPMethod method;
	str_t URI[URI_MAX];
	ssize_t len = HTTPConnectionReadRequest(conn, &method, URI, sizeof(URI));
	if(UV_EOF == len) return 0;
	if(UV_EMSGSIZE == len) return 414; // Request-URI Too Large
	if(len < 0) {
		fprintf(stderr, "Request error %s\n", uv_strerror(len));
		return 500;
	}

	HTTPHeadersRef headers;
	int rc = HTTPHeadersCreateFromConnection(conn, &headers);
	if(UV_EMSGSIZE == rc) return 431; // Request Header Fields Too Large
	if(rc < 0) return 500;

	strarg_t const host = HTTPHeadersGet(headers, "host");
	str_t domain[1023+1]; domain[0] = '\0';
	if(host) sscanf(host, "%1023[^:]", domain);
	// TODO: Verify Host header to prevent DNS rebinding.

	if(server == server_raw && server_tls) {
		// Redirect from HTTP to HTTPS
		if('\0' == domain[0]) return 400;
		strarg_t const port = SERVER_PORT_TLS;
		str_t loc[URI_MAX];
		rc = snprintf(loc, sizeof(loc), "https://%s:%s/", domain, port);
		if(rc >= sizeof(loc)) 414; // Request-URI Too Large
		if(rc < 0) return 500;
		HTTPConnectionSendRedirect(conn, 301, loc);
		// TODO: HSTS
		return 0;
	}

	strarg_t const cookie = HTTPHeadersGet(headers, "cookie");
	SLNSessionCacheRef const cache = SLNRepoGetSessionCache(repo);
	SLNSessionRef session = NULL;
	rc = SLNSessionCacheCopyActiveSession(cache, cookie, &session);
	if(rc < 0) return 500;
	// Note: null session is valid (zero permissions).

	rc = -1;
	rc = rc >= 0 ? rc : SLNServerDispatch(repo, session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : BlogDispatch(blog, session, conn, method, URI, headers);

	SLNSessionRelease(&session);
	HTTPHeadersFree(&headers);
	return rc;
}
static void listener(void *ctx, HTTPServerRef const server, HTTPConnectionRef const conn) {
	assert(server);
	assert(conn);
	int rc = listener0(ctx, server, conn);
	if(rc < 0) rc = 404;
	if(rc > 0) HTTPConnectionSendStatus(conn, rc);
}

static void ignore(uv_signal_t *const signal, int const signum) {
	// Do nothing
}
static void stop(uv_signal_t *const signal, int const signum) {
	sig = signum;
	uv_stop(async_loop);
}

static void init(void *const unused) {
	int rc;
	async_random((byte_t *)&SLNSeed, sizeof(SLNSeed));

	uv_signal_init(async_loop, sigpipe);
	uv_signal_start(sigpipe, ignore, SIGPIPE);
	uv_unref((uv_handle_t *)sigpipe);

	str_t *tmp = strdup(path);
	strarg_t const reponame = basename(tmp); // TODO
	repo = SLNRepoCreate(path, reponame);
	FREE(&tmp);
	if(!repo) {
		fprintf(stderr, "Repository could not be opened\n");
		return;
	}
	blog = BlogCreate(repo);
	if(!blog) {
		fprintf(stderr, "Blog server could not be initialized\n");
		return;
	}

	if(SERVER_PORT_RAW) {
		server_raw = HTTPServerCreate((HTTPListener)listener, blog);
		if(!server_raw) {
			fprintf(stderr, "Web server could not be initialized\n");
			return;
		}
		rc = HTTPServerListen(server_raw, SERVER_ADDRESS, SERVER_PORT_RAW);
		if(rc < 0) {
			fprintf(stderr, "Unable to start server (%d, %s)\n", rc, sln_strerror(rc));
			return;
		}
		strarg_t const port = SERVER_PORT_RAW;
		fprintf(stderr, "StrongLink server running at http://localhost:%s/\n", port);
	}
	if(SERVER_PORT_TLS) {
		struct tls_config *config = tls_config_new();
		if(!config) {
			fprintf(stderr, "Couldn't create TLS configuration\n");
			return;
		}
		str_t pemfile[PATH_MAX];
		snprintf(pemfile, sizeof(pemfile), "%s/key.pem", path);
		rc = tls_config_set_key_file(config, pemfile);
		if(0 != rc) {
			fprintf(stderr, "Couldn't load key.pem for TLS\n");
			tls_config_free(config); config = NULL;
			return;
		}
		snprintf(pemfile, sizeof(pemfile), "%s/crt.pem", path);
		rc = tls_config_set_cert_file(config, pemfile);
		if(0 != rc) {
			fprintf(stderr, "Couldn't load crt.pem for TLS\n");
			tls_config_free(config); config = NULL;
			return;
		}
		server_tls = HTTPServerCreate((HTTPListener)listener, blog);
		if(!server_tls) {
			fprintf(stderr, "Web server could not be initialized\n");
			tls_config_free(config); config = NULL;
			return;
		}
		rc = HTTPServerListenSecure(server_tls, SERVER_ADDRESS, SERVER_PORT_TLS, config);
		if(rc < 0) {
			fprintf(stderr, "Unable to start server (%d, %s)\n", rc, sln_strerror(rc));
			tls_config_free(config); config = NULL;
			return;
		}
		strarg_t const port = SERVER_PORT_TLS;
		fprintf(stderr, "StrongLink server running at https://localhost:%s/\n", port);
		tls_config_free(config); config = NULL;
	}

//	SLNRepoPullsStart(repo);

	uv_signal_init(async_loop, sigint);
	uv_signal_start(sigint, stop, SIGINT);
	uv_unref((uv_handle_t *)sigint);
}
static void term(void *const unused) {
	fprintf(stderr, "\nStopping StrongLink server...\n");

	uv_ref((uv_handle_t *)sigint);
	uv_signal_stop(sigint);
	async_close((uv_handle_t *)sigint);

	SLNRepoPullsStop(repo);
	HTTPServerClose(server_raw);
	HTTPServerClose(server_tls);

	uv_ref((uv_handle_t *)sigpipe);
	uv_signal_stop(sigpipe);
	uv_close((uv_handle_t *)sigpipe, NULL);
}
static void cleanup(void *const unused) {
	HTTPServerFree(&server_raw);
	HTTPServerFree(&server_tls);
	BlogFree(&blog);
	SLNRepoFree(&repo);

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
		fprintf(stderr, "TLS initialization error\n");
		return 1;
	}

	// TODO: Real option parsing.
	if(2 != argc || '-' == argv[1][0]) {
		fprintf(stderr, "Usage:\n\t" "%s <repo>\n", argv[0]);
		return 1;
	}
	path = argv[1];

	// Even our init code wants to use async I/O.
	async_spawn(STACK_DEFAULT, init, NULL);
	uv_run(async_loop, UV_RUN_DEFAULT);

	async_spawn(STACK_DEFAULT, term, NULL);
	uv_run(async_loop, UV_RUN_DEFAULT);

	// cleanup is separate from term because connections might
	// still be active.
	async_spawn(STACK_DEFAULT, cleanup, NULL);
	uv_run(async_loop, UV_RUN_DEFAULT);

	async_destroy();

	// TODO: Windows?
	if(sig) raise(sig);

	return 0;
}
