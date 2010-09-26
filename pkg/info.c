#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <pkgdb.h>

#include "info.h"

int
cmd_info(int argc, char **argv)
{
	struct pkg **pkgs;
	int i;
	(void)argc;

	if ((pkgs = pkgdb_list_packages(argv[1])) == NULL)
		return 0;

	if (pkgdb_count(pkgs) == 1) {
		/* one match */
		printf("Information for %s-%s\n", pkgs[0]->name, pkgs[0]->version);
		printf("Comment:\n%s\n\n", pkgs[0]->comment);
		printf("Description:\n%s\n\n", pkgs[0]->desc);
	}
	else {
		for (i = 0; pkgs[i] != NULL; i++)
			printf("%s-%s: %s\n", pkgs[i]->name, pkgs[i]->version, pkgs[i]->comment);
	}

	pkgdb_free(pkgs);
	return (0);
}
