#include "async.h"
#include "EarthFS.h"
#include "http/HTTPServer.h"

bool_t EFSServerDispatch(EFSRepoRef const repo, HTTPMessageRef const msg, HTTPMethod const method, strarg_t const URI);

typedef struct Blog* BlogRef;

BlogRef BlogCreate(EFSRepoRef const repo);
void BlogFree(BlogRef const blog);
bool_t BlogDispatch(BlogRef const blog, HTTPMessageRef const msg, HTTPMethod const method, strarg_t const URI);

static EFSRepoRef repo = NULL;
static BlogRef blog = NULL;
static HTTPServerRef server = NULL;

static void listener(void *ctx, HTTPMessageRef const msg) {
	HTTPMethod const method = HTTPMessageGetRequestMethod(msg);
	strarg_t const URI = HTTPMessageGetRequestURI(msg);
	if(EFSServerDispatch(repo, msg, method, URI)) return;
	if(BlogDispatch(blog, msg, method, URI)) return;
	HTTPMessageSendStatus(msg, 400);
}
static void init(void) {
	repo = EFSRepoCreate("/home/ben/Documents/testrepo");
	blog = BlogCreate(repo);
	server = HTTPServerCreate((HTTPListener)listener, blog);
	HTTPServerListen(server, "8000", INADDR_LOOPBACK);
	EFSRepoStartPulls(repo);
	co_terminate();
}
static void term(void) {
	// TODO: EFSRepoStopPulls(repo);
	HTTPServerClose(server);
	HTTPServerFree(server); server = NULL;
	BlogFree(blog); blog = NULL;
	EFSRepoFree(repo); repo = NULL;
	co_terminate();
}
int main(int const argc, char const *const *const argv) {
	signal(SIGPIPE, SIG_IGN);
	async_init();
	async_sqlite_register();

	// Even our init code wants to use async I/O.
	async_wakeup(co_create(STACK_SIZE, init));
	uv_run(loop, UV_RUN_DEFAULT);

	async_wakeup(co_create(STACK_SIZE, term));
	uv_run(loop, UV_RUN_DEFAULT); // Allows term() to execute.

	return EXIT_SUCCESS;
}

