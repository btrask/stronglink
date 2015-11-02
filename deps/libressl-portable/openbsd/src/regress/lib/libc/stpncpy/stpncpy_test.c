/* $OpenBSD$ */

/*
 * Public domain, 2012, Christian Weisgerber <naddy@openbsd.org>
 */

#include <string.h>

int main(void)
{
	char dst[8];
	char *src = "abcdef";

	if (stpncpy(dst, src, 5) != dst + 5)
		return 1;
	if (stpncpy(dst, src, 6) != dst + 6)
		return 1;
	if (stpncpy(dst, src, 7) != dst + 6)
		return 1;
	if (stpncpy(dst, src, 8) != dst + 6)
		return 1;
	
	return 0;
}
