#include <limits.h>
#include <signal.h>
#include "util/fts.h"
#include "util/hash.h"
#include "util/raiserlimit.h"
#include "EarthFS.h"
#include "http/HTTPServer.h"
#include "http/HTTPHeaders.h"

int EFSServerDispatch(EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers);

typedef struct Blog* BlogRef;

BlogRef BlogCreate(EFSRepoRef const repo);
void BlogFree(BlogRef *const blogptr);
int BlogDispatch(BlogRef const blog, EFSSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers);

static strarg_t path = NULL;
static EFSRepoRef repo = NULL;
static BlogRef blog = NULL;
static HTTPServerRef server = NULL;
static uv_signal_t sigint[1] = {};
static int sig = 0;

static void listener(void *ctx, HTTPConnectionRef const conn) {
	HTTPMethod method;
	str_t URI[URI_MAX];
	int rc = HTTPConnectionReadRequest(conn, &method, URI, URI_MAX);
	if(rc < 0) return;

	HTTPHeadersRef headers = HTTPHeadersCreateFromConnection(conn);
	if(!headers) return;

	strarg_t const cookie = HTTPHeadersGet(headers, "cookie");
	EFSSessionRef session = EFSRepoCreateSession(repo, cookie);
	if(!session) {
		HTTPHeadersFree(&headers);
		HTTPConnectionSendStatus(conn, 403); // TODO
		return;
	}

	rc = -1;
	rc = rc >= 0 ? rc : EFSServerDispatch(session, conn, method, URI, headers);
	rc = rc >= 0 ? rc : BlogDispatch(blog, session, conn, method, URI, headers);

	EFSSessionFree(&session);
	HTTPHeadersFree(&headers);
	if(rc < 0) rc = 404;
	if(rc > 0) HTTPConnectionSendStatus(conn, rc);
}

static void ignore(uv_signal_t *const signal, int const signum) {
	// Do nothing
}
static void stop(uv_signal_t *const signal, int const signum) {
	sig = signum;
	uv_stop(loop);
}

static void init(void *const unused) {
	async_random((byte_t *)&hash_salt, sizeof(hash_salt));

	repo = EFSRepoCreate(path, "unnamed repo"); // TODO
	if(!repo) {
		fprintf(stderr, "Repository could not be opened\n");
		return;
	}
	blog = BlogCreate(repo);
	if(!blog) {
		fprintf(stderr, "Blog server could not be initialized\n");
		return;
	}
	server = HTTPServerCreate((HTTPListener)listener, blog);
	if(!server) {
		fprintf(stderr, "Web server could not be initialized\n");
		return;
	}
	uint32_t type;
//	type = INADDR_ANY;
	type = INADDR_LOOPBACK;
	int rc = HTTPServerListen(server, "8000", type); // TODO
	if(rc < 0) {
		fprintf(stderr, "Unable to start server (%d, %s)", rc, uv_strerror(rc));
		return;
	}
	fprintf(stderr, "StrongLink server running at http://localhost:8000/\n");
	EFSRepoPullsStart(repo);

	uv_signal_init(loop, sigint);
	uv_signal_start(sigint, stop, SIGINT);
	uv_unref((uv_handle_t *)sigint);
}
static void term(void *const unused) {
	fprintf(stderr, "\nStopping StrongLink server...\n");
	uv_ref((uv_handle_t *)sigint);
	uv_signal_stop(sigint);
	async_close((uv_handle_t *)sigint);

	EFSRepoPullsStop(repo);
	HTTPServerClose(server);
}
int main(int const argc, char const *const *const argv) {
	// Depending on how async_pool and async_fs are configured, we might be
	// using our own thread pool heavily or not. However, at the minimum,
	// uv_getaddrinfo uses the libuv thread pool, and it blocks on the
	// network, so don't set this number too low.
	if(!getenv("UV_THREADPOOL_SIZE")) putenv("UV_THREADPOOL_SIZE=4");

	raiserlimit();
	async_init();
	uv_signal_t sigpipe[1];
	uv_signal_init(loop, sigpipe);
	uv_signal_start(sigpipe, ignore, SIGPIPE);
	uv_unref((uv_handle_t *)sigpipe);

	// TODO: Real option parsing.
	if(2 != argc || '-' == argv[1][0]) {
		fprintf(stderr, "Usage:\n\t" "%s <repo>\n", argv[0]);
		return 1;
	}
	path = argv[1];

	// Even our init code wants to use async I/O.
	async_spawn(STACK_DEFAULT, init, NULL);
	uv_run(loop, UV_RUN_DEFAULT);

	async_spawn(STACK_DEFAULT, term, NULL);
	uv_run(loop, UV_RUN_DEFAULT); // Allows term() to execute.
	HTTPServerFree(&server);
	BlogFree(&blog);
	EFSRepoFree(&repo);

	uv_ref((uv_handle_t *)sigpipe);
	uv_signal_stop(sigpipe);
	uv_close((uv_handle_t *)sigpipe, NULL);

	// TODO: Windows?
	if(sig) raise(sig);

	return 0;
}
