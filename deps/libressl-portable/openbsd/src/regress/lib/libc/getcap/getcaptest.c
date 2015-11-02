/*	$OpenBSD$	*/

/*
 * Written by Raymond Lai <ray@cyth.net>.
 * Public domain.
 */

#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char *argv[])
{
	char *buf, *db_array[2];

	if (argc != 2)
		return (1);

	db_array[0] = argv[1];
	db_array[1] = NULL;

	while (cgetnext(&buf, db_array) > 0)
		puts(buf);

	return (0);
}
