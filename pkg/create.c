#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <pkgdb.h>

#include "create.h"

/*
 * options:
 * -x: regex
 * -g: globbing
 * -b: backup (create from installed pkg)
 * -r: rootdir for the package
 * -m: path to dir where to find the +MANIFEST
 * -f <format>: format could be txz, tgz, tbz or tar
 * -o: output directory where to create packages by default ./ is used
 */

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
