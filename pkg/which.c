#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <pkgdb.h>
#include <pkg_manifest.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>

#include "which.h"

int
cmd_which(int argc, char **argv)
{
	struct pkgdb db;
	struct pkg pkg;
	struct pkg_manifest *m;
	char pathabs[MAXPATHLEN];
	char pathabsdir[MAXPATHLEN];

	if (argc < 2 || argc > 4) {
		warnx("No file given");
		return (-1);
	}

	argc--;
	argv++;

	if (pkgdb_open(&db) == -1) {
		pkgdb_warn(&db);
		return (-1);
	}

	if (pkgdb_query_init(&db, NULL, MATCH_ALL) == -1) {
		pkgdb_warn(&db);
		return (-1);
	}

	realpath(dirname(argv[0]), pathabsdir);
	snprintf(pathabs, sizeof(pathabs), "%s/%s", pathabsdir, basename(argv[0]));

	while (pkgdb_query(&db, &pkg) == 0) {
		manifest_from_pkg(&pkg, &m);
		pkg_manifest_file_init(m);
		while (pkg_manifest_file_next(m) == 0) {
			if (strcmp(pathabs, pkg_manifest_file_path(m)) == 0) {
				printf("%s is owned by %s-%s\n", pathabs, pkg_name(&pkg), pkg_version(&pkg));
				pkgdb_query_free(&db);
				pkgdb_close(&db);
				return (0);
			}
		}

	}

	warnx("No packages owns %s", pathabs);
	pkgdb_query_free(&db);
	pkgdb_close(&db);
	return (-1);
}
