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
	struct pkg_repos_entry *re = NULL;
	int retcode = EPKG_OK;
	int i;
	int multi_repos = 0;

	if (argc < 2) {
		usage_install();
		return (EX_USAGE);
	}

	if (geteuid() != 0) {
		warnx("installing packages can only be done as root");
		return (EX_NOPERM);
	}

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK) {
		return (EX_IOERR);
	}

	if (pkg_jobs_new(&jobs, PKG_JOBS_INSTALL, db) != EPKG_OK) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	for (i = 1; i < argc; i++) {
		if ((pkg = pkgdb_query_remote(db, argv[i])) == NULL) {
			retcode = EPKG_FATAL;
			goto cleanup;
		}

		pkg_jobs_add(jobs, pkg);
	}

	/* print a summary before applying the jobs */
	pkg = NULL;
	printf("The following packages will be installed:\n");

	if ((strcmp(pkg_config("PKG_MULTIREPOS"), "true") == 0) && (pkg_config("PACKAGESITE") == NULL))
		multi_repos = 1;

	while (pkg_jobs(jobs, &pkg) == EPKG_OK) {
		printf("\t%s-%s", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
		
		if (multi_repos == 1) {
			re = NULL;
			pkg_repos_next(pkg, &re);
			printf(" [ from repository %s ]", pkg_repos_get_name(re));
		}

		printf("\n");
	}
	printf("\n");

	retcode = pkg_jobs_apply(jobs, 0);

	cleanup:
	
	pkg_jobs_free(jobs);
	pkgdb_close(db);

	return (retcode == EPKG_OK ? EX_OK : 1);
}
