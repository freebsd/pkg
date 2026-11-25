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

#define LOCK_COMMENT_KEY "lock_comment"

static int exec_lock_unlock(int, char**,
    int (*lockfct)(struct pkgdb *, struct pkg *, bool batch, const char *),
    bool allow_comment);
static int do_lock(struct pkgdb *db, struct pkg *pkg, bool batch,
    const char *comment);
static int do_unlock(struct pkgdb *db, struct pkg *pkg, bool batch,
    __unused const char *comment);

void
usage_lock(void)
{
	fprintf(stderr,
	    "Usage: pkg lock [-lqy] [-c string] [-a|[-Cgix] <pkg-name>]\n");
	fprintf(stderr, "       pkg lock --has-locked-packages\n");
	fprintf(stderr, "       pkg unlock [-lqy] [-a|[-Cgix] <pkg-name>]\n");
	fprintf(stderr, "For more information see 'pkg help lock'.\n");
}

static char *
get_lock_comment(struct pkgdb *db, struct pkg *pkg)
{
	const char *name;
	char *comment = NULL;

	pkg_get(pkg, PKG_ATTR_NAME, &name);
	pkgdb_local_get(db, name, LOCK_COMMENT_KEY, &comment);

	return (comment);
}

static int
set_lock_comment(struct pkgdb *db, struct pkg *pkg, const char *comment)
{
	const char *name;
	char *cur = NULL;

	if (comment == NULL)
		return (EPKG_OK);

	pkg_get(pkg, PKG_ATTR_NAME, &name);
	pkgdb_local_get(db, name, LOCK_COMMENT_KEY, &cur);
	if (cur != NULL)
		free(cur);

	/* An empty comment removes the existing one. */
	return (pkgdb_local_set(db, name, LOCK_COMMENT_KEY,
	    comment[0] == '\0' ? NULL : comment));
}

static int
do_lock(struct pkgdb *db, struct pkg *pkg, bool batch, const char *comment)
{
	int ret;

	if (pkg_is_locked(pkg)) {
		if (comment == NULL) {
			if (batch)
				return (EPKG_OK);
			if (!quiet)
				pkg_printf("%n-%v: already locked\n",
				       pkg, pkg);
			return (EPKG_OK);
		}
		if (!quiet)
			pkg_printf("%n-%v: already locked\n", pkg, pkg);
		return (set_lock_comment(db, pkg, comment));
	}

	if (!query_yesno(false, "%n-%v: lock this package? ",
				 pkg, pkg))
		return (EPKG_OK);

	if (!quiet)
		pkg_printf("Locking %n-%v\n", pkg, pkg);

	if ((ret = pkgdb_set(db, pkg, PKG_SET_LOCKED, (int)true)) != EPKG_OK)
		return (ret);

	return (set_lock_comment(db, pkg, comment));
}


static int
do_unlock(struct pkgdb *db, struct pkg *pkg, bool batch,
    __unused const char *comment)
{
	const char *name;
	char *cur;
	int ret;

	if (!pkg_is_locked(pkg)) {
		if (batch)
			return (EPKG_OK);
		if (!quiet)
			pkg_printf("%n-%v: already unlocked\n", pkg, pkg);
		return (EPKG_OK);
	}

	if (!query_yesno(false, "%n-%v: unlock this package? ",
				 pkg, pkg))
		return (EPKG_OK);

	if (!quiet)
		pkg_printf("Unlocking %n-%v\n", pkg, pkg);

	if ((ret = pkgdb_set(db, pkg, PKG_SET_LOCKED, (int)false)) != EPKG_OK)
		return (ret);

	cur = get_lock_comment(db, pkg);
	if (cur != NULL) {
		free(cur);
		pkg_get(pkg, PKG_ATTR_NAME, &name);
		return (pkgdb_local_set(db, name, LOCK_COMMENT_KEY, NULL));
	}

	return (ret);
}

static int
do_lock_unlock(struct pkgdb *db, int match, const char *pkgname,
    int (*lockfct)(struct pkgdb *, struct pkg *, bool, const char *),
    const char *comment)
{
	struct pkgdb_it	*it = NULL;
	struct pkg	*pkg = NULL;
	int		 retcode;
	int		 exitcode = EXIT_SUCCESS;
	bool		 gotone = false;
	vec_t(struct pkg *)pkgs = vec_init();

	if (!pkgdb_lock_or_fail(db, PKGDB_LOCK_EXCLUSIVE))
		return (EXIT_FAILURE);

	if ((it = pkgdb_query(db, pkgname, match)) == NULL) {
		exitcode = EXIT_FAILURE;
		goto cleanup;
	}

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		gotone = true;
		vec_push(&pkgs, pkg);
		pkg = NULL;
	}
	vec_foreach(pkgs, i) {
		retcode = lockfct(db, pkgs.d[i], match != MATCH_EXACT,
		    comment);
		if (retcode != EPKG_OK) {
			exitcode = EXIT_FAILURE;
			goto cleanup;
		}
	}

	/* No package was found matching that name. */
	if (!gotone)
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
	return (exec_lock_unlock(argc, argv, do_lock, true));
}

int
exec_unlock(int argc, char **argv)
{
	return (exec_lock_unlock(argc, argv, do_unlock, false));
}

static int
list_locked(struct pkgdb *db, bool has_locked)
{
	struct pkgdb_it	*it = NULL;
	struct pkg	*pkg = NULL;
	char		*comment;
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
		if ((comment = get_lock_comment(db, pkg)) != NULL) {
			printf("\t%s\n", comment);
			free(comment);
		}
	}

	if (!gotone && !quiet && !has_locked)
		printf("No locked packages were found\n");

	pkg_free(pkg);
	pkgdb_it_free(it);

	return (gotone ? EXIT_SUCCESS : EXIT_FAILURE);
}

static int
exec_lock_unlock(int argc, char **argv,
    int (*lockfct)(struct pkgdb *, struct pkg *, bool, const char *),
    bool allow_comment)
{
	struct pkgdb	*db = NULL;
	int		 match = MATCH_EXACT;
	int		 retcode, i;
	int		 exitcode = EXIT_SUCCESS;
	int		 ch;
	const char	*comment = NULL;
	bool		 show_locked = false;
	bool		 read_only = false;
	bool		 has_locked_packages = false;

	struct option longopts[] = {
		{ "all",		no_argument,	NULL,	'a' },
		{ "case-sensitive",	no_argument,	NULL,	'C' },
		{ "comment",		required_argument,	NULL,	'c' },
		{ "glob",		no_argument,	NULL,	'g' },
		{ "show-locked",	no_argument,	NULL,	'l' },
		{ "quiet",		no_argument,	NULL,	'q' },
		{ "regex",		no_argument,	NULL,	'x' },
		{ "yes",		no_argument,	NULL,	'y' },
		{ "has-locked-packages",no_argument,	NULL,	1 },
		{ NULL,		0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+aCc:gilqxy", longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
			break;
		case 'C':
			pkgdb_set_case_sensitivity(true);
			break;
		case 'c':
			if (!allow_comment) {
				usage_lock();
				return (EXIT_FAILURE);
			}
			comment = optarg;
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
			exitcode = do_lock_unlock(db, match, NULL, lockfct,
			    comment);
		} else {
			for (i = 0; i < argc; i++) {
				exitcode = do_lock_unlock(db, match, argv[i],
				    lockfct, comment);
			}
		}
	}

	if (show_locked)
		exitcode = list_locked(db, has_locked_packages);

	pkgdb_close(db);

	return (exitcode);
}
