#include <sys/param.h>

#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <string.h>
#include <unistd.h>

#include "register.h"

int
cmd_register(int argc, char **argv)
{
	struct pkg *pkg;
	char ch;
	char *comment = NULL;
	char *descpath = NULL;
	char *oldplist = NULL;
	char *flattenedplist = NULL;
	char *prefix = NULL;
	char *mtree = NULL;
	char *origin = NULL;
	char *depends = NULL;

(void)pkg;
(void)flattenedplist;

	while ((ch = getopt(argc, argv, "vc:d:f:p:P:m:o:O:")) != -1) {
		switch (ch) {
			case 'O':
			case 'v':
				/* IGNORE */
				break;
			case 'c':
				comment = strdup(optarg);
				break;
			case 'd':
				descpath = strdup(optarg);
				break;
			case 'f':
				oldplist = strdup(optarg);
				break;
			case 'p':
				prefix = strdup(optarg);
				break;
			case 'P':
				depends = strdup(optarg);
				break;
			case 'm':
				mtree = strdup(optarg);
				break;
			case 'o':
				origin = strdup(optarg);
				break;
		}
	}
	printf("%s\n", comment);

	return (0);
}
