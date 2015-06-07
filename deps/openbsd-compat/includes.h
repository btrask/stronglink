// TODO: HACK
// We just copy some of the definitions we need out of openbsd-compat.h.
// That header assumes a UNIX-like system, whereas we only want
// a few C runtime extensions.

#include <sys/types.h>

#ifndef HAVE_STRLCPY
/* #include <sys/types.h> XXX Still needed? */
size_t strlcpy(char *dst, const char *src, size_t siz);
#endif

#ifndef HAVE_STRLCAT
/* #include <sys/types.h> XXX Still needed? */
size_t strlcat(char *dst, const char *src, size_t siz);
#endif 

