/*
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
	fprintf(stderr, "usage: pkg autoremove [-yq]\n\n");
	fprintf(stderr, "For more information see 'pkg help autoremove'.\n");
}

int
exec_autoremove(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct pkgdb_it *it;
	struct pkg *pkg = NULL;
	struct pkg_jobs *jobs = NULL;
	int retcode = EPKG_OK;
	int64_t oldsize = 0, newsize = 0;
	int64_t flatsize, newflatsize;
	char size[7];
	int ch;
	bool yes = false;

	while ((ch = getopt(argc, argv, "yq")) != -1) {
		switch (ch) {
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
	argv += optind;

	(void) argv;
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
	if (pkg_jobs_new(&jobs, PKG_JOBS_DEINSTALL, db, true, false)
	    != EPKG_OK) {
		pkgdb_close(db);
		return (EX_IOERR);
	}

	if ((it = pkgdb_query_autoremove(db)) == NULL) {
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	while ((retcode = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK) {
		pkg_get(pkg, PKG_FLATSIZE, &flatsize, PKG_NEW_FLATSIZE, &newflatsize);
		oldsize += flatsize;
		newsize += newflatsize;
		pkg_jobs_add(jobs, pkg);
		pkg = NULL;
	}

	if (oldsize > newsize) {
		newsize *= -1;
		humanize_number(size, sizeof(size), oldsize - newsize, "B", HN_AUTOSCALE, 0);
	} else {
		humanize_number(size, sizeof(size), newsize - oldsize, "B", HN_AUTOSCALE, 0);
	}

	if (pkg_jobs_count(jobs) == 0) {
		printf("Nothing to do.\n");
		retcode = 0;
		goto cleanup;
	}

	pkg = NULL;
	if (!quiet) {
		printf("Packages to be autoremoved: \n");
		while (pkg_jobs(jobs, &pkg) == EPKG_OK) {
			const char *name, *version;
			pkg_get(pkg, PKG_NAME, &name, PKG_VERSION, &version);
			printf("\t%s-%s\n", name, version);
		}

		if (oldsize > newsize)
			printf("\nThe autoremoval will free %s\n", size);
		else
			printf("\nThe autoremoval will require %s more space\n", size);

		if (!yes)
			pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);
		if (!yes)
			yes = query_yesno("\nProceed with autoremoval of packages [y/N]: ");
	}

	if (yes) {
		if ((retcode = pkg_jobs_apply(jobs)) != EPKG_OK)
			goto cleanup;
	}

	if (pkgdb_compact(db) != EPKG_OK) {
		retcode = EPKG_FATAL;
	}

	cleanup:
	pkg_free(pkg);
	pkg_jobs_free(jobs);
	pkgdb_it_free(it);
	pkgdb_close(db);

	return ((retcode == EPKG_OK) ? EX_OK : EX_SOFTWARE);
}
