/*-
 * Copyright (c) 2012-2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2015-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <pkg.h>

#include "pkgcli.h"

static int exec_lock_unlock(int, char**, int (*lockfct)(struct pkgdb *, struct pkg *, bool batch));
static int do_lock(struct pkgdb *db, struct pkg *pkg, bool batch);
static int do_unlock(struct pkgdb *db, struct pkg *pkg, bool batch);

void
usage_lock(void)
{
	fprintf(stderr, "Usage: pkg lock [-lqy] [-a|[-Cgix] <pkg-name>]\n");
	fprintf(stderr, "       pkg lock --has-locked-packages\n");
	fprintf(stderr, "       pkg unlock [-lqy] [-a|[-Cgix] <pkg-name>]\n");
	fprintf(stderr, "For more information see 'pkg help lock'.\n");
}

static int
do_lock(struct pkgdb *db, struct pkg *pkg, bool batch)
{
	if (pkg_is_locked(pkg)) {
		if (batch)
			return (EPKG_OK);
		if (!quiet)
			pkg_printf("%n-%v: already locked\n",
			       pkg, pkg);
		return (EPKG_FATAL);
	}

	if (!query_yesno(false, "%n-%v: lock this package? ",
				 pkg, pkg))
		return (EPKG_OK);

	if (!quiet)
		pkg_printf("Locking %n-%v\n", pkg, pkg);

	return (pkgdb_set(db, pkg, PKG_SET_LOCKED, (int)true));
}


static int
do_unlock(struct pkgdb *db, struct pkg *pkg, bool batch)
{
	if (!pkg_is_locked(pkg)) {
		if (batch)
			return (EPKG_OK);
		if (!quiet)
			pkg_printf("%n-%v: already unlocked\n", pkg, pkg);
		return (EPKG_FATAL);
	}

	if (!query_yesno(false, "%n-%v: unlock this package? ",
				 pkg, pkg))
		return (EPKG_OK);

	if (!quiet)
		pkg_printf("Unlocking %n-%v\n", pkg, pkg);

	return (pkgdb_set(db, pkg, PKG_SET_LOCKED, (int)false));
}

static int
do_lock_unlock(struct pkgdb *db, int match, const char *pkgname,
    int (*lockfct)(struct pkgdb *, struct pkg *, bool))
{
	struct pkgdb_it	*it = NULL;
	struct pkg	*pkg = NULL;
	int		 retcode;
	int		 exitcode = EXIT_SUCCESS;
	bool		 gotone = false;
	vec_t(struct pkg *)pkgs = vec_init();

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_EXCLUSIVE) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get an exclusive lock on database. "
		      "It is locked by another process");
		return (EXIT_FAILURE);
	}

	if ((it = pkgdb_query(db, pkgname, match)) == NULL) {
		exitcode = EXIT_FAILURE;
		goto cleanup;
	}

	while (pkgdb_it_next(it, &pkg, 0) == EPKG_OK) {
		gotone = true;
		vec_push(&pkgs, pkg);
		pkg = NULL;
	}
	vec_foreach(pkgs, i) {
		retcode = lockfct(db, pkgs.d[i], match != MATCH_EXACT);
		if (retcode != EPKG_OK) {
			exitcode = EXIT_FAILURE;
			goto cleanup;
		}
	}

	/* No package was found matching that name. */
        if (gotone == false)
	        exitcode = EXIT_FAILURE;

cleanup:
	vec_free_and_free(&pkgs, pkg_free);
	pkgdb_it_free(it);

	pkgdb_release_lock(db, PKGDB_LOCK_EXCLUSIVE);

	return (exitcode);
}

int
exec_lock(int argc, char **argv)
{
	return (exec_lock_unlock(argc, argv, do_lock));
}

int
exec_unlock(int argc, char **argv)
{
	return (exec_lock_unlock(argc, argv, do_unlock));
}

