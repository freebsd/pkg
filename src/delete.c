/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2013-2014 Matthew Seaman <matthew@FreeBSD.org>
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
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_delete(void)
{
	fprintf(stderr, "Usage: pkg delete [-DfnqRy] [-Cgix] <pkg-name> ...\n");
	fprintf(stderr, "       pkg delete [-Dnqy] -a\n\n");
	fprintf(stderr, "For more information see 'pkg help delete'.\n");
}

int
exec_delete(int argc, char **argv)
{
	struct pkg_jobs	*jobs = NULL;
	struct pkgdb	*db = NULL;
	match_t		 match = MATCH_EXACT;
	pkg_flags	 f = PKG_FLAG_NONE;
	bool		 recursive_flag = false, rc = false;
	int		 retcode = EX_SOFTWARE;
	int		 ch;
	int		 i;
	int		 lock_type = PKGDB_LOCK_ADVISORY;

	struct option longopts[] = {
		{ "all",			no_argument,	NULL,	'a' },
		{ "case-sensitive",		no_argument,	NULL,	'C' },
		{ "no-deinstall-script",	no_argument,	NULL,	'D' },
		{ "force",			no_argument,	NULL,	'f' },
		{ "glob",			no_argument,	NULL,	'g' },
		{ "case-insensitive",		no_argument,	NULL,	'i' },
		{ "dry-run",			no_argument,	NULL,	'n' },
		{ "quiet",			no_argument,	NULL,	'q' },
		{ "recursive",			no_argument,	NULL,	'R' },
		{ "regex",			no_argument,	NULL,	'x' },
		{ "yes",			no_argument,	NULL,	'y' },
		{ NULL,				0,		NULL,	0   },
	};

	nbactions = nbdone = 0;

	while ((ch = getopt_long(argc, argv, "+aCDfginqRxy", longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
			break;
		case 'C':
			pkgdb_set_case_sensitivity(true);
			break;
		case 'D':
			f |= PKG_FLAG_NOSCRIPT;
			break;
		case 'f':
			f |= PKG_FLAG_FORCE;
			force = true;
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'i':
			pkgdb_set_case_sensitivity(false);
			break;
		case 'n':
			f |= PKG_FLAG_DRY_RUN;
			lock_type = PKGDB_LOCK_READONLY;
			dry_run = true;
			break;
		case 'q':
			quiet = true;
			break;
		case 'R':
			recursive_flag = true;
			break;
		case 'x':
			match = MATCH_REGEX;
			break;
		case 'y':
			yes = true;
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

	if (dry_run)
		retcode = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_LOCAL);
	else
		retcode = pkgdb_access(PKGDB_MODE_READ|PKGDB_MODE_WRITE,
				       PKGDB_DB_LOCAL);

	if (retcode == EPKG_ENODB) {
		warnx("No packages installed.  Nothing to do!");
		return (EX_OK);
	} else if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to delete packages");
		return (EX_NOPERM);
	} else if (retcode != EPKG_OK) {
		warnx("Error accessing the package database");
		return (EX_SOFTWARE);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (EX_IOERR);

	if (pkgdb_obtain_lock(db, lock_type) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get an advisory lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}


	if (pkg_jobs_new(&jobs, PKG_JOBS_DEINSTALL, db) != EPKG_OK) {
		pkgdb_close(db);
		return (EX_IOERR);
	}

	/*
	 * By default delete packages recursively.
	 * If force mode is enabled then we try to remove packages non-recursively.
	 * However, if -f and -R flags are both enabled then we return to
	 * recursive deletion.
	 */
	if (!force || recursive_flag)
		f |= PKG_FLAG_RECURSIVE;

	pkg_jobs_set_flags(jobs, f);

	if (match == MATCH_EXACT) {
		for (i = 0; i < argc; i++) {
			if (strchr(argv[i], '*') != NULL) {
				match = MATCH_GLOB;
				break;
			}
		}
	}

	if (pkg_jobs_add(jobs, match, argv, argc) == EPKG_FATAL)
		goto cleanup;

	if (pkg_jobs_solve(jobs) != EPKG_OK) {
		fprintf(stderr, "Cannot perform request\n");
		retcode = EX_NOPERM;
		goto cleanup;
	}

	/* check if we have something to deinstall */
	if ((nbactions = pkg_jobs_count(jobs)) == 0) {
		if (argc == 0) {
			if (!quiet)
				printf("Nothing to do.\n");
			retcode = EX_OK;
		} else {
			fprintf(stderr, "Package(s) not found!\n");
			retcode = EX_DATAERR;
		}
		goto cleanup;
	}

	if (!quiet || dry_run) {
		if (!quiet) {
			print_jobs_summary(jobs,
				"Deinstallation has been requested for the following %d packages "
				"(of %d packages in the universe):\n\n", nbactions,
				pkg_jobs_total(jobs));
		}
		if (dry_run) {
			retcode = EX_OK;
			goto cleanup;
		}
		rc = query_yesno(false,
		            "\nProceed with deinstalling packages? [y/N]: ");
	}
	else
		rc = yes;

	if (!rc || (retcode = pkg_jobs_apply(jobs)) != EPKG_OK)
		goto cleanup;

	pkgdb_compact(db);

	retcode = EX_OK;

cleanup:
	pkgdb_release_lock(db, lock_type);
	pkg_jobs_free(jobs);
	pkgdb_close(db);

	return (retcode);
}
