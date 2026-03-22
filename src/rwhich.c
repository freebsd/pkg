/*-
 * Copyright (c) 2025 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/param.h>

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pkg.h>
#include "pkgcli.h"

void
usage_rwhich(void)
{
	fprintf(stderr, "Usage: pkg rwhich [-gq] [-r reponame] <file>\n\n");
	fprintf(stderr, "For more information see 'pkg help rwhich'.\n");
}

int
exec_rwhich(int argc, char **argv)
{
	struct pkgdb	*db = NULL;
	struct pkgdb_it	*it = NULL;
	struct pkg	*pkg = NULL;
	int		 retcode = EXIT_FAILURE;
	int		 ch;
	bool		 glob = false;
	const char	*reponame = NULL;
	c_charv_t	 repos = vec_init();

	struct option longopts[] = {
		{ "glob",		no_argument,		NULL,	'g' },
		{ "quiet",		no_argument,		NULL,	'q' },
		{ "repository",		required_argument,	NULL,	'r' },
		{ NULL,			0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+gqr:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'g':
			glob = true;
			break;
		case 'q':
			quiet = true;
			break;
		case 'r':
			reponame = optarg;
			break;
		default:
			usage_rwhich();
			return (EXIT_FAILURE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		usage_rwhich();
		return (EXIT_FAILURE);
	}

	if (pkgdb_open_all(&db, PKGDB_REMOTE, reponame) != EPKG_OK)
		return (EXIT_FAILURE);

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database, "
		    "it is locked by another process");
		return (EXIT_FAILURE);
	}

	if (reponame != NULL)
		vec_push(&repos, reponame);

	while (argc >= 1) {
		if ((it = pkgdb_repo_which(db, argv[0], glob, &repos)) == NULL) {
			retcode = EXIT_FAILURE;
			goto cleanup;
		}

		pkg = NULL;
		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
			retcode = EXIT_SUCCESS;
			if (quiet)
				pkg_printf("%n-%v\n", pkg, pkg);
			else
				pkg_printf("%S is provided by package %n-%v\n",
				    argv[0], pkg, pkg);
		}
		if (retcode != EXIT_SUCCESS && !quiet)
			printf("%s was not found in the repository catalogue\n",
			    argv[0]);

		pkg_free(pkg);
		pkgdb_it_free(it);

		argc--;
		argv++;
	}

cleanup:
	vec_free(&repos);
	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	pkgdb_close(db);

	return (retcode);
}
