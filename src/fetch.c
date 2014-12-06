/*-
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2013-2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2012-2013 Bryan Drewery <bdrewery@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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

#include <sys/types.h>

#include <err.h>
#include <getopt.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_fetch(void)
{
	fprintf(stderr, "Usage: pkg fetch [-r reponame] [-o destdir] [-dqUy] "
					"[-Cgix] <pkg-name> <...>\n");
	fprintf(stderr, "       pkg fetch [-r reponame] [-dqUy] -a\n");
	fprintf(stderr, "       pkg fetch [-r reponame] [-dqUy] -u\n\n");
	fprintf(stderr, "For more information see 'pkg help fetch'.\n");
}

int
exec_fetch(int argc, char **argv)
{
	struct pkgdb	*db = NULL;
	struct pkg_jobs	*jobs = NULL;
	const char	*reponame = NULL;
	const char *destdir = NULL;
	int		 ch;
	int		 retcode = EX_SOFTWARE;
	bool		 upgrades_for_installed = false, rc, csum_only = false;
	unsigned	 mode;
	match_t		 match = MATCH_EXACT;
	pkg_flags	 f = PKG_FLAG_NONE;

	struct option longopts[] = {
		{ "all",		no_argument,		NULL,	'a' },
		{ "case-sensitive",	no_argument,		NULL,	'C' },
		{ "dependencies",	no_argument,		NULL,	'd' },
		{ "glob",		no_argument,		NULL,	'g' },
		{ "case-insensitive",	no_argument,		NULL,	'i' },
		{ "quiet",		no_argument,		NULL,	'q' },
		{ "repository",		required_argument,	NULL,	'r' },
		{ "available-updates",	no_argument,		NULL,	'u' },
		{ "no-repo-update",	no_argument,		NULL,	'U' },
		{ "regex",		no_argument,		NULL,	'x' },
		{ "yes",		no_argument,		NULL,	'y' },
		{ "output",		required_argument,	NULL,	'o' },
		{ NULL,			0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+aCdgiqr:Uuxyo:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
			break;
		case 'C':
			pkgdb_set_case_sensitivity(true);
			break;
		case 'd':
			f |= PKG_FLAG_WITH_DEPS | PKG_FLAG_RECURSIVE;
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
		case 'r':
			reponame = optarg;
			break;
		case 'u':
			f |= PKG_FLAG_UPGRADES_FOR_INSTALLED;
			upgrades_for_installed = true;
			break;
		case 'U':
			auto_update = false;
			break;
		case 'x':
			match = MATCH_REGEX;
			break;
		case 'y':
			yes = true;
			break;
		case 'o':
			f |= PKG_FLAG_FETCH_MIRROR;
			destdir = optarg;
			break;
		default:
			usage_fetch();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;
	
	if (argc < 1 && match != MATCH_ALL && !upgrades_for_installed) {
		usage_fetch();
		return (EX_USAGE);
	}

        if (match == MATCH_ALL && upgrades_for_installed) {
		usage_fetch();
		return (EX_USAGE);
	}

	if (auto_update)
		mode = PKGDB_MODE_READ|PKGDB_MODE_WRITE|PKGDB_MODE_CREATE;
	else
		mode = PKGDB_MODE_READ;

	retcode = pkgdb_access(mode, PKGDB_DB_REPO);

	if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to access repo catalogue");
		return (EX_NOPERM);
	} else if (retcode != EPKG_OK)
		return (EX_IOERR);

	if (upgrades_for_installed) {
		retcode = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_LOCAL);

		if (retcode == EPKG_ENOACCESS) {
			warnx("Insufficient privileges to access the package database");
			return (EX_NOPERM);
		} else if (retcode != EPKG_OK)
			return (EX_IOERR);
	}

	/* first update the remote repositories if needed */
	if (auto_update &&
	    (retcode = pkgcli_update(false, false, reponame)) != EPKG_OK)
		return (retcode);

	if (pkgdb_open_all(&db, PKGDB_REMOTE, reponame) != EPKG_OK)
		return (EX_IOERR);

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}


	if (pkg_jobs_new(&jobs, PKG_JOBS_FETCH, db) != EPKG_OK)
		goto cleanup;

	if (reponame != NULL && pkg_jobs_set_repository(jobs, reponame) != EPKG_OK)
		goto cleanup;

	if (destdir != NULL && pkg_jobs_set_destdir(jobs, destdir) != EPKG_OK)
		goto cleanup;

	pkg_jobs_set_flags(jobs, f);

	if (!upgrades_for_installed &&
	    pkg_jobs_add(jobs, match, argv, argc) != EPKG_OK)
		goto cleanup;

	if (pkg_jobs_solve(jobs) != EPKG_OK)
		goto cleanup;

	if (pkg_jobs_count(jobs) == 0)
		goto cleanup;

	if (!quiet) {
		rc = print_jobs_summary(jobs, "The following packages will be fetched:\n\n");

		if (rc != 0)
			rc = query_yesno(false, "\nProceed with fetching "
			    "packages? [y/N]: ");
		else {
			printf("No packages are required to be fetched.\n");
			rc = query_yesno(false, "Check the integrity of packages "
							"downloaded? [y/N]: ");
			csum_only = true;
		}
	}
	else {
		rc = true;
	}
	
	if (!rc || (retcode = pkg_jobs_apply(jobs)) != EPKG_OK)
		goto cleanup;

	if (csum_only && !quiet)
		printf("Integrity check was successful.\n");

	retcode = EX_OK;

cleanup:
	pkg_jobs_free(jobs);
	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	pkgdb_close(db);

	return (retcode);
}
