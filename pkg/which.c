#include <sys/param.h>

#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>

#include "pkgcli.h"

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
	struct pkg *pkg = NULL;
	char pathabs[MAXPATHLEN + 1];
	int ret = EPKG_OK, retcode = EPKG_OK;
	const char *name, *version;

	if (argc != 2) {
		usage_which();
		return (EX_USAGE);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		pkgdb_close(db);
		return (EX_IOERR);
	}

	absolutepath(argv[1], pathabs, sizeof(pathabs));

	if ((it = pkgdb_query_which(db, pathabs)) == NULL) {
		return (EX_IOERR);
	}

	if (( ret = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK) {
		retcode = EPKG_OK;
		pkg_get(pkg, PKG_NAME, &name, PKG_VERSION, &version);
		printf("%s was installed by package %s-%s\n", pathabs, name, version);
	} else if (ret != EPKG_END) {
		retcode = EPKG_WARN;
	} else {
		printf("%s was not found in the database\n", pathabs);
		retcode = EPKG_WARN;
	}
		
	pkg_free(pkg);
	pkgdb_it_free(it);

	pkgdb_close(db);
	return (retcode);
}
