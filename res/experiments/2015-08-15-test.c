#include <stdlib.h>

void FREE(void **ptrptr) {
	free(*ptrptr); *ptrptr = NULL;
}
int main() {
	char *x = malloc(100);
	FREE(&x);
	return 0;
}

