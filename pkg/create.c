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
	fprintf(stderr, "usage: pkg create [-gx] [-r rootdir] [-m manifest] [-f format] [-o outdir] "
			"<pkg> ...\n");
	fprintf(stderr, "       pkg create -a [-r rootdir] [-m manifest] [-f format] [-o outdir]\n\n");
	fprintf(stderr, "For more information see 'pkg help create'.\n");
}

/*
 * options:
 * -x: regex
 * -g: globbing
 * -r: rootdir for the package
 * -m: path to dir where to find the metadata
 * -f <format>: format could be txz, tgz, tbz or tar
 * -o: output directory where to create packages by default ./ is used
 */

int
exec_create(int argc, char **argv)
{
	match_t match = MATCH_EXACT;
	const char *outdir = NULL;
	const char *format = NULL;
	const char *rootdir = NULL;
	const char *manifestdir = NULL;
	pkg_formats fmt;
	int ch;

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
			warnx("unknown format %s, using txz", format);
			fmt = TXZ;
		}
	}

	if (manifestdir == NULL)
		return pkg_create_matches(argc, argv, match, fmt, outdir, rootdir);
	else
		return pkg_create_fakeroot(outdir, fmt, rootdir, manifestdir);
}

