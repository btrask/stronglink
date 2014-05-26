#include "EarthFS.h"
#include "HTTPServer.h"

static void listener(void *const context, HTTPConnectionRef const conn) {
	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html");
	HTTPConnectionBeginBody(conn);
	fd_t const stream = HTTPConnectionGetStream(conn);
	write(stream, "Test", 4);
	HTTPConnectionClose(conn);
	printf("testing! %s\n", HTTPConnectionGetRequestURI(conn));
}

int main(int argc, char **argv) {
/*	EFSHasherRef const hasher = EFSHasherCreate("text/plain");
	EFSHasherWrite(hasher, (void *)"asdf", 4);
	EFSURIListRef const URIs = EFSHasherCreateURIList(hasher);

	for(EFSIndex i = 0; i < EFSURIListGetCount(URIs); ++i) {
		printf("%s\n", EFSURIListGetURI(URIs, i));
	}

	EFSHasherFree(hasher);
	EFSURIListFree(URIs);*/


	HTTPServerRef const server = HTTPServerCreate(listener, NULL);
	HTTPServerListen(server, 8000, INADDR_LOOPBACK);
	sleep(1000);

	return EXIT_SUCCESS;
}

