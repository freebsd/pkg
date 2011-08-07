#include <pkg.h>
#include <sysexits.h>

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

	if (argc < 2 || argc > 3 || argv[1][0] != '-') {
		usage_backup();
		return (EX_USAGE);
	}

	if (argc == 3)
		dest = argv[2];

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (EX_IOERR);

	if (argv[1][1] == 'd') {
		printf("Dumping database...");
		fflush(stdout);
		if (pkgdb_dump(db, dest) == EPKG_FATAL)
			return (EPKG_FATAL);

		printf("done\n");
	}

	if (argv[1][1] == 'r') {
		printf("Restoring database...");
		fflush(stdout);
		if (pkgdb_load(db, dest) == EPKG_FATAL)
			return (EPKG_FATAL);
		printf("done\n");
	}

	pkgdb_close(db);


	return (EPKG_OK);
}
