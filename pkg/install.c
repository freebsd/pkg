/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
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

#include <sys/types.h>

#include <err.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <libutil.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_install(void)
{
	fprintf(stderr,
	    "usage: pkg install [-AfgLnqRxy] [-r reponame] <pkg-name> ...\n\n");
	fprintf(stderr, "For more information see 'pkg help install'.\n");
}

int
exec_install(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct pkg_jobs *jobs = NULL;
	const char *reponame = NULL;
	int retcode;
	int updcode = EPKG_OK;
	int ch;
	bool yes;
	bool auto_update;
	match_t match = MATCH_EXACT;
	bool dry_run = false;
	nbactions = nbdone = 0;
	pkg_flags f = PKG_FLAG_NONE;

	pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);
	pkg_config_bool(PKG_CONFIG_REPO_AUTOUPDATE, &auto_update);

	while ((ch = getopt(argc, argv, "AfgLnqRr:xy")) != -1) {
		switch (ch) {
		case 'A':
			f |= PKG_FLAG_AUTOMATIC;
			break;
		case 'f':
			f |= PKG_FLAG_FORCE;
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'L':
			auto_update = false;
			break;
		case 'n':
			f |= PKG_FLAG_DRY_RUN;
			dry_run = true;
			break;
		case 'q':
			quiet = true;
			break;
		case 'R':
			f |= PKG_FLAG_RECURSIVE;
			break;
		case 'r':
			reponame = optarg;
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

	retcode = pkgdb_access(PKGDB_MODE_READ  |
			       PKGDB_MODE_WRITE |
			       PKGDB_MODE_CREATE,
			       PKGDB_DB_LOCAL   |
			       PKGDB_DB_REPO);
	if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privilege to install packages");
		return (EX_NOPERM);
	} else if (retcode != EPKG_OK)
		return (EX_IOERR);
	else
		retcode = EX_SOFTWARE;

	/* first update the remote repositories if needed */
	if (auto_update && (updcode = pkgcli_update(false)) != EPKG_OK)
		return (updcode);

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK)
		return (EX_IOERR);

	if (pkg_jobs_new(&jobs, PKG_JOBS_INSTALL, db) != EPKG_OK)
		goto cleanup;

	pkg_jobs_set_flags(jobs, f);

	if (pkg_jobs_add(jobs, match, argv, argc) == EPKG_FATAL)
		goto cleanup;

	if (pkg_jobs_solve(jobs) != EPKG_OK)
		goto cleanup;

	if ((nbactions = pkg_jobs_count(jobs)) == 0)
		goto cleanup;

	/* print a summary before applying the jobs */
	if (!quiet || dry_run) {
		print_jobs_summary(jobs,
		    "The following %d packages will be installed:\n\n",
		    nbactions);

		if (!yes && !dry_run)
			yes = query_yesno(
			    "\nProceed with installing packages [y/N]: ");
		if (dry_run)
			yes = false;
	}

	if (yes && pkg_jobs_apply(jobs) != EPKG_OK)
		goto cleanup;

	if (messages != NULL) {
		sbuf_finish(messages);
		printf("%s", sbuf_data(messages));
	}

	retcode = EX_OK;

cleanup:
	pkg_jobs_free(jobs);
	pkgdb_close(db);

	return (retcode);
}
