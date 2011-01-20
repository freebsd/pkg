#include <sys/param.h>

#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>

#include "create.h"

void
usage_create(void)
{
	fprintf(stderr, "create [-gx] [-r rootdir] [-m manifest] [-f format] [-o outdir] "
			"<pkg-name>\n"
			"create -a [-r rootdir] [-m manifest] [-f format] [-o outdir]\n");
}

/*
 * options:
 * -x: regex
 * -g: globbing
 * -r: rootdir for the package
 * -m: path to dir where to find the +MANIFEST
 * -f <format>: format could be txz, tgz, tbz or tar
 * -o: output directory where to create packages by default ./ is used
 */

int
exec_create(int argc, char **argv)
{
	struct pkgdb *db;
	struct pkgdb_it *it;
	struct pkg *pkg;

	match_t match = MATCH_EXACT;
	const char *outdir = NULL;
	const char *format = NULL;
	const char *rootdir = NULL;
	const char *manifestdir = NULL;
	pkg_formats fmt;
	char mpath[MAXPATHLEN];
	char ch;

	while ((ch = getopt(argc, argv, "agxXf:r:m:o:")) != -1) {
		switch (ch) {
			case 'a':
				match = MATCH_ALL;
				break;
			case 'g':
				match = MATCH_GLOB;
				break;
			case 'x':
				match = MATCH_REGEX;
				break;
			case 'X':
				match = MATCH_EREGEX;
				break;
			case 'f':
				format = optarg;
				break;
			case 'o':
				outdir = optarg;
				break;
			case 'r':
				rootdir = optarg;
				break;
			case 'm':
				manifestdir = optarg;
				break;
		}
	}
	argc -= optind;
	argv += optind;

	if (match != MATCH_ALL && argc == 0) {
		usage_create();
		return (EX_USAGE);
	}

	if (outdir == NULL)
		outdir = "./";

	if (format == NULL) {
		fmt = TXZ;
	} else {
		if (strcmp(format, "txz") == 0)
			fmt = TXZ;
		else if (strcmp(format, "tbz") == 0)
			fmt = TBZ;
		else if (strcmp(format, "tgz") == 0)
			fmt = TGZ;
		else if (strcmp(format, "tar") == 0)
			fmt = TAR;
		else {
			warnx("Unknown format %s, using txz", format);
			fmt = TXZ;
		}
	}

	if (manifestdir == NULL) {
		/* create package from local db */
		if (pkgdb_open(&db) == -1) {
			pkgdb_warn(db);
			pkgdb_close(db);
			return (-1);
		}

		if ((it = pkgdb_query(db, argv[0], match)) == NULL) {
			pkgdb_warn(db);
			return (-1);
		}

		pkg_new(&pkg);
		while (pkgdb_it_next_pkg(it, &pkg, PKG_ALL) == 0) {
			printf("Creating package for %s-%s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
			pkg_create(NULL, fmt, outdir, rootdir, pkg);
		}
		pkg_free(pkg);
		pkgdb_it_free(it);
		pkgdb_close(db);
	} else {
		snprintf(mpath, sizeof(mpath), "%s/+MANIFEST", manifestdir);
		pkg_create(mpath, fmt, outdir, rootdir, NULL);
	}
	return (0);
}
