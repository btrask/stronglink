/*	$OpenBSD$	*/

/*	Written by Ingo Schwarze, 2013,  Public domain. */

void shortseek(void);
void longseek(void);

int
main(void)
{
	shortseek();
	longseek();
	return(0);
}
