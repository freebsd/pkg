#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <pkgdb.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include "create.h"

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
cmd_create(int argc, char **argv)
{
	struct pkgdb db;
	struct pkg pkg;

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

	if (argc == 0) {
		warnx("No package provided");
		return (-1);
	}

	if (rootdir == NULL)
		rootdir = "/";

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
			pkgdb_warn(&db);
			return (-1);
		}

		if (pkgdb_query_init(&db, argv[0], match) == -1) {
			pkgdb_warn(&db);
			return (-1);
		}

		while (pkgdb_query(&db, &pkg) == 0) {
			snprintf(mpath, sizeof(mpath), "%s/%s-%s/+MANIFEST", pkgdb_get_dir(), pkg_name(&pkg), pkg_version(&pkg));
			pkg_create(mpath, fmt, outdir, rootdir, &pkg);
		}
		pkgdb_query_free(&db);
		pkgdb_close(&db);
	} else {
		snprintf(mpath, sizeof(mpath), "%s/+MANIFEST", manifestdir);
		pkg_create(mpath, fmt, outdir, rootdir, NULL);
	}
	return (0);
}
