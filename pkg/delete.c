#include <sys/types.h>

#include <err.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "delete.h"

void
usage_delete(void)
{
	fprintf(stderr, "usage: pkg delete [-f] <pkg-name>\n");
	fprintf(stderr, "       pkg delete -a\n\n");
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
	char *origin = NULL;
	int ch;
	int flags = PKG_LOAD_BASIC;
	int force = 0;
	int retcode = EPKG_OK;

	while ((ch = getopt(argc, argv, "af")) != -1) {
		switch (ch) {
			case 'a':
				match = MATCH_ALL;
				break;
			case 'f':
				force = 1;
				break;
			default:
				usage_delete();
				return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1 && match == MATCH_EXACT) {
		usage_delete();
		return (EX_USAGE);
	}

	if (geteuid() != 0) {
		warnx("deleting packages can only be done as root");
		return (EX_NOPERM);
	}
	
	if ((retcode = pkgdb_open(&db, PKGDB_DEFAULT, "local.sqlite")) != EPKG_OK) {
		goto cleanup;
	}

	if (argc == 1)
		origin = argv[0];

	if ((retcode = pkg_jobs_new(&jobs, PKG_JOBS_DEINSTALL, db)) != EPKG_OK) {
		goto cleanup;
	}

	if ((it = pkgdb_query(db, origin, match)) == NULL) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	while ((retcode = pkgdb_it_next(it, &pkg, flags)) == EPKG_OK) {
		pkg_jobs_add(jobs, pkg);
		pkg = NULL;
	}

	if (retcode != EPKG_END) {
		goto cleanup;
	}

	if ((retcode = pkg_jobs_apply(jobs, force)) != EPKG_OK) {
		goto cleanup;
	}

	retcode = pkgdb_compact(db);

	cleanup:
	pkgdb_it_free(it);
	pkgdb_close(db);
	pkg_jobs_free(jobs);

	return (retcode == EPKG_OK ? EX_OK : 1);
}
