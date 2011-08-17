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
	fprintf(stderr, "usage: pkg install [-ygxXf] <pkg-name> <...>\n\n");
	fprintf(stderr, "For more information see 'pkg help install'.\n");
}

int
exec_install(int argc, char **argv)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	struct pkgdb *db = NULL;
	struct pkg_jobs *jobs = NULL;
	int retcode = 1;
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
		goto cleanup;
	}

	for (i = 0; i < argc; i++) {
		if ((it = pkgdb_rquery(db, argv[i], match, FIELD_NAME)) == NULL) {
			goto cleanup;
		}

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_DEPS) == EPKG_OK) {
			pkg_jobs_add(jobs, pkg);
			pkg = NULL;
		}
		
		pkgdb_it_free(it);
	}

	if (pkg_jobs_isempty(jobs))
		goto cleanup;

	/* print a summary before applying the jobs */
	pkg = NULL;
	printf("The following packages will be installed:\n");
	while (pkg_jobs(jobs, &pkg) == EPKG_OK) {
		printf("\t%s-%s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
	}
 
	if (yes == 0)
		yes = query_yesno("\nProceed with installing packages [y/N]: ");

	if (yes == 1)
		if (pkg_jobs_apply(jobs, 0) != EPKG_OK)
			goto cleanup;

	retcode = 0;

	cleanup:
	pkg_jobs_free(jobs);
	pkgdb_close(db);

	return (retcode);
}
