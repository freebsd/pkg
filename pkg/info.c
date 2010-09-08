#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <pkgdb.h>

#include "info.h"

void *
cmd_info(int argc, char **argv)
{
	struct pkg **pkgs = NULL;

	if (argc == 1) {
		pkgs = pkgdb_list_packages();
	} else {
		printf("Not implemented yet\n");
	}
}
