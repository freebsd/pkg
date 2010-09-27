#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <pkgdb.h>

#include "info.h"

int
cmd_info(int argc, char **argv)
{
	struct pkgdb db;
	struct pkg *pkg;
	(void)argc;

	pkgdb_init(&db, argv[1]);

	if (pkgdb_count(&db) == 1) {
		/* one match */
		pkg = TAILQ_FIRST(&db.pkgs);
		printf("Information for %s-%s\n", pkg->name, pkg->version);
		printf("Comment:\n%s\n\n", pkg->comment);
		printf("Description:\n%s\n\n", pkg->desc);
	}
	else if (pkgdb_count(&db) > 1) {
		TAILQ_FOREACH(pkg, &db.pkgs, entry) {
			printf("%s-%s: %s\n", pkg->name, pkg->version, pkg->comment);
		}
	}

	pkgdb_free(&db);
	return (0);
}
