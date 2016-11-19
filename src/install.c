/*-
 * Copyright (c) 2011-2016 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2013-2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2012-2013 Bryan Drewery <bdrewery@FreeBSD.org>
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
usage_install(void)
{
	fprintf(stderr,
	    "Usage: pkg install [-AfInFMqRUy] [-r reponame] [-Cgix] <pkg-name> ...\n\n");
	fprintf(stderr, "For more information see 'pkg help install'.\n");
}

int
exec_install(int argc, char **argv)
{
	struct pkgdb	*db = NULL;
	struct pkg_jobs	*jobs = NULL;
	const char	*reponame = NULL;
	int		 retcode;
	int		 updcode = EPKG_OK;
	int		 ch;
	int		 mode, repo_type;
	int		 done = 0;
	int		 lock_type = PKGDB_LOCK_ADVISORY;
	bool		 rc = true;
	bool		 local_only = false;
	match_t		 match = MATCH_EXACT;
	pkg_flags	 f = PKG_FLAG_NONE | PKG_FLAG_PKG_VERSION_TEST;

	struct option longopts[] = {
		{ "automatic",		no_argument,		NULL,	'A' },
		{ "case-sensitive",	no_argument,		NULL,	'C' },
		{ "force",		no_argument,		NULL,	'f' },
		{ "fetch-only",		no_argument,		NULL,	'F' },
		{ "glob",		no_argument,		NULL,	'g' },
		{ "case-insensitive",	no_argument,		NULL,	'i' },
		{ "no-install-scripts",	no_argument,		NULL,	'I' },
		{ "local-only",		no_argument,		NULL,	'l' },
		{ "ignore-missing",	no_argument,		NULL,	'M' },
		{ "dry-run",		no_argument,		NULL,	'n' },
		{ "quiet",		no_argument,		NULL,	'q' },
		{ "repository",		required_argument,	NULL,	'r' },
		{ "recursive",		no_argument,		NULL,   'R' },
		{ "no-repo-update",	no_argument,		NULL,	'U' },
		{ "regex",		no_argument,		NULL,	'x' },
		{ "yes",		no_argument,		NULL,	'y' },
		{ NULL,			0,			NULL,	0   },
	};

	nbactions = nbdone = 0;

	if (strcmp(argv[0], "add") == 0) {
		auto_update = false;
		local_only = true;
		yes = true;
		quiet = true;
	}

	while ((ch = getopt_long(argc, argv, "+ACfFgiIlMnqr:RUxy", longopts, NULL)) != -1) {
		switch (ch) {
		case 'A':
			f |= PKG_FLAG_AUTOMATIC;
			break;
		case 'C':
			pkgdb_set_case_sensitivity(true);
			break;
		case 'f':
			f |= PKG_FLAG_FORCE;
			break;
		case 'F':
			f |= PKG_FLAG_SKIP_INSTALL;
			lock_type = PKGDB_LOCK_READONLY;
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'i':
			pkgdb_set_case_sensitivity(false);
			break;
		case 'I':
			f |= PKG_FLAG_NOSCRIPT;
			break;
		case 'l':
			local_only = true;
			auto_update = false;
			break;
		case 'M':
			f |= PKG_FLAG_FORCE_MISSING;
			break;
		case 'n':
			f |= PKG_FLAG_DRY_RUN;
			lock_type = PKGDB_LOCK_READONLY;
			dry_run = true;
			break;
		case 'q':
			quiet = true;
			break;
		case 'r':
			reponame = optarg;
			break;
		case 'R':
			f |= PKG_FLAG_RECURSIVE;
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
		default:
			usage_install();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		usage_install();
		return (EX_USAGE);
	}

	if (dry_run && !auto_update)
		mode = PKGDB_MODE_READ;
	else
		mode =	PKGDB_MODE_READ  |
				PKGDB_MODE_WRITE |
				PKGDB_MODE_CREATE;
	if (local_only)
		repo_type = PKGDB_DB_LOCAL;
	else
		repo_type = PKGDB_DB_LOCAL|PKGDB_DB_REPO;

	retcode = pkgdb_access(mode, repo_type);

	if (retcode == EPKG_ENOACCESS && dry_run) {
		auto_update = false;
		retcode = pkgdb_access(PKGDB_MODE_READ,
				       repo_type);
	}

	if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to install packages");
		return (EX_NOPERM);
	} else if (retcode != EPKG_OK)
		return (EX_IOERR);
	else
		retcode = EX_SOFTWARE;

	/* first update the remote repositories if needed */
	if (auto_update && pkg_repos_total_count() > 0 &&
	    (updcode = pkgcli_update(false, false, reponame)) != EPKG_OK)
		return (updcode);

	if (pkgdb_open_all(&db,
	    local_only ? PKGDB_DEFAULT : PKGDB_MAYBE_REMOTE,
	    reponame) != EPKG_OK)
		return (EX_IOERR);

	if (pkgdb_obtain_lock(db, lock_type) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get an advisory lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	if (pkg_jobs_new(&jobs, PKG_JOBS_INSTALL, db) != EPKG_OK)
		goto cleanup;

	if (!local_only && reponame != NULL &&
			pkg_jobs_set_repository(jobs, reponame) != EPKG_OK)
		goto cleanup;

	pkg_jobs_set_flags(jobs, f);

	if (pkg_jobs_add(jobs, match, argv, argc) == EPKG_FATAL)
		goto cleanup;

	if (pkg_jobs_solve(jobs) != EPKG_OK)
		goto cleanup;

	while ((nbactions = pkg_jobs_count(jobs)) > 0) {
		rc = yes;
		/* print a summary before applying the jobs */
		if (!quiet || dry_run) {
			print_jobs_summary(jobs,
			    "The following %d package(s) will be affected (of %d checked):\n\n",
			    nbactions, pkg_jobs_total(jobs));

			if (!dry_run) {
				rc = query_yesno(false,
				    "\nProceed with this action? ");
			} else {
				rc = false;
			}
		}

		if (rc) {
			retcode = pkg_jobs_apply(jobs);
			done = 1;
			if (retcode == EPKG_CONFLICT) {
				printf("Conflicts with the existing packages "
				    "have been found.\nOne more solver "
				    "iteration is needed to resolve them.\n");
				continue;
			}
			else if (retcode != EPKG_OK)
				goto cleanup;
		} else {
		}

		if (messages != NULL) {
			printf("%s", utstring_body(messages));
		}
		break;
	}

	if (done == 0 && rc)
		printf("The most recent version of packages are already installed\n");

	if (rc)
		retcode = EX_OK;
	else
		retcode = EXIT_FAILURE;

cleanup:
	pkgdb_release_lock(db, lock_type);
	pkg_jobs_free(jobs);
	pkgdb_close(db);

	if (!dry_run)
		pkg_cache_full_clean();

	if (!rc && newpkgversion)
		newpkgversion = false;

	return (retcode);
}
