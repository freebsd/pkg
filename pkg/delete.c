#include <sys/types.h>

#include <err.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>
#include <libutil.h>
#include <string.h>

#include <pkg.h>

#include "utils.h"
#include "delete.h"

void
usage_delete(void)
{
	fprintf(stderr, "usage: pkg delete [-ygxXfr] <pkg-name> <...>\n");
	fprintf(stderr, "       pkg delete [-y] -a\n\n");
	fprintf(stderr, "For more information see 'pkg help delete'.\n");
}

int
exec_delete(int argc, char **argv)
{
	struct pkg_jobs *jobs = NULL;
	struct pkg *pkg = NULL;
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	match_t match = MATCH_EXACT;
	int ch;
	int flags = PKG_LOAD_BASIC;
	int force = 0;
	int yes = 0;
	int retcode = 1;
	int recursive = 0;
	int64_t oldsize = 0, newsize = 0;
	char size[7];
	const char *assume_yes = NULL;

	while ((ch = getopt(argc, argv, "agxXfyr")) != -1) {
		switch (ch) {
			case 'a':
				match = MATCH_ALL;
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
			case 'f':
				force = 1;
				break;
			case 'y':
				yes = 1;
				break;
			case 'r':
				recursive = 1;
				break;
			default:
				usage_delete();
				return (EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1 && match != MATCH_ALL) {
		usage_delete();
		return (EX_USAGE);
	}

	if (geteuid() != 0) {
		warnx("deleting packages can only be done as root");
		return (EX_NOPERM);
	}
	
	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		return (EPKG_FATAL);
	}

	if (pkg_jobs_new(&jobs, PKG_JOBS_DEINSTALL, db) != EPKG_OK) {
		pkgdb_close(db);
		return (EPKG_FATAL);
	}

	if ((it = pkgdb_query_delete(db, match, argc, argv, recursive)) == NULL)
		goto cleanup;

	while (pkgdb_it_next(it, &pkg, flags) == EPKG_OK) {
		oldsize += pkg_flatsize(pkg);
		newsize += pkg_new_flatsize(pkg);
		pkg_jobs_add(jobs, pkg);
		pkg = NULL;
	}

	/* check if we have something to deinstall */
	if (pkg_jobs_is_empty(jobs)) {
		printf("Nothing to do\n");
		retcode = 0;
		goto cleanup;
	}

	if (oldsize > newsize) {
		newsize *= -1;
		humanize_number(size, sizeof(size), oldsize - newsize, "B", HN_AUTOSCALE, 0);
	} else {
		humanize_number(size, sizeof(size), newsize - oldsize, "B", HN_AUTOSCALE, 0);
	}

	pkg = NULL;
	printf("The following packages will be deinstalled:\n");
	while (pkg_jobs(jobs, &pkg) == EPKG_OK)
		printf("\t%s-%s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));

	if (oldsize > newsize)
		printf("\nThe deinstallation will save %s\n", size);
	else
		printf("\nThe deinstallation will require %s more space\n", size);

	assume_yes = pkg_config("ASSUME_ALWAYS_YES");
	if (assume_yes && (strcasecmp(assume_yes, "yes") == 0))
	    yes = 1;

	if (yes == 0)
		yes = query_yesno("\nProceed with deinstalling packages [y/N]: ");

	if (yes == 1) {
		if ((retcode = pkg_jobs_apply(jobs, force)) != EPKG_OK)
			goto cleanup;
	} else
		goto cleanup;

	pkgdb_compact(db);

	retcode = 0;

	cleanup:
	pkgdb_it_free(it);
	pkgdb_close(db);
	pkg_jobs_free(jobs);

	return (retcode);
}
