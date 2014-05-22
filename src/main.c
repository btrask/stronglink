#include <stdio.h>
#include <stdlib.h>
#include "EarthFS.h"

int main(int argc, char **argv) {
	EFSHasherRef const hasher = EFSHasherCreate("text/plain");
	EFSHasherWrite(hasher, (void *)"asdf", 4);
	EFSURIListRef const URIs = EFSHasherCreateURIList(hasher);

	for(EFSIndex i = 0; i < EFSURIListGetCount(URIs); ++i) {
		printf("%s\n", EFSURIListGetURI(URIs, i));
	}

	EFSHasherFree(hasher);
	EFSURIListFree(URIs);
	return EXIT_SUCCESS;
}

