#include <sys/types.h>

#include <err.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "utils.h"
#include "install.h"

void
usage_install(void)
{
	fprintf(stderr, "usage: pkg install [-ygxXf] <pkg-name> <...>\n");
	fprintf(stderr, "For more information see 'pkg help install'.\n");
}

int
exec_install(int argc, char **argv)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	struct pkgdb *db = NULL;
	struct pkg_jobs *jobs = NULL;
	int retcode = EPKG_OK;
	int i, ch, yes = 0;
	match_t match = MATCH_EXACT;

	while ((ch = getopt(argc, argv, "ygxXf")) != -1) {
		switch (ch) {
			case 'y':
				yes = 1;
				break;
			case 'g':
				match = MATCH_GLOB;
				break;
			case 'x':
				match = MATCH_REGEX;
				break;
			case 'X':
				match = MATCH_EREGEX;
				break;
			default:
				usage_install();
				return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
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

	for (i = 0; i < argc; i++) {
		if ((it = pkgdb_rquery(db, argv[i], match, REPO_SEARCH_NAME)) == NULL) {
			retcode = EPKG_FATAL;
			goto cleanup;
		}

		while (( retcode = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK) {
			pkg_jobs_add(jobs, pkgdb_query_remote(db, pkg_get(pkg, PKG_ORIGIN)));
		}

	}

	/* print a summary before applying the jobs */
	pkg = NULL;
	printf("The following packages will be installed:\n");
	while (pkg_jobs(jobs, &pkg) == EPKG_OK) {
		printf("\t%s-%s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
	}
 
	if (yes == 0)
		yes = query_yesno("\nProceed with installing packages [y/N]: ");

	if (yes == 1)
		retcode = pkg_jobs_apply(jobs, 0);

	cleanup:
	
	pkg_jobs_free(jobs);
	pkgdb_it_free(it);
	pkg_free(pkg);
	pkgdb_close(db);

	return (retcode == EPKG_OK ? EX_OK : 1);
}
