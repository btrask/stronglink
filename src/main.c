#include "async.h"
#include "EarthFS.h"
#include "HTTPServer.h"

void sqlite_async_register(void);

extern HeaderFieldList const EFSHeaderFieldList;
bool_t EFSServerDispatch(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI);

typedef struct Blog* BlogRef;

BlogRef BlogCreate(EFSRepoRef const repo);
void BlogFree(BlogRef const blog);
bool_t BlogDispatch(BlogRef const blog, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI);

static EFSRepoRef repo = NULL;
static BlogRef blog = NULL;
static HTTPServerRef server = NULL;

static void listener(void *ctx, HTTPConnectionRef const conn) {
	HTTPMethod const method = HTTPConnectionGetRequestMethod(conn);
	strarg_t const URI = HTTPConnectionGetRequestURI(conn);
	if(EFSServerDispatch(repo, conn, method, URI)) return;
	if(BlogDispatch(blog, conn, method, URI)) return;
	HTTPConnectionSendStatus(conn, 404);
}
static void init(void) {
	repo = EFSRepoCreate("/home/ben/Documents/testrepo");
	blog = BlogCreate(repo);
	server = HTTPServerCreate((HTTPListener)listener, blog, &EFSHeaderFieldList);
	HTTPServerListen(server, 8000, "127.0.0.1");
	co_terminate();
}
static void term(void) {
	HTTPServerClose(server);
	HTTPServerFree(server); server = NULL;
	BlogFree(blog); blog = NULL;
	EFSRepoFree(repo); repo = NULL;
	co_terminate();
}
int main(int const argc, char const *const *const argv) {
	signal(SIGPIPE, SIG_IGN);
	async_init();
	sqlite_async_register();

	// Even our init code wants to use async I/O.
	co_switch(co_create(STACK_SIZE, init));
	uv_run(loop, UV_RUN_DEFAULT);

	co_switch(co_create(STACK_SIZE, term));
	uv_run(loop, UV_RUN_DEFAULT); // Allows term() to execute.

	return EXIT_SUCCESS;
}

