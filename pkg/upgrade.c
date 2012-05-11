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

#include <sys/param.h>

#include <err.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <libutil.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_upgrade(void)
{
	fprintf(stderr, "usage pkg upgrade [-r reponame] [-yfq]\n");
	fprintf(stderr, "For more information see 'pkg help upgrade'.\n");
}

int
exec_upgrade(int argc, char **argv)
{
	char url[MAXPATHLEN];
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *pkg = NULL;
	struct pkg_jobs *jobs = NULL;
	const char *reponame = NULL;
	const char *repo_name = NULL;
	const char *packagesite = NULL;
	struct pkg_config_kv *repokv = NULL;
	int retcode = 1;
	int ch;
	bool yes = false;
	bool all = false;
	bool multi_repos = false;

	if (geteuid() != 0) {
		warnx("upgrading can only be done as root");
		return (EX_NOPERM);
	}

	while ((ch = getopt(argc, argv, "yr:fq")) != -1) {
		switch (ch) {
			case 'y':
				yes = true;
				break;
			case 'r':
				reponame = optarg;
				break;
			case 'q':
				quiet = true;
				break;
			case 'f':
				all = true;
				break;
			default:
				usage_upgrade();
				return (EX_USAGE);
				break; /* NOT REACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 0) {
		usage_upgrade();
		return (EX_USAGE);
	}

	/* first update the remote repositories if needed */
	pkg_config_bool(PKG_CONFIG_MULTIREPOS, &multi_repos);
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
		if (retcode == EPKG_UPTODATE)
			retcode = EPKG_OK;
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
				retcode = EPKG_OK;
			}
		}
	}
	if (retcode != EPKG_OK)
		return (retcode);

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK) {
		return (EX_IOERR);
	}

	if (pkg_jobs_new(&jobs, PKG_JOBS_INSTALL, db) != EPKG_OK) {
		goto cleanup;
	}

	if ((it = pkgdb_query_upgrades(db, reponame, all)) == NULL) {
		goto cleanup;
	}

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_DEPS) == EPKG_OK) {
		pkg_jobs_add(jobs, pkg);
		pkg = NULL;
	}
	pkgdb_it_free(it);

	if (pkg_jobs_is_empty(jobs)) {
		if (!quiet)
			printf("Nothing to do\n");
		retcode = EXIT_SUCCESS;
		goto cleanup;
	}

	pkg = NULL;
	if (!quiet) {
		print_jobs_summary(jobs, PKG_JOBS_INSTALL, "The following packages will be upgraded:\n\n");

		if (!yes)
			pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);
		if (!yes)
			yes = query_yesno("\nProceed with upgrading packages [y/N]: ");
	}

	if (yes)
		if (pkg_jobs_apply(jobs, 0) != EPKG_OK)
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
