#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sysexits.h>
#include <pkg.h>
#include "version.h"

void
usage_version(void)
{
	fprintf(stderr, "version [-hIoqv] [-l limchar] [-L limchar] [[-X] -s string]"
			"[-O origin] [index]\n"
			"version -t version1 version2\n"
			"version -T pkgname pattern\n");
}

int exec_version(int argc, char **argv)
{
	unsigned int opt = 0;
	int ch;

	while ((ch = getopt(argc, argv, "hIoqvlLXsOtT")) != -1) {
		switch (ch) {
			case 'h':
				usage_version();
				return (0);
			case 'I':
				opt |= VERSION_INDEX;
				break;
			case 'o':
				opt |= VERSION_ORIGIN;
				break;
			case 'q':
				opt |= VERSION_QUIET;
				break;
			case 'v':
				opt |= VERSION_VERBOSE;
				break;
			case 'l':
				opt |= VERSION_STATUS;
				break;
			case 'L':
				opt |= VERSION_NOSTATUS;
				break;
			case 'X':
				opt |= VERSION_EREGEX;
				break;
			case 's':
				opt |= VERSION_STRING;
				break;
			case 'O':
				opt |= VERSION_WITHORIGIN;
				break;
			case 't':
				opt |= VERSION_TESTVERSION;
				break;
			case 'T':
				opt |= VERSION_TESTPATTERN;
				break;
		}
	}
	argc -= optind;
	argv += optind;

	/* -t must be unique */
	if (((opt & VERSION_TESTVERSION) && opt != VERSION_TESTVERSION) ||
			(opt == VERSION_TESTVERSION && argc < 2)) {
		usage_version();
		return (EX_USAGE);
	}

	else if (opt == VERSION_TESTVERSION) {
		switch (pkg_version_cmp(argv[0], argv[1])) {
			case -1:
				printf("<\n");
				break;
			case 0:
				printf("=\n");
				break;
			case 1:
				printf(">\n");
				break;
		}
	}

	return 0;
}
