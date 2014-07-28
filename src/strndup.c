#include <stdlib.h>
#include "strndup.h"

// strndup() isn't available on some platforms like OS X 10.6.
// TODO: Find a real implementation so we know we aren't
// introducing any stupid bugs.
char *strndup(const char *s, size_t n) {
        size_t len = 0;
        for(; len < n && '\0' != s[len]; ++len);
        char *x = malloc(len+1);
        if(!x) return NULL;
        memcpy(x, s, len);
	x[len] = '\0';
        return x;
}
