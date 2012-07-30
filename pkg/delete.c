/*
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

#include <err.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <libutil.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_delete(void)
{
	fprintf(stderr, "usage: pkg delete [-fgnqRXxy] <pkg-name> ...\n");
	fprintf(stderr, "       pkg delete [-nqy] -a\n\n");
	fprintf(stderr, "For more information see 'pkg help delete'.\n");
}

int
exec_delete(int argc, char **argv)
{
	struct pkg_jobs *jobs = NULL;
	struct pkg *pkg = NULL;
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	match_t match = MATCH_EXACT;
	int ch;
	int flags = PKG_LOAD_BASIC;
	int force = 0;
	bool yes = false;
	bool dry_run = false;
	int retcode = EX_SOFTWARE;
	int recursive = 0;
	bool haspkg = false;
	const char *origin;

	while ((ch = getopt(argc, argv, "afgnqRXxy")) != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
			break;
		case 'f':
			force = 1;
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'n':
			dry_run = true;
			break;
		case 'q':
			quiet = true;
			break;
		case 'R':
			recursive = 1;
			break;
		case 'X':
			match = MATCH_EREGEX;
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

	if (geteuid() != 0) {
		warnx("deleting packages can only be done as root");
		return (EX_NOPERM);
	}
	
	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		return (EPKG_FATAL);
	}

	if (pkg_jobs_new(&jobs, PKG_JOBS_DEINSTALL, db) != EPKG_OK) {
		pkgdb_close(db);
		return (EX_IOERR);
	}

	if ((it = pkgdb_query_delete(db, match, argc, argv, recursive)) == NULL)
		goto cleanup;

	while (pkgdb_it_next(it, &pkg, flags) == EPKG_OK) {
		pkg_get(pkg, PKG_ORIGIN, &origin);
		if (!force && !haspkg) {
			if (strcmp(origin, "ports-mgmt/pkg") == 0)
				haspkg = true;
		}
		pkg_jobs_add(jobs, pkg);
		pkg = NULL;
	}
	if (haspkg && !force) {
		warnx("You are about to delete 'ports-mgmt/pkg' which is really"
		    "dangerous, you can't do that without specifying -f");
		goto cleanup;
	}

	/* check if we have something to deinstall */
	if (pkg_jobs_is_empty(jobs)) {
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

	pkg = NULL;
	if (!quiet || dry_run) {
		print_jobs_summary(jobs, PKG_JOBS_DEINSTALL,
		    "The following packages will be deinstalled:\n\n");

		if (!yes)
			pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);
		if (!yes && !dry_run)
			yes = query_yesno(
		            "\nProceed with deinstalling packages [y/N]: ");
		if (dry_run)
			yes = false;
	}
	if (yes) {
		if ((retcode = pkg_jobs_apply(jobs, force)) != EPKG_OK)
			goto cleanup;
	} else
		goto cleanup;

	pkgdb_compact(db);

	retcode = EX_OK;

cleanup:
	pkg_jobs_free(jobs);
	pkgdb_it_free(it);
	pkgdb_close(db);

	return (retcode);
}
