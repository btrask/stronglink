#include <uv.h>
#include "EarthFS.h"
#include "HTTPServer.h"

void squvco_register(int const makeDefault);

void EFSServerDispatch(EFSRepoRef const repo, HTTPConnectionRef const conn);

int main(int const argc, char const *const *const argv) {
	squvco_register(1);

	EFSRepoRef const repo = EFSRepoCreate("/home/ben/Documents/testrepo");

	HTTPServerRef const server = HTTPServerCreate((HTTPListener)EFSServerDispatch, repo);
	HTTPServerListen(server, 8000, "127.0.0.1");

	uv_run(uv_default_loop(), UV_RUN_DEFAULT);

	HTTPServerClose(server);
	HTTPServerFree(server);

	EFSRepoFree(repo);

	return EXIT_SUCCESS;
}

