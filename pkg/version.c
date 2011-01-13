#include <stdlib.h>
#include <stdio.h>
#include <sysexits.h>
#include <pkg.h>
#include "version.h"

void usage_version(void)
{
	fprintf(stderr, "version pkg0 pkg1\n"
		"compare pkg0 and pkg1 versions\n");
}

int exec_version(int argc, char **argv)
{
	if (argc != 3 || argv[1] == NULL || argv[2] == NULL) {
		usage_version();
		return (EX_USAGE);
	}

	switch (pkg_version_cmp(argv[1], argv[2])) {
		case -1:
			printf("<\n");
			break;
		case 0:
			printf("=\n");
			break;
		case 1:
			printf(">\n");
			break;
		default:
			break;
	}
	return 0;
}
