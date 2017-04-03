/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2013-2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2016 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_autoremove(void)
{
	fprintf(stderr, "Usage: pkg autoremove [-ynq]\n\n");
	fprintf(stderr, "For more information see 'pkg help autoremove'.\n");
}

int
exec_autoremove(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct pkg_jobs *jobs = NULL;
	int retcode = EX_OK;
	int ch;
	nbactions = nbdone = 0;
	pkg_flags f = PKG_FLAG_FORCE;
	bool rc = false;
	int lock_type = PKGDB_LOCK_ADVISORY;

	struct option longopts[] = {
		{ "dry-run",	no_argument,	NULL,	'n' },
		{ "quiet",	no_argument,	NULL,	'q' },
		{ "yes",	no_argument,	NULL,	'y' },
		{ NULL,		0,		NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+nqy", longopts, NULL)) != -1) {
		switch (ch) {
		case 'n':
			f |= PKG_FLAG_DRY_RUN;
			dry_run = true;
			lock_type = PKGDB_LOCK_READONLY;
			break;
		case 'q':
			quiet = true;
			break;
		case 'y':
			yes = true;
			break;
		default:
			break;
		}
	}
	argc -= optind;

	if (argc != 0) {
		usage_autoremove();
		return (EX_USAGE);
	}

	if (dry_run)
		retcode = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_LOCAL);
	else
		retcode = pkgdb_access(PKGDB_MODE_READ|PKGDB_MODE_WRITE,
				       PKGDB_DB_LOCAL);

	if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to autoremove packages");
		return (EX_NOPERM);
	} else if (retcode == EPKG_ENODB) {
		warnx("No packages installed.  Nothing to do!");
		return (EX_OK);
	} else if (retcode != EPKG_OK) {
		warnx("Error accessing the package database");
		return (EX_SOFTWARE);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		return (EX_IOERR);
	}

	if (pkgdb_obtain_lock(db, lock_type) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get an advisory lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}
	/* Always force packages to be removed */
	if (pkg_jobs_new(&jobs, PKG_JOBS_AUTOREMOVE, db) != EPKG_OK) {
		pkgdb_close(db);
		return (EX_IOERR);
	}

	pkg_jobs_set_flags(jobs, f);

	if ((retcode = pkg_jobs_solve(jobs)) != EPKG_OK) {
		retcode = EX_SOFTWARE;
		goto cleanup;
	}

	if ((nbactions = pkg_jobs_count(jobs)) == 0) {
		printf("Nothing to do.\n");
		goto cleanup;
	}

	if (!quiet || dry_run) {
		print_jobs_summary(jobs,
				"Deinstallation has been requested for the following %d packages:\n\n", nbactions);
		if (!dry_run)
			rc = query_yesno(false,
		            "\nProceed with deinstalling packages? ");
	}
	if ((yes || rc ) && !dry_run && ((retcode = pkg_jobs_apply(jobs)) != EPKG_OK)) {
		goto cleanup;
	}

	if (!yes && !rc)
		retcode = EXIT_FAILURE;

	pkgdb_compact(db);

cleanup:
	pkg_jobs_free(jobs);
	pkgdb_release_lock(db, lock_type);
	pkgdb_close(db);

	return (retcode);
}
