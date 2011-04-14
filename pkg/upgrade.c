#include <sys/param.h>
#include <sys/types.h>

#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <libutil.h>

#include <pkg.h>

#include "upgrade.h"

void
usage_upgrade(void)
{
	fprintf(stderr, "usage pkg upgrade\n\n");
	fprintf(stderr, "For more information see 'pkg help upgrade'.\n");
}

int
exec_upgrade(int argc, char **argv)
{
	int fd;
	struct pkgdb *db = NULL;
	struct pkgdb_it *it;
	struct pkg *pkg = NULL;
	char *packagesite = NULL;
	int retcode = 0;
	properties conf;

	(void) argv;
	if (argc != 1) {
		usage_upgrade();
		return (-1);
	}

	if (geteuid() != 0) {
		warnx("updating the remote database can only be done as root");
		return (EX_NOPERM);
	}

	if ((fd = open("/etc/pkg.conf", O_RDONLY)) > 0) {
		conf = properties_read(fd);
		close(fd);
	}

	packagesite = getenv("PACKAGESITE");

	if (packagesite == NULL) {
		packagesite = property_find(conf, "packagesite");
		if (packagesite == NULL) {
			pkg_error_warn("unable to determine PACKAGESITE");
			retcode = 1;
			goto cleanup;
		}
	}


	if (pkgdb_open(&db) != EPKG_OK) {
		pkg_error_warn("can not open database");
		return (1);
	}


	if ((it = pkgdb_repos_diff(db)) == NULL) {
		pkg_error_warn("can not query database");
		goto cleanup;
	}

	while ((retcode = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_NEWVERSION)) == EPKG_OK) {
		printf("%s: %s -> %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_get(pkg, PKG_NEWVERSION));
	}

	cleanup:
	
	if (db != NULL)
		pkgdb_close(db);

	if (conf != NULL)
		properties_free(conf);

	return (retcode);
}
