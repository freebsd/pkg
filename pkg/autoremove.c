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

#include "autoremove.h"

static int
query_yesno(const char *msg)
{
	int c, r = 0;

	printf(msg);

	c = getchar();
	if (c == 'y' || c == 'Y')
		r = 1;
	else if (c == '\n' || c == EOF)
		return 0;

	while((c = getchar()) != '\n' && c != EOF)
		continue;

	return r;
}

void
usage_autoremove(void)
{
	fprintf(stderr, "usage pkg autoremove\n\n");
	fprintf(stderr, "For more information see 'pkg help autoremove'.\n");
}

int
exec_autoremove(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct pkgdb_it *it;
	struct pkg *pkg = NULL;
	int retcode = 0;
	int64_t oldsize = 0, newsize = 0;
	char size[7];

	(void) argv;
	if (argc != 1) {
		usage_autoremove();
		return (-1);
	}

	if (geteuid() != 0) {
		warnx("autoremove can only be done as root");
		return (EX_NOPERM);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		pkg_error_warn("can not open database");
		return (1);
	}

	if ((it = pkgdb_query_autoremove(db)) == NULL) {
		pkg_error_warn("can not query database");
		goto cleanup;
	}

	printf("Packages to be autoremoved: \n");
	while ((retcode = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK) {
		oldsize += pkg_flatsize(pkg);
		newsize += pkg_new_flatsize(pkg);
		printf("\t%s-%s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
	}
	printf("\n");
	pkgdb_it_free(it);

	if (oldsize > newsize) {
		newsize *= -1;
		humanize_number(size, sizeof(size), oldsize - newsize, "B", HN_AUTOSCALE, 0);
		printf("the autoremove will save %s\n", size);
	} else {
		humanize_number(size, sizeof(size), newsize - oldsize, "B", HN_AUTOSCALE, 0);
		printf("the autoremove will require %s more space\n", size);
	}

	if (query_yesno("Proceed (y|N): ")) {
		it = pkgdb_query_autoremove(db);
		while ((retcode = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK) {
			if (pkg_delete(pkg, db, 0) != EPKG_OK) {
				retcode++;
				pkg_error_warn("can not delete %s-%s", pkg_get(pkg, PKG_ORIGIN));
			}
		}

	} else {
		printf("Aborted\n");
	}

	if (pkgdb_compact(db) != EPKG_OK)
		pkg_error_warn("can not compact database");

	cleanup:
	pkg_free(pkg);
	pkgdb_close(db);

	return (retcode);
}
