/*-
 * Copyright (c) 2012 Matthew Seaman <matthew@FreeBSD.org>
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
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>
#include <libutil.h>

#include <pkg.h>

#include "pkgcli.h"

enum action {
	LOCK,
	UNLOCK,
};

static int exec_lock_unlock(int, char**, enum action);
static int do_lock(struct pkgdb *db, struct pkg *pkg);
static int do_unlock(struct pkgdb *db, struct pkg *pkg);

static bool yes = false;	/* Assume yes answer to questions */

void
usage_lock(void)
{
	fprintf(stderr, "Usage: pkg lock [-giqxy] <pkg-name>\n");
	fprintf(stderr, "       pkg lock [-qy] -a\n");
	fprintf(stderr, "       pkg unlock [-giqxy] <pkg-name>\n");
	fprintf(stderr, "       pkg unlock [-qy] -a\n");
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

	if (!yes && !query_yesno("%n-%v: lock this package? [y/N]: ",
				 pkg, pkg))
		return (EPKG_OK);

	if (!quiet)
		pkg_printf("Locking %n-%v\n", pkg, pkg);

	return (pkgdb_set(db, pkg, PKG_SET_LOCKED, (int64_t)true));
}


static int
do_unlock(struct pkgdb *db, struct pkg *pkg)
{
	if (!pkg_is_locked(pkg)) {
		if (!quiet)
			pkg_printf("%n-%v: already unlocked\n", pkg, pkg);
		return (EPKG_OK);
	}

	if (!yes && !query_yesno("%n-%v: unlock this package? [y/N]: ",
				 pkg, pkg))
		return (EPKG_OK);

	if (!quiet)
		pkg_printf("Unlocking %n-%v\n", pkg, pkg);

	return (pkgdb_set(db, pkg, PKG_SET_LOCKED, (int64_t)false));
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
exec_lock_unlock(int argc, char **argv, enum action action)
{
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *pkg = NULL;
	const char *pkgname;
	int match = MATCH_EXACT;
	int retcode;
	int exitcode = EX_OK;
	int ch;

	pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);

	while ((ch = getopt(argc, argv, "agiqxy")) != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'i':
			pkgdb_set_case_sensitivity(false);
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
		default:
			usage_lock();
			return (EX_USAGE);
		}
        }
	argc -= optind;
	argv += optind;

	if (!(match == MATCH_ALL && argc == 0) && argc != 1) {
		usage_lock();
		return (EX_USAGE);
	}

	if (match == MATCH_ALL)
		pkgname = NULL;
	else
		pkgname = argv[0];

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
	if (retcode != EPKG_END)
		exitcode = EX_IOERR;

cleanup:
	if (pkg != NULL)
		pkg_free(pkg);
	if (it != NULL)
		pkgdb_it_free(it);
	if (db != NULL)
		pkgdb_close(db);

	return (exitcode);
}
