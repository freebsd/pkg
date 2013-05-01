/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2012-2013 Bryan Drewery <bdrewery@FreeBSD.org>
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
#include <inttypes.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>
#include <libutil.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_upgrade(void)
{
	fprintf(stderr, "usage: pkg upgrade [-fInFqUy] [-r reponame]\n\n");
	fprintf(stderr, "For more information see 'pkg help upgrade'.\n");
}

int
exec_upgrade(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct pkg_jobs *jobs = NULL;
	const char *reponame = NULL;
	int retcode;
	int updcode;
	int ch;
	bool yes;
	bool dry_run = false;
	bool auto_update;
	nbactions = nbdone = 0;
	pkg_flags f = PKG_FLAG_NONE | PKG_FLAG_PKG_VERSION_TEST;

	pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);
	pkg_config_bool(PKG_CONFIG_REPO_AUTOUPDATE, &auto_update);


	while ((ch = getopt(argc, argv, "fLnqFr:Uy")) != -1) {
		switch (ch) {
		case 'f':
			f |= PKG_FLAG_FORCE;
			break;
		case 'I':
			f |= PKG_FLAG_NOSCRIPT;
			break;
		case 'L':
			warnx("!!! The -L flag is deprecated and will be removed. Please use -U now.");
			/* FALLTHROUGH */
		case 'U':
			auto_update = false;
			break;
		case 'n':
			f |= PKG_FLAG_DRY_RUN;
			dry_run = true;
			break;
		case 'F':
			f |= PKG_FLAG_SKIP_INSTALL;
			break;
		case 'q':
			quiet = true;
			break;
		case 'r':
			reponame = optarg;
			break;
		case 'y':
			yes = true;
			break;
		default:
			usage_upgrade();
			return (EX_USAGE);
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 0) {
		usage_upgrade();
		return (EX_USAGE);
	}

	if (dry_run)
		retcode = pkgdb_access(PKGDB_MODE_READ,
				       PKGDB_DB_LOCAL|PKGDB_DB_REPO);
	else
		retcode = pkgdb_access(PKGDB_MODE_READ  |
				       PKGDB_MODE_WRITE |
				       PKGDB_MODE_CREATE,
				       PKGDB_DB_LOCAL|PKGDB_DB_REPO);

	if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privilege to upgrade packages");
		return (EX_NOPERM);
	} else if (retcode != EPKG_OK)
		return (EX_IOERR);
	else
		retcode = EX_SOFTWARE;
	
	/* first update the remote repositories if needed */
	if (!dry_run && auto_update && 
	    (updcode = pkgcli_update(false)) != EPKG_OK)
		return (updcode);

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK)
		return (EX_IOERR);

	if (pkg_jobs_new(&jobs, PKG_JOBS_UPGRADE, db) != EPKG_OK)
		goto cleanup;

	if (reponame != NULL && pkg_jobs_set_repository(jobs, reponame) != EPKG_OK)
		goto cleanup;

	pkg_jobs_set_flags(jobs, f);

	if (pkg_jobs_solve(jobs) != EPKG_OK)
		goto cleanup;

	if ((nbactions = pkg_jobs_count(jobs)) == 0) {
		if (!quiet)
			printf("Nothing to do\n");
		retcode = EXIT_SUCCESS;
		goto cleanup;
	}

	if (!quiet || dry_run) {
		print_jobs_summary(jobs,
		    "Uprgades have been requested for the following %d "
		    "packages:\n\n", nbactions);

		if (!yes && !dry_run)
			yes = query_yesno("\nProceed with upgrading "
			          "packages [y/N]: ");
		if (dry_run)
			yes = false;
	}

	if (yes && pkg_jobs_apply(jobs) != EPKG_OK)
		goto cleanup;

	if (messages != NULL) {
		sbuf_finish(messages);
		printf("%s", sbuf_data(messages));
	}

	retcode = EXIT_SUCCESS;

	cleanup:
	pkg_jobs_free(jobs);
	pkgdb_close(db);

	return (retcode);
}
