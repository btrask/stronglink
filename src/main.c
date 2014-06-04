#include "EarthFS.h"
#include "HTTPServer.h"

void EFSServerDispatch(EFSRepoRef const repo, HTTPConnectionRef const conn);

int main(int const argc, char const *const *const argv) {
	EFSRepoRef const repo = EFSRepoCreate("/home/ben/Documents/testrepo");

	HTTPServerRef const server = HTTPServerCreate((HTTPListener)EFSServerDispatch, repo);
	HTTPServerListen(server, 8000, INADDR_LOOPBACK);

	sleep(1000);

	HTTPServerClose(server);
	HTTPServerFree(server);

	EFSRepoFree(repo);

	return EXIT_SUCCESS;
}

