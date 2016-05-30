/*-
 * Copyright (c) 2012-2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2015 Baptiste Daroussin <bapt@FreeBSD.org>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "pkgcli.h"

enum action {
	LOCK,
	UNLOCK,
};

static int exec_lock_unlock(int, char**, enum action);
static int do_lock(struct pkgdb *db, struct pkg *pkg);
static int do_unlock(struct pkgdb *db, struct pkg *pkg);

void
usage_lock(void)
{
	fprintf(stderr, "Usage: pkg lock [-lqy] [-a|[-Cgix] <pkg-name>]\n");
	fprintf(stderr, "       pkg lock --has-locked-packages\n");
	fprintf(stderr, "       pkg unlock [-lqy] [-a|[-Cgix] <pkg-name>]\n");
	fprintf(stderr, "For more information see 'pkg help lock'.\n");
}

static int
do_lock(struct pkgdb *db, struct pkg *pkg)
{
	if (pkg_is_locked(pkg)) {
		if (!quiet)
			pkg_printf("%n-%v: already locked\n",
			       pkg, pkg);
		return (EPKG_OK);
	}

	if (!query_yesno(false, "%n-%v: lock this package? ",
				 pkg, pkg))
		return (EPKG_OK);

	if (!quiet)
		pkg_printf("Locking %n-%v\n", pkg, pkg);

	return (pkgdb_set(db, pkg, PKG_SET_LOCKED, (int)true));
}


static int
do_unlock(struct pkgdb *db, struct pkg *pkg)
{
	if (!pkg_is_locked(pkg)) {
		if (!quiet)
			pkg_printf("%n-%v: already unlocked\n", pkg, pkg);
		return (EPKG_OK);
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
    enum action action)
{
	struct pkgdb_it	*it = NULL;
	struct pkg	*pkg = NULL;
	int		 retcode;
	int		 exitcode = EX_OK;

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_EXCLUSIVE) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get an exclusive lock on database. "
		      "It is locked by another process");
		return (EX_TEMPFAIL);
	}

	if ((it = pkgdb_query(db, pkgname, match)) == NULL) {
		exitcode = EX_IOERR;
		goto cleanup;
	}

	while ((retcode = pkgdb_it_next(it, &pkg, 0)) == EPKG_OK) {
		if (action == LOCK)
			retcode = do_lock(db, pkg);
		else
			retcode = do_unlock(db, pkg);

		if (retcode != EPKG_OK) {
			exitcode = EX_IOERR;
			goto cleanup;
		}
	}

cleanup:
	pkg_free(pkg);
	pkgdb_it_free(it);

	pkgdb_release_lock(db, PKGDB_LOCK_EXCLUSIVE);

	return (exitcode);
}

int
exec_lock(int argc, char **argv)
{
	return (exec_lock_unlock(argc, argv, LOCK));
}

int
exec_unlock(int argc, char **argv)
{
	return (exec_lock_unlock(argc, argv, UNLOCK));
}

static int
list_locked(struct pkgdb *db, bool has_locked)
{
	struct pkgdb_it	*it = NULL;
	struct pkg	*pkg = NULL;
	bool		 gotone = false;

	if ((it = pkgdb_query(db, " where locked=1", MATCH_CONDITION)) == NULL) {
		pkgdb_close(db);
		return (EX_UNAVAILABLE);
	}

	if (!quiet && !has_locked)
		printf("Currently locked packages:\n");

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		gotone = true;
		if (has_locked)
			break;
		pkg_printf("%n-%v\n", pkg, pkg);
	}

	pkg_free(pkg);
	pkgdb_it_free(it);

	if (has_locked)
		return (gotone ? EXIT_SUCCESS : EXIT_FAILURE);

	return (EX_OK);
}

static int
exec_lock_unlock(int argc, char **argv, enum action action)
{
	struct pkgdb	*db = NULL;
	const char	*pkgname;
	int		 match = MATCH_EXACT;
	int		 retcode;
	int		 exitcode = EX_OK;
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
			return (EX_USAGE);
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
		return (EX_USAGE);
	}

	if (match == MATCH_ALL)
		pkgname = NULL;
	else
		pkgname = argv[0];

	if (read_only)
		retcode = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_LOCAL);
	else
		retcode = pkgdb_access(PKGDB_MODE_READ|PKGDB_MODE_WRITE,
			       PKGDB_DB_LOCAL);
	if (retcode == EPKG_ENODB) {
		if (match == MATCH_ALL)
			return (EX_OK);
		if (!quiet)
			warnx("No packages installed.  Nothing to do!");
		return (EX_OK);
	} else if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to modify the package database");
		return (EX_NOPERM);
	} else if (retcode != EPKG_OK) {
		warnx("Error accessing the package database");
		return (EX_SOFTWARE);
	}

	retcode = pkgdb_open(&db, PKGDB_DEFAULT);
	if (retcode != EPKG_OK)
		return (EX_IOERR);

	if (!read_only)
		exitcode = do_lock_unlock(db, match, pkgname, action);

	if (show_locked)
		exitcode = list_locked(db, has_locked_packages);

	pkgdb_close(db);

	return (exitcode);
}
