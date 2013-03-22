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

#include <sys/stat.h>
#include <sys/param.h>

#include <err.h>
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
pkgcli_update(bool force) {
	const char *packagesite = NULL;
	const char *repo_name;
	bool multi_repos = false;
	struct pkg_config_kv *repokv = NULL;
	int retcode = EPKG_FATAL;
	char name[MAXPATHLEN];

	/* Only auto update if the user has write access. */
	if (pkgdb_access(PKGDB_MODE_READ|PKGDB_MODE_WRITE|PKGDB_MODE_CREATE,
	    PKGDB_DB_REPO) == EPKG_ENOACCESS)
		return (EPKG_OK);

	if (!quiet)
		printf("Updating repository catalogue\n");

	pkg_config_bool(PKG_CONFIG_MULTIREPOS, &multi_repos);

	/* single repository */
	if (!multi_repos) {
		/*
		 * Single remote database
		 */
		
		pkg_config_string(PKG_CONFIG_REPO, &packagesite);
		
		if (packagesite == NULL) {
			warnx("PACKAGESITE is not defined.");
			return (1);
		}

		retcode = pkg_update("repo", packagesite, force);
		if (retcode == EPKG_UPTODATE) {
			if (!quiet)
				printf("Repository catalogue is up-to-date, "
				       "no need to fetch fresh copy\n");
			retcode = EPKG_OK;
		}
	} else {
		/* multiple repositories */
		while (pkg_config_kvlist(PKG_CONFIG_REPOS, &repokv) == EPKG_OK) {
			repo_name = pkg_config_kv_get(repokv, PKG_CONFIG_KV_KEY);
			packagesite = pkg_config_kv_get(repokv, PKG_CONFIG_KV_VALUE);

			snprintf(name, MAXPATHLEN, "repo-%s", repo_name);
			retcode = pkg_update(name, packagesite, force);
			if (retcode == EPKG_UPTODATE) {
				if (!quiet)
					printf("%s repository catalogue is "
					       "up-to-date, no need to fetch "
					       "fresh copy\n", repo_name);
				retcode = EPKG_OK;
			}
			if (retcode != EPKG_OK)
				break;
		}
	}

	return (retcode);
}


void
usage_update(void)
{
	fprintf(stderr, "usage: pkg update [-fq]\n\n");
	fprintf(stderr, "For more information see 'pkg help update'.\n");
}

int
exec_update(int argc, char **argv)
{
	int ret;
	int ch;
	bool force = false;

	while ((ch = getopt(argc, argv, "fq")) != -1) {
		switch (ch) {
		case 'q':
			quiet = true;
			break;
		case 'f':
			force = true;
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
		warnx("Insufficient privilege to update repository "
		      "catalogue");
		return (EX_NOPERM);
	} else if (ret != EPKG_OK)
		return (EX_IOERR);

	ret = pkgcli_update(force);

	return ((ret == EPKG_OK) ? EX_OK : EX_SOFTWARE);
}
