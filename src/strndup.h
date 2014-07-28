#include <stdlib.h>
#include <string.h>

static char *strndup(const char *s, size_t n) {
        size_t len = 0;
        for(; len <= n && '\0' != s[len]; ++len);
        char *x = malloc(len);
        if(!x) return NULL;
        memcpy(x, s, len);
        return x;
}
