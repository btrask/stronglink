#include "async.h"
#include "EarthFS.h"
#include "HTTPServer.h"

void sqlite_async_register(void);

extern HeaderFieldList const EFSHeaderFieldList;
bool_t EFSServerDispatch(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI);

bool_t BlogDispatch(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI);

static void listener(EFSRepoRef const repo, HTTPConnectionRef const conn) {
	HTTPMethod const method = HTTPConnectionGetRequestMethod(conn);
	strarg_t const URI = HTTPConnectionGetRequestURI(conn);
	if(EFSServerDispatch(repo, conn, method, URI)) return;
	if(BlogDispatch(repo, conn, method, URI)) return;
	HTTPConnectionSendStatus(conn, 404);
}

int main(int const argc, char const *const *const argv) {
	signal(SIGPIPE, SIG_IGN);
	async_init();
	sqlite_async_register();

	EFSRepoRef const repo = EFSRepoCreate("/home/ben/Documents/testrepo");

	HTTPServerRef const server = HTTPServerCreate((HTTPListener)listener, repo, &EFSHeaderFieldList);
	HTTPServerListen(server, 8000, "127.0.0.1");

	uv_run(loop, UV_RUN_DEFAULT);

	HTTPServerClose(server);
	HTTPServerFree(server);

	EFSRepoFree(repo);

	return EXIT_SUCCESS;
}

