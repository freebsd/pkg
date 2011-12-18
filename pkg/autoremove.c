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

#include "utils.h"
#include "autoremove.h"

void
usage_autoremove(void)
{
	fprintf(stderr, "usage pkg autoremove [-y]\n\n");
	fprintf(stderr, "For more information see 'pkg help autoremove'.\n");
}

int
exec_autoremove(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct pkgdb_it *it;
	struct pkg *pkg = NULL;
	struct pkg_jobs *jobs = NULL;
	int retcode = EPKG_OK;
	int64_t oldsize = 0, newsize = 0;
	char size[7];
	int ch;
	bool yes = false;

	while ((ch = getopt(argc, argv, "y")) != -1) {
		switch (ch) {
			case 'y':
				yes = true;
				break;
			default:
				break;
		}
        }
	argc -= optind;
	argv += optind;

	(void) argv;
	if (argc != 0) {
		usage_autoremove();
		return (EX_USAGE);
	}

	if (geteuid() != 0) {
		warnx("autoremove can only be done as root");
		return (EX_NOPERM);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		return (EX_IOERR);
	}

	if (pkg_jobs_new(&jobs, PKG_JOBS_DEINSTALL, db) != EPKG_OK) {
		pkgdb_close(db);
		return (EPKG_FATAL);
	}

	if ((it = pkgdb_query_autoremove(db)) == NULL) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	while ((retcode = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK) {
		oldsize += pkg_flatsize(pkg);
		newsize += pkg_new_flatsize(pkg);
		pkg_jobs_add(jobs, pkg);
		pkg = NULL;
	}

	if (oldsize > newsize) {
		newsize *= -1;
		humanize_number(size, sizeof(size), oldsize - newsize, "B", HN_AUTOSCALE, 0);
	} else {
		humanize_number(size, sizeof(size), newsize - oldsize, "B", HN_AUTOSCALE, 0);
	}

	if (pkg_jobs_is_empty(jobs)) {
		printf("Nothing to do\n");
		retcode = 0;
		goto cleanup;
	}

	pkg = NULL;
	printf("Packages to be autoremoved: \n");
	while (pkg_jobs(jobs, &pkg) == EPKG_OK)
		printf("\t%s-%s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));

	if (oldsize > newsize)
		printf("\nThe autoremove will save %s\n", size);
	else
		printf("\nThe autoremove will require %s more space\n", size);

	if (yes == false)
		pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);
	if (yes == false)
		yes = query_yesno("\nProceed with autoremove of packages [y/N]: ");

	if (yes == true) {
		if ((retcode = pkg_jobs_apply(jobs, 1)) != EPKG_OK)
			goto cleanup;
	}

	if (pkgdb_compact(db) != EPKG_OK) { 
		retcode = EPKG_FATAL;
	}

	cleanup:
	pkg_free(pkg);
	pkg_jobs_free(jobs);
	pkgdb_it_free(it);
	pkgdb_close(db);

	return (retcode);
}
