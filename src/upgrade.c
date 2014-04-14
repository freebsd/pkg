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
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_upgrade(void)
{
	fprintf(stderr, "Usage: pkg upgrade [-fInFqUy] [-r reponame]\n\n");
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
	int lock_type = PKGDB_LOCK_ADVISORY;
	bool yes = true, yes_arg = false;
	bool dry_run = false;
	bool auto_update;
	int done = 0;
	nbactions = nbdone = 0;
	pkg_flags f = PKG_FLAG_NONE | PKG_FLAG_PKG_VERSION_TEST;

	yes_arg = pkg_object_bool(pkg_config_get("ASSUME_ALWAYS_YES"));
	auto_update = pkg_object_bool(pkg_config_get("REPO_AUTOUPDATE"));

	while ((ch = getopt(argc, argv, "fInqFr:Uy")) != -1) {
		switch (ch) {
		case 'f':
			f |= PKG_FLAG_FORCE;
			break;
		case 'I':
			f |= PKG_FLAG_NOSCRIPT;
			break;
		case 'U':
			auto_update = false;
			break;
		case 'n':
			f |= PKG_FLAG_DRY_RUN;
			lock_type = PKGDB_LOCK_READONLY;
			dry_run = true;
			break;
		case 'F':
			f |= PKG_FLAG_SKIP_INSTALL;
			lock_type = PKGDB_LOCK_READONLY;
			break;
		case 'q':
			quiet = true;
			break;
		case 'r':
			reponame = optarg;
			break;
		case 'y':
			yes_arg = true;
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

	if (dry_run && !auto_update)
		retcode = pkgdb_access(PKGDB_MODE_READ,
				       PKGDB_DB_LOCAL|PKGDB_DB_REPO);
	else
		retcode = pkgdb_access(PKGDB_MODE_READ  |
				       PKGDB_MODE_WRITE |
				       PKGDB_MODE_CREATE,
				       PKGDB_DB_LOCAL|PKGDB_DB_REPO);
	if (retcode == EPKG_ENOACCESS && dry_run) {
		auto_update = false;
		retcode = pkgdb_access(PKGDB_MODE_READ,
				       PKGDB_DB_LOCAL|PKGDB_DB_REPO);
	}

	if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privilege to upgrade packages");
		return (EX_NOPERM);
	} else if (retcode != EPKG_OK)
		return (EX_IOERR);
	else
		retcode = EX_SOFTWARE;
	
	/* first update the remote repositories if needed */
	if (auto_update &&
	    (updcode = pkgcli_update(false, reponame)) != EPKG_OK)
		return (updcode);

	if (pkgdb_open_all(&db, PKGDB_REMOTE, reponame) != EPKG_OK)
		return (EX_IOERR);

	if (pkgdb_obtain_lock(db, lock_type, 0, 0) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get an advisory lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	if (pkg_jobs_new(&jobs, PKG_JOBS_UPGRADE, db) != EPKG_OK)
		goto cleanup;

	if (reponame != NULL && pkg_jobs_set_repository(jobs, reponame) != EPKG_OK)
		goto cleanup;

	pkg_jobs_set_flags(jobs, f);

	if (pkg_jobs_solve(jobs) != EPKG_OK)
		goto cleanup;

	while ((nbactions = pkg_jobs_count(jobs)) > 0) {
		/* print a summary before applying the jobs */
		yes = yes_arg;
		if (!quiet || dry_run) {
			print_jobs_summary(jobs,
				"The following %d packages will be affected (of %d checked):\n\n",
				nbactions, pkg_jobs_total(jobs));

			if (!yes && !dry_run)
				yes = query_yesno(false, "\nProceed with this action [y/N]: ");
			if (dry_run)
				yes = false;
		}

		if (yes) {
			retcode = pkg_jobs_apply(jobs);
			done = 1;
			if (retcode == EPKG_CONFLICT) {
				printf ("The conflicts with the existing packages have been found.\n"
						"We need to run one more solver iteration to resolve them.\n");
				continue;
			}
			else if (retcode != EPKG_OK)
				goto cleanup;
		}

		if (messages != NULL) {
			sbuf_finish(messages);
			printf("%s", sbuf_data(messages));
		}
		break;
	}

	if (done == 0 && yes)
		printf("Your packages are up to date\n");

	retcode = EX_OK;

cleanup:
	pkg_jobs_free(jobs);
	pkgdb_release_lock(db, lock_type);
	pkgdb_close(db);

	if (!yes && newpkgversion)
		newpkgversion = false;

	return (retcode);
}
