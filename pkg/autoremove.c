/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
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
usage_autoremove(void)
{
	fprintf(stderr, "usage: pkg autoremove [-ynq]\n\n");
	fprintf(stderr, "For more information see 'pkg help autoremove'.\n");
}

int
exec_autoremove(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct pkg_jobs *jobs = NULL;
	int retcode = EX_SOFTWARE;
	int ch;
	bool yes = false;
	bool dry_run = false;
	nbactions = nbdone = 0;
	pkg_flags f = PKG_FLAG_FORCE;

	pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);

	while ((ch = getopt(argc, argv, "ynq")) != -1) {
		switch (ch) {
		case 'q':
			quiet = true;
			break;
		case 'y':
			yes = true;
			break;
		case 'n':
			f |= PKG_FLAG_DRY_RUN;
			dry_run = true;
			break;
		default:
			break;
		}
        }
	argc -= optind;
	argv += optind;

	if (argc != 0) {
		usage_autoremove();
		return (EX_USAGE);
	}

	if (geteuid() != 0) {
		warnx("autoremove can only be done as root");
		return (EX_NOPERM);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		return (EX_IOERR);
	}

	/* Always force packages to be removed */
	if (pkg_jobs_new(&jobs, PKG_JOBS_AUTOREMOVE, db) != EPKG_OK) {
		pkgdb_close(db);
		return (EX_IOERR);
	}

	pkg_jobs_set_flags(jobs, f);

	if ((retcode = pkg_jobs_solve(jobs)) != EPKG_OK)
		goto cleanup;

	if ((nbactions = pkg_jobs_count(jobs)) == 0) {
		printf("Nothing to do.\n");
		retcode = 0;
		goto cleanup;
	}

	if (!quiet || dry_run) {
		print_jobs_summary(jobs,
		    "Deinstallation has been requested for the following %d packages:\n\n", nbactions);
		if (!yes && !dry_run)
			yes = query_yesno(
		            "\nProceed with deinstalling packages [y/N]: ");
		if (dry_run)
			yes = false;
	}
	if (!yes || (retcode = pkg_jobs_apply(jobs)) != EPKG_OK)
		goto cleanup;

	pkgdb_compact(db);

	retcode = EX_OK;

cleanup:
	pkg_jobs_free(jobs);
	pkgdb_close(db);

	return (retcode);
}
