/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
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

void
usage_upgrade(void)
{
	fprintf(stderr, "usage: pkg upgrade [-fLnqy] [-r reponame]\n\n");
	fprintf(stderr, "For more information see 'pkg help upgrade'.\n");
}

int
exec_upgrade(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *pkg = NULL;
	struct pkg_jobs *jobs = NULL;
	const char *reponame = NULL;
	int retcode = 1;
	int updcode;
	int ch;
	bool yes;
	bool all = false;
	bool dry_run = false;
	bool auto_update = true;
	nbactions = nbdone = 0;

	pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);

	while ((ch = getopt(argc, argv, "fLnqr:y")) != -1) {
		switch (ch) {
		case 'f':
			all = true;
			break;
		case 'L':
			auto_update = false;
			break;
		case 'n':
			dry_run = true;
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

	if (!dry_run && geteuid() != 0) {
		warnx("Upgrading can only be done as root");
		return (EX_NOPERM);
	}

	if (argc != 0) {
		usage_upgrade();
		return (EX_USAGE);
	}

	/* first update the remote repositories if needed */
	if (!dry_run && auto_update && 
	    (updcode = pkgcli_update(false)) != EPKG_OK)
		return (updcode);

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK) {
		return (EX_IOERR);
	}

	if (pkg_jobs_new(&jobs, PKG_JOBS_INSTALL, db, false, dry_run)
	    != EPKG_OK) {
		goto cleanup;
	}

	if ((it = pkgdb_query_upgrades(db, reponame, all)) == NULL) {
		goto cleanup;
	}

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_DEPS) ==
	       EPKG_OK) {
		pkg_jobs_add(jobs, pkg);
		pkg = NULL;
	}
	pkgdb_it_free(it);

	if ((nbactions = pkg_jobs_count(jobs)) == 0) {
		if (!quiet)
			printf("Nothing to do\n");
		retcode = EXIT_SUCCESS;
		goto cleanup;
	}

	pkg = NULL;
	if (!quiet || dry_run) {
		print_jobs_summary(jobs, PKG_JOBS_INSTALL,
		    "Uprgades have been requested for the following %d "
		    "packages:\n\n", nbactions);

		if (!yes && !dry_run)
			yes = query_yesno("\nProceed with upgrading "
			          "packages [y/N]: ");
		if (dry_run)
			yes = false;
	}

	if (yes)
		if (pkg_jobs_apply(jobs) != EPKG_OK)
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