static int
list_locked(struct pkgdb *db, bool has_locked)
{
	struct pkgdb_it	*it = NULL;
	struct pkg	*pkg = NULL;
	bool		 gotone = false;

	if ((it = pkgdb_query_cond(db, " WHERE locked=1", NULL, MATCH_ALL)) == NULL) {
		pkgdb_close(db);
		return (EXIT_FAILURE);
	}

        while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		if (!gotone) {
			gotone = true;
			if (has_locked) {
				break;
			} else {
				if (!quiet) {
					printf("Currently locked packages:\n");
				}
			}
		}
		pkg_printf("%n-%v\n", pkg, pkg);
	}

	if (!gotone && !quiet && !has_locked)
		printf("No locked packages were found\n");

	pkg_free(pkg);
	pkgdb_it_free(it);

	return (gotone ? EXIT_SUCCESS : EXIT_FAILURE);
}

static int
exec_lock_unlock(int argc, char **argv, int (*lockfct)(struct pkgdb *, struct pkg *, bool))
{
	struct pkgdb	*db = NULL;
	int		 match = MATCH_EXACT;
	int		 retcode, i;
	int		 exitcode = EXIT_SUCCESS;
	int		 ch;
	bool		 show_locked = false;
	bool		 read_only = false;
	bool		 has_locked_packages = false;

	struct option longopts[] = {
		{ "all",		no_argument,	NULL,	'a' },
		{ "case-sensitive",	no_argument,	NULL,	'C' },
		{ "glob",		no_argument,	NULL,	'g' },
		{ "show-locked",	no_argument,	NULL,	'l' },
		{ "quiet",		no_argument,	NULL,	'q' },
		{ "regex",		no_argument,	NULL,	'x' },
		{ "yes",		no_argument,	NULL,	'y' },
		{ "has-locked-packages",no_argument,	NULL,	1 },
		{ NULL,		0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+aCgilqxy", longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
			break;
		case 'C':
			pkgdb_set_case_sensitivity(true);
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'i':
			pkgdb_set_case_sensitivity(false);
			break;
		case 'l':
			show_locked = true;
			break;
		case 'q':
			quiet = true;
			break;
		case 'x':
			match = MATCH_REGEX;
			break;
		case 'y':
			yes = true;
			break;
		case 1:
			show_locked = true;
			has_locked_packages = true;
			break;
		default:
			usage_lock();
			return (EXIT_FAILURE);
		}
        }
	argc -= optind;
	argv += optind;

	/* Allow 'pkg lock -l' (or 'pkg unlock -l') without any
	 * package arguments to just display what packages are
	 * currently locked.  In this case, we only need a read_only
	 * connection to the DB. */

	if (show_locked && match != MATCH_ALL && argc == 0)
		read_only = true;

	if (!show_locked && match != MATCH_ALL && argc == 0) {
		usage_lock();
		return (EXIT_FAILURE);
	}

	if (read_only)
		retcode = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_LOCAL);
	else
		retcode = pkgdb_access(PKGDB_MODE_READ|PKGDB_MODE_WRITE,
			       PKGDB_DB_LOCAL);
	if (retcode == EPKG_ENODB) {
		if (match == MATCH_ALL)
			return (EXIT_SUCCESS);
		if (!quiet)
			warnx("No packages installed.  Nothing to do!");
		return (EXIT_SUCCESS);
	} else if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to modify the package database");
		return (EXIT_FAILURE);
	} else if (retcode != EPKG_OK) {
		warnx("Error accessing the package database");
		return (EXIT_FAILURE);
	}

	retcode = pkgdb_open(&db, PKGDB_DEFAULT);
	if (retcode != EPKG_OK)
		return (EXIT_FAILURE);

	if (!read_only) {
		if (match == MATCH_ALL) {
			exitcode = do_lock_unlock(db, match, NULL, lockfct);
		} else {
			for (i = 0; i < argc; i++) {
				exitcode = do_lock_unlock(db, match, argv[i], lockfct);
			}
		}
	}

	if (show_locked)
		exitcode = list_locked(db, has_locked_packages);

	pkgdb_close(db);

	return (exitcode);
}
