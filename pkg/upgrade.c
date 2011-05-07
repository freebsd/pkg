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
	struct pkgdb *db = NULL;
	struct pkgdb_it *it;
	struct pkg *pkg = NULL;
	int retcode = 0;
	int64_t oldsize = 0, newsize = 0;
	int64_t dlsize = 0;
	char size[7];

	(void) argv;
	if (argc != 1) {
		usage_upgrade();
		return (-1);
	}

	if (geteuid() != 0) {
		warnx("upgrading can only be done as root");
		return (EX_NOPERM);
	}

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK) {
		pkg_error_warn("can not open database");
		return (1);
	}

	if ((it = pkgdb_query_upgrades(db)) == NULL) {
		pkg_error_warn("can not query database");
		goto cleanup;
	}

	printf("Packages to be upgraded: \n");
	while ((retcode = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_NEWVERSION)) == EPKG_OK) {
		oldsize += pkg_flatsize(pkg);
		newsize += pkg_new_flatsize(pkg);
		dlsize += pkg_new_pkgsize(pkg);
		printf("\t%s: %s -> %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_get(pkg,PKG_NEWVERSION));
	}
	printf("\n");
	pkgdb_it_free(it);

	if ((it = pkgdb_query_downgrades(db)) == NULL) {
		pkg_error_warn("can not query database");
		goto cleanup;
	}

	printf("Packages to be downgraded: \n");
	while ((retcode = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_NEWVERSION)) == EPKG_OK) {
		oldsize += pkg_flatsize(pkg);
		newsize += pkg_new_flatsize(pkg);
		dlsize += pkg_new_pkgsize(pkg);
		printf("\t%s: %s -> %s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION), pkg_get(pkg, PKG_NEWVERSION));
	}
	printf("\n");
	pkgdb_it_free(it);


	if (oldsize > newsize) {
		newsize *= -1;
		humanize_number(size, sizeof(size), oldsize - newsize, "B", HN_AUTOSCALE, 0);
		printf("the upgrade will save %s\n", size);
	} else {
		humanize_number(size, sizeof(size), newsize - oldsize, "B", HN_AUTOSCALE, 0);
		printf("the upgrade will require %s more space\n", size);
	}
	humanize_number(size, sizeof(size), dlsize, "B", HN_AUTOSCALE, 0);
	printf("%s to be downloaded\n", size);


	cleanup:
	
	if (db != NULL)
		pkgdb_close(db);

	return (retcode);
}
