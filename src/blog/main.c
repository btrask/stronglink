// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <libgen.h> // basename(3)
#include <limits.h>
#include <signal.h>
#include <unistd.h> // Work around bad includes in libtls
#include <tls.h>
#include "../util/fts.h"
#include "../util/raiserlimit.h"
#include "../http/HTTPServer.h"
#include "../StrongLink.h"
#include "Blog.h"
#include "RSSServer.h"

#define SERVER_ADDRESS NULL // NULL = public, "localhost" = private
#define SERVER_PORT_RAW 8000 // HTTP default 80, 0 for disabled
#define SERVER_PORT_TLS 0 // HTTPS default 443, 0 for disabled
#define SERVER_LOG_FILE NULL // stdout or NULL for disabled

// https://wiki.mozilla.org/Security/Server_Side_TLS
// https://wiki.mozilla.org/index.php?title=Security/Server_Side_TLS&oldid=1080944
// "Modern" compatibility ciphersuite
#define TLS_CIPHERS "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:kEDH+AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA:DHE-RSA-AES256-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!3DES:!MD5:!PSK"

// According to SSL Labs, enabling TLS1.1 doesn't do any good...
// Not 100% sure about its status in IE11 though.
#define TLS_PROTOCOLS (TLS_PROTOCOL_TLSv1_2)

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

static int listener0(HTTPServerRef const server, HTTPConnectionRef const conn, str_t *const URI, size_t const URIMax, HTTPHeadersRef *const outheaders, SLNSessionRef *const outsession) {
	HTTPMethod method;
	ssize_t len = HTTPConnectionReadRequest(conn, &method, URI, URIMax);
	if(UV_EOF == len) return 0;
	if(UV_ECONNRESET == len) return 0;
	if(UV_EMSGSIZE == len) return 414; // Request-URI Too Large
	if(len < 0) {
		alogf("Request error: %s\n", uv_strerror(len));
		return 500;
	}

	int rc = HTTPHeadersCreateFromConnection(conn, outheaders);
	if(UV_EMSGSIZE == rc) return 431; // Request Header Fields Too Large
	if(rc < 0) return 500;

	strarg_t const host = HTTPHeadersGet(*outheaders, "host");
	str_t domain[1023+1]; domain[0] = '\0';
	if(host) sscanf(host, "%1023[^:]", domain);
	// TODO: Verify Host header to prevent DNS rebinding.

	if(SERVER_PORT_TLS && server == server_raw) {
		// Redirect from HTTP to HTTPS
		if('\0' == domain[0]) return 400;
		int const port = SERVER_PORT_TLS;
		str_t loc[URI_MAX];
		rc = snprintf(loc, sizeof(loc), "https://%s:%d%s", domain, port, URI);
		if(rc >= sizeof(loc)) return 414; // Request-URI Too Large
		if(rc < 0) return 500;
		HTTPConnectionSendRedirect(conn, 301, loc);
		return 0;
	}

	strarg_t const cookie = HTTPHeadersGet(*outheaders, "cookie");
	SLNSessionCacheRef const cache = SLNRepoGetSessionCache(repo);
	rc = SLNSessionCacheCopyActiveSession(cache, cookie, outsession);
	if(rc < 0) return 500;
	// Note: null session is valid (zero permissions).

	rc = -1;
	rc = rc >= 0 ? rc : SLNServerDispatch(repo, *outsession, conn, method, URI, *outheaders);
	rc = rc >= 0 ? rc : RSSServerDispatch(rss, *outsession, conn, method, URI, *outheaders);
	rc = rc >= 0 ? rc : BlogDispatch(blog, *outsession, conn, method, URI, *outheaders);
	return rc;
}
static void listener(void *ctx, HTTPServerRef const server, HTTPConnectionRef const conn) {
	assert(server);
	assert(conn);
	str_t URI[URI_MAX]; URI[0] = '\0';
	HTTPHeadersRef headers = NULL;
	SLNSessionRef session = NULL;
	int rc = listener0(server, conn, URI, sizeof(URI), &headers, &session);
	if(rc < 0) rc = 404;
	if(rc > 0) HTTPConnectionSendStatus(conn, rc);
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
	server_raw = HTTPServerCreate((HTTPListener)listener, blog);
	if(!server_raw) {
		alogf("HTTP server could not be initialized\n");
		return -1;
	}
	int rc = HTTPServerListen(server_raw, SERVER_ADDRESS, SERVER_PORT_RAW);
	if(rc < 0) {
		alogf("HTTP server could not be started: %s\n", sln_strerror(rc));
		return -1;
	}
	int const port = SERVER_PORT_RAW;
	alogf("StrongLink server running at http://localhost:%d/\n", port);
	return 0;
}
static int init_https(void) {
	if(!SERVER_PORT_TLS) return 0;
	struct tls_config *config = tls_config_new();
	if(!config) {
		alogf("TLS config error: %s\n", strerror(errno));
		return -1;
	}
	int rc = tls_config_set_ciphers(config, TLS_CIPHERS);
	if(0 != rc) {
		alogf("TLS ciphers error: %s\n", strerror(errno));
		tls_config_free(config); config = NULL;
		return -1;
	}
	tls_config_set_protocols(config, TLS_PROTOCOLS);
	str_t pemfile[PATH_MAX];
	snprintf(pemfile, sizeof(pemfile), "%s/key.pem", path);
	rc = tls_config_set_key_file(config, pemfile);
	if(0 != rc) {
		alogf("TLS key file error: %s\n", strerror(errno));
		tls_config_free(config); config = NULL;
		return -1;
	}
	snprintf(pemfile, sizeof(pemfile), "%s/crt.pem", path);
	rc = tls_config_set_cert_file(config, pemfile);
	if(0 != rc) {
		alogf("TLS crt file error: %s\n", strerror(errno));
		tls_config_free(config); config = NULL;
		return -1;
	}
	struct tls *tls = tls_server();
	if(!tls) {
		alogf("TLS engine error: %s\n", strerror(errno));
		tls_config_free(config); config = NULL;
		return -1;
	}
	rc = tls_configure(tls, config);
	tls_config_free(config); config = NULL;
	if(0 != rc) {
		alogf("TLS config error: %s\n", tls_error(tls));
		tls_free(tls); tls = NULL;
		return -1;
	}
	server_tls = HTTPServerCreate((HTTPListener)listener, blog);
	if(!server_tls) {
		alogf("HTTPS server could not be initialized\n");
		tls_free(tls); tls = NULL;
		return -1;
	}
	rc = HTTPServerListenSecure(server_tls, SERVER_ADDRESS, SERVER_PORT_TLS, &tls);
	tls_free(tls); tls = NULL;
	if(rc < 0) {
		alogf("HTTPS server could not be started: %s\n", sln_strerror(rc));
		return -1;
	}
	int const port = SERVER_PORT_TLS;
	alogf("StrongLink server running at https://localhost:%d/\n", port);
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

	// cleanup is separate from term because connections might
	// still be active.
	async_spawn(STACK_DEFAULT, cleanup, NULL);
	uv_run(async_loop, UV_RUN_DEFAULT);

	async_destroy();

	// TODO: Windows?
	if(sig) raise(sig);

	return 0;
}
