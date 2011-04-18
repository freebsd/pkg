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
	fprintf(stderr, "usage: pkg which <file>\n\n");
	fprintf(stderr, "For more information see 'pkg help which'.\n");
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
	int ret;

	if (argc != 2) {
		usage_which();
		return (EX_USAGE);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		pkg_error_warn("can not open database");
		pkgdb_close(db);
		return (-1);
	}

	realpath(dirname(argv[1]), pathabsdir);
	snprintf(pathabs, sizeof(pathabs), "%s/%s", pathabsdir, basename(argv[1]));

	if ((it = pkgdb_query_which(db, pathabs)) == NULL) {
		pkg_error_warn("can not query database");
		return (-1);
	}

	pkg_new(&pkg);
	if (( ret = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK) {
		retcode = 0;
		printf("%s was installed by package %s-%s\n", pathabs, pkg_get(pkg, PKG_NAME),
			   pkg_get(pkg, PKG_VERSION));
	} else if (ret != EPKG_END) {
		pkg_error_warn("can not iterate over results");
		retcode = -1;
	} else {
		printf("%s was not found in the database\n", pathabs);
		retcode = -1;
	}
		
	pkg_free(pkg);
	pkgdb_it_free(it);

	pkgdb_close(db);
	return (retcode);
}
