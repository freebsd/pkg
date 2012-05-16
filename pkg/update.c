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
 * Fetch remote databases.
 */
int
pkgcli_update(void) {
	char url[MAXPATHLEN];
	const char *packagesite = NULL;
	const char *repo_name;
	bool multi_repos = false;
	struct pkg_config_kv *repokv = NULL;
	int retcode;

	if (!quiet)
		printf("Updating remote repository\n");

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

		if (packagesite[strlen(packagesite) - 1] == '/')
			snprintf(url, MAXPATHLEN, "%srepo.txz", packagesite);
		else
			snprintf(url, MAXPATHLEN, "%s/repo.txz", packagesite);

		retcode = pkg_update("repo", url);
		if (retcode == EPKG_UPTODATE) {
			if (!quiet)
				printf("Remote repository up-to-date, no need to upgrade\n");
			retcode = EPKG_OK;
		}
	} else {
		/* multiple repositories */
		while (pkg_config_list(PKG_CONFIG_REPOS, &repokv) == EPKG_OK) {
			repo_name = pkg_config_kv_get(repokv, PKG_CONFIG_KV_KEY);
			packagesite = pkg_config_kv_get(repokv, PKG_CONFIG_KV_VALUE);

			if (packagesite[strlen(packagesite) - 1] == '/')
				snprintf(url, MAXPATHLEN, "%srepo.txz", packagesite);
			else
				snprintf(url, MAXPATHLEN, "%s/repo.txz", packagesite);

			retcode = pkg_update(repo_name, url);
			if (retcode == EPKG_UPTODATE) {
				if (!quiet)
					printf("%s repository up-to-date, no need to upgrade\n", repo_name);
				retcode = EPKG_OK;
			}
		}
	}

	return (retcode);
}


void
usage_update(void)
{
	fprintf(stderr, "usage: pkg update [-q]\n\n");
	fprintf(stderr, "For more information see 'pkg help update'.\n");
}

int
exec_update(int argc, char **argv)
{
	int retcode = EPKG_OK;
	int ch;

	while ((ch = getopt(argc, argv, "q")) != -1) {
		switch (ch) {
			case 'q':
				quiet = true;
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

	if (geteuid() != 0) {
		warnx("updating the remote database can only be done as root");
		return (EX_NOPERM);
	}

	retcode = pkgcli_update();

	return (retcode);
}
