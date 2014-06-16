#include <uv.h>
#include "../deps/libco/libco.h"
#include "EarthFS.h"
#include "HTTPServer.h"

cothread_t yield = NULL;
uv_loop_t *loop = NULL;

void squvco_register(int const makeDefault);

extern HeaderFieldList const EFSHeaderFieldList;
void EFSServerDispatch(EFSRepoRef const repo, HTTPConnectionRef const conn);

int main(int const argc, char const *const *const argv) {
	signal(SIGPIPE, SIG_IGN);
	yield = co_active();
	loop = uv_default_loop();
	squvco_register(true);

	EFSRepoRef const repo = EFSRepoCreate("/home/ben/Documents/testrepo");

	HTTPServerRef const server = HTTPServerCreate((HTTPListener)EFSServerDispatch, repo, &EFSHeaderFieldList);
	HTTPServerListen(server, 8000, "127.0.0.1");

	uv_run(loop, UV_RUN_DEFAULT);

	HTTPServerClose(server);
	HTTPServerFree(server);

	EFSRepoFree(repo);

	return EXIT_SUCCESS;
}

