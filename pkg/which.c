#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>

#include "which.h"

int
cmd_which(int argc, char **argv)
{
	struct pkgdb *db;
	struct pkg *pkg;
	char pathabs[MAXPATHLEN];
	char pathabsdir[MAXPATHLEN];
	int retcode = 1;

	if (argc < 2 || argc > 4) {
		warnx("No file given");
		return (-1);
	}

	argc--;
	argv++;

	if (pkgdb_open(&db) == -1) {
		pkgdb_warn(db);
		return (-1);
	}

	realpath(dirname(argv[0]), pathabsdir);
	snprintf(pathabs, sizeof(pathabs), "%s/%s", pathabsdir, basename(argv[0]));

	pkg_new(&pkg);
	if (pkgdb_query_which(db, pathabs, pkg) == 0) {
		retcode = 0;
		printf("%s was installed by package %s-%s\n", pathabs, pkg_name(pkg),
			   pkg_version(pkg));
	}
	pkg_free(pkg);

	pkgdb_close(db);
	return (retcode);
}
