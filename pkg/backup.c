#include <pkg.h>

#include "backup.h"

void
usage_backup(void)
{
	fprintf(stderr, "usage: pkg backup -[d|r] dest\n");
	fprintf(stderr, "For more information see 'pkg help backup'.\n");
}

int
exec_backup(int argc, char **argv)
{
	struct pkgdb  *db;
	char *dest = NULL;

	if (argc < 1 || argc > 2 || argv[1][0] != '-') {
		usage_backup();
		return (-1);
	}

	if (argc == 2)
		dest = argv[2];

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		pkg_error_warn("can not open database");
		pkgdb_close(db);
		return (EPKG_FATAL);
	}

	if (argv[1][1] == 'd') {
		printf("Dumping database...");
		fflush(stdout);
		pkgdb_dump(db, dest);
		printf("Done\n");
	}

	if (argv[1][1] == 'r') {
		fprintf(stderr, "not yet implemented\n");
	}
}
