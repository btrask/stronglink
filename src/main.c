#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "EarthFS.h"
#include "EFSHTTPServer.h"

static void listener(void *const context, str_t const *const URI, fd_t const response, EFSHTTPCallbacks *const callbacks) {
	str_t buf[] =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html\r\n"
		"\r\n"
		"Test";
	(void)BTErrno(write(response, buf, sizeof(buf)));
	(void)close(response);
	printf("testing! %s, %d, %d\n", URI, (int)response, sizeof(buf));
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


	EFSHTTPServerRef const server = EFSHTTPServerCreate(listener, NULL);
	EFSHTTPServerListen(server, 8000, INADDR_LOOPBACK);
	sleep(1000);

	return EXIT_SUCCESS;
}

