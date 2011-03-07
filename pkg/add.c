#include <stdio.h>
#include <pkg.h>
#include <stdbool.h>
#include <err.h>

#include "add.h"

void
usage_add(void)
{
	fprintf(stderr, "add ... <pkg-name>\n"
			"add\n");
}

int
exec_add(int argc, char **argv)
{
	struct pkgdb *db;
	struct pkgdb_it *it;
	struct pkg *pkg = NULL;
	struct pkg *p;
	struct pkg **deps;
	int ret = 0;
	bool installed = false;
	int i;

	if (argc != 2) {
		usage_add();
		return (-1);
	}

	if (pkg_open(argv[1], &pkg) != EPKG_OK) {
		pkg_error_warn("can not open file %s", argv[1]);
		return (-1);
	}

	if (pkgdb_open(&db) != EPKG_OK) {
		pkg_error_warn("can not open database");
		pkgdb_close(db);
		return (-1);
	}

	/* TODO: all the following stuff should be into libpkg */

	/* check if already installed */
	if ((it = pkgdb_query(db, pkg_get(pkg, PKG_ORIGIN), MATCH_EXACT)) == NULL) {
		pkg_error_warn("can not query the database");
		return (-1);
	}

	pkg_new(&p);

	if (pkgdb_it_next_pkg(it, &p, PKG_BASIC) == EPKG_OK) {
		installed = true;
	}
	pkgdb_it_free(it);

	if (installed) {
		err(1, "%s is already installed\n", pkg_get(pkg, PKG_NAME));
	}

	deps = pkg_deps(pkg);
	if (deps != NULL) {
		pkg_resolvdeps(pkg, db);

		for (i = 0; deps[i] != NULL; i++) {
			if (pkg_type(deps[i]) == PKG_NOTFOUND) {
				warnx("%s-%s: unresolved dependency %s-%s", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_get(deps[i], PKG_NAME), pkg_get(deps[i], PKG_VERSION));
				ret = 1;
			}
		}
	}

	if (ret != 0) {
		return (ret);
	}

	if (pkg_add(db, pkg) != EPKG_OK)
		pkg_error_warn("can not install %s", argv[1]);

	pkgdb_close(db);
	pkg_free(pkg);

	return (0);
}
