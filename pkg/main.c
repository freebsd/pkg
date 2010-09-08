#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>

static void
usage()
{
	fprintf(stderr, "usage: ...");
	exit(EX_USAGE);
}

int
main(int argc, char **argv)
{
	return (EXIT_SUCCESS);
}
