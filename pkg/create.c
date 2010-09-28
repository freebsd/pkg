#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <pkgdb.h>

#include "create.h"
#define args

int
cmd_create(int argc, char **argv)
{
	if (argc < 2) {
		warnx("No arguments provided");
		return (-1);
	}
	pkg_create(argv[1], TXZ, "./", "/");
	return (0);
}
