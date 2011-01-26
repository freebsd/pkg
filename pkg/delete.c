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
	char ch;
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

	if (pkgdb_open(&db) == 1) {
		pkgdb_warn(db);
		pkgdb_close(db);
		return (-1);
	}

	if (argc == 1)
		origin = argv[0];

	it = pkgdb_query(db, origin, match);

	pkg_new(&pkg);
	while (pkgdb_it_next_pkg(it, &pkg, PKG_BASIC|PKG_FILES|PKG_RDEPS) == 0) {
		if (pkg_delete(pkg, db, force) != 0) {
			retcode++;
			warnx("Can not delete %s", pkg_get(pkg, PKG_ORIGIN));
		}
	}
	pkg_free(pkg);

	pkgdb_it_free(it);
	pkgdb_close(db);
	return (retcode);
}
