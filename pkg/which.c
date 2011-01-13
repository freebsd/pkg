#include <sys/param.h>

#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>

#include "which.h"

void
usage_which(void)
{
	fprintf(stderr, "which <file>\n");
}

int
exec_which(int argc, char **argv)
{
	struct pkgdb *db;
	struct pkgdb_it *it;
	struct pkg *pkg;
	char pathabs[MAXPATHLEN];
	char pathabsdir[MAXPATHLEN];
	int retcode = 1;

	if (argc != 2) {
		usage_which();
		return (EX_USAGE);
	}

	if (pkgdb_open(&db) == -1) {
		pkgdb_warn(db);
		return (-1);
	}

	realpath(dirname(argv[1]), pathabsdir);
	snprintf(pathabs, sizeof(pathabs), "%s/%s", pathabsdir, basename(argv[1]));

	if ((it = pkgdb_query_which(db, pathabs)) == NULL) {
		pkgdb_warn(db);
		return (-1);
	}

	pkg_new(&pkg);
	if (pkgdb_it_next_pkg(it, &pkg, PKG_BASIC) == 0) {
		retcode = 0;
		printf("%s was installed by package %s-%s\n", pathabs, pkg_name(pkg),
			   pkg_version(pkg));
	}
	pkg_free(pkg);
	pkgdb_it_free(it);

	pkgdb_close(db);
	return (retcode);
}
