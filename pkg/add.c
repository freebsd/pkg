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

	if (argc != 2) {
		usage_add();
		return (-1);
	}

	if (pkgdb_open(&db) != EPKG_OK) {
		pkg_error_warn("can not open database");
		pkgdb_close(db);
		return (-1);
	}

	if (pkg_add(db, argv[1]) != EPKG_OK)
		pkg_error_warn("can not install %s", argv[1]);

	pkgdb_close(db);

	return (0);
}
