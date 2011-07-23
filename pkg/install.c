#include <sys/types.h>

#include <err.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "install.h"

void
usage_install(void)
{
	fprintf(stderr, "usage: pkg install <pkg-name>\n");
	fprintf(stderr, "For more information see 'pkg help install'.\n");
}

int
exec_install(int argc, char **argv)
{
	struct pkg *pkg = NULL;
	struct pkgdb *db = NULL;
	struct pkg_jobs *jobs = NULL;
	struct pkg_jobs_entry *je = NULL;
	int retcode = EPKG_OK;
	int i;

	if (argc < 2) {
		usage_install();
		return (EX_USAGE);
	}

	if (geteuid() != 0) {
		warnx("installing packages can only be done as root");
		return (EX_NOPERM);
	}

	if (pkgdb_open(&db, PKGDB_REMOTE, "repo.sqlite") != EPKG_OK) {
		return (EX_IOERR);
	}


	/* create a jobs object */
	if (pkg_jobs_new(&jobs) != EPKG_OK) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	/* create a jobs entry */
	if (pkg_jobs_new_entry(jobs, &je, PKG_JOBS_INSTALL, db) != EPKG_OK) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	for (i = 1; i < argc; i++) {
		if ((pkg = pkgdb_query_remote(db, argv[i])) == NULL) {
			retcode = EPKG_FATAL;
			goto cleanup;
		}

		pkg_jobs_add(je, pkg);
	}

	/* print a summary before applying the jobs */
	pkg = NULL;
	printf("The following packages will be installed:\n");
	while (pkg_jobs_entry(je, &pkg) == EPKG_OK) {
		printf("%s-%s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
	}

	retcode = pkg_jobs_apply(je, 0);

	cleanup:
	pkgdb_close(db);
	pkg_jobs_free(jobs);

	return (retcode == EPKG_OK ? EX_OK : 1);
}

