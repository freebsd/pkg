/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2014 Matthew Seaman <matthew@FreeBSD.org>
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

#include <sys/stat.h>
#include <sys/param.h>

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "pkgcli.h"

/**
 * Fetch repository calalogues.
 */
int
pkgcli_update(bool force, bool strict, const char *reponame)
{
	int retcode = EPKG_FATAL, update_count = 0, total_count = 0;
	struct pkg_repo *r = NULL;

	/* Only auto update if the user has write access. */
	if (pkgdb_access(PKGDB_MODE_READ|PKGDB_MODE_WRITE|PKGDB_MODE_CREATE,
	    PKGDB_DB_REPO) == EPKG_ENOACCESS)
		return (EPKG_OK);

	if (pkg_repos_total_count() == 0) {
		fprintf(stderr, "No active remote repositories configured.\n");
		return (EPKG_FATAL);
	}

	while (pkg_repos(&r) == EPKG_OK) {
		if (reponame != NULL) {
			if (strcmp(pkg_repo_name(r), reponame) != 0)
				continue;
		} else {
			if (!pkg_repo_enabled(r))
				continue;
		}

		if (!quiet)
			printf("Updating %s repository catalogue...\n",
			    pkg_repo_name(r));
		retcode = pkg_update(r, force);
		if (retcode == EPKG_UPTODATE) {
			if (!quiet)
				printf("%s repository is up-to-date.\n",
				    pkg_repo_name(r));
		}
		else if (retcode != EPKG_OK && strict)
			retcode = EPKG_FATAL;

		total_count ++;
		if (retcode != EPKG_OK)
			continue;

		update_count ++;
	}

	if (!strict || retcode == EPKG_UPTODATE)
		retcode = EPKG_OK;

	if (total_count == 0) {
		if (!quiet)
			printf("No repositories are enabled.\n");
		retcode = EPKG_FATAL;
	}
	else if (update_count == 0) {
		if (!quiet)
			if (retcode == EPKG_OK)
				printf("All repositories are up-to-date.\n");
	}

	return (retcode);
}


void
usage_update(void)
{
	fprintf(stderr, "Usage: pkg update [-fq] [-r reponame]\n\n");
	fprintf(stderr, "For more information, see 'pkg help update'.\n");
}

int
exec_update(int argc, char **argv)
{
	int		 ret;
	int		 ch;
	const char	*reponame = NULL;

	struct option longopts[] = {
		{ "force",	no_argument,		NULL,	'f' },
		{ "quiet",	no_argument,		NULL,	'q' },
		{ "repository", required_argument,	NULL,	'r' },
		{ NULL,		0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+fqr:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'f':
			force = true;
			break;
		case 'q':
			quiet = true;
			break;
		case 'r':
			reponame = optarg;
			break;
		default:
			usage_update();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 0) {
		usage_update();
		return (EX_USAGE);
	}

	ret = pkgdb_access(PKGDB_MODE_WRITE|PKGDB_MODE_CREATE,
			   PKGDB_DB_REPO);
	if (ret == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to update the repository "
		      "catalogue.");
		return (EX_NOPERM);
	} else if (ret != EPKG_OK)
		return (EX_IOERR);

	/* For pkg-update update op is strict */
	ret = pkgcli_update(force, true, reponame);

	return ((ret == EPKG_OK) ? EX_OK : EX_SOFTWARE);
}
