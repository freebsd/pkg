#include <err.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "delete.h"

void
usage_delete(void)
{
	fprintf(stderr, "delete [-f] <pkg-name>\n"
			"delete -a\n");
}

int
exec_delete(int argc, char **argv)
{
	struct pkg *pkg;
	struct pkgdb *db;
	struct pkgdb_it *it;
	match_t match = MATCH_EXACT;
	char *origin = NULL;
	int ch;
	int ret;
	int flags = PKG_LOAD_BASIC;
	int force = 0;
	int retcode = 0;

	while ((ch = getopt(argc, argv, "af")) != -1) {
		switch (ch) {
			case 'a':
				match = MATCH_ALL;
				force = 1;
				break;
			case 'f':
				force = 1;
				break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1 && match == MATCH_EXACT) {
		usage_delete();
		return (EX_USAGE);
	}

	if (pkgdb_open(&db) != EPKG_OK) {
		pkg_error_warn("can not open database");
		pkgdb_close(db);
		return (-1);
	}

	if (argc == 1)
		origin = argv[0];

	if ((it = pkgdb_query(db, origin, match)) == NULL) {
		pkg_error_warn("Can not query database");
		return (1);
	}

	pkg_new(&pkg);
	while ((ret = pkgdb_it_next(it, &pkg, flags)) == EPKG_OK) {
		if (pkg_delete(pkg, db, force) != EPKG_OK) {
			retcode++;
			pkg_error_warn("can not delete %s", pkg_get(pkg, PKG_ORIGIN));
		}
	}
	pkg_free(pkg);

	if (ret != EPKG_END) {
		pkg_error_warn("can not iterate over results");
		retcode++;
	}

	pkgdb_it_free(it);

	if (pkgdb_compact(db) != EPKG_OK)
		pkg_error_warn("can not compact database");

	pkgdb_close(db);
	return (retcode);
}
