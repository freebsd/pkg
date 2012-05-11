/*
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
#include <sys/param.h>

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
usage_fetch(void)
{
	fprintf(stderr, "usage: pkg fetch [-r reponame] [-yqgxXa] <pkg-name> <...>\n\n");
	fprintf(stderr, "For more information see 'pkg help fetch'.\n");
}

int
exec_fetch(int argc, char **argv)
{
	char url[MAXPATHLEN];
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	struct pkgdb *db = NULL;
	struct pkg_jobs *jobs = NULL;
	const char *reponame = NULL;
	const char *repo_name = NULL;
	const char *packagesite = NULL;
	struct pkg_config_kv *repokv = NULL;
	int retcode = EXIT_FAILURE;
	int ch;
	bool yes = false;
	bool multi_repos = false;
	match_t match = MATCH_EXACT;

	while ((ch = getopt(argc, argv, "ygxXr:qa")) != -1) {
		switch (ch) {
		case 'y':
			yes = true;
			break;
		case 'a':
			match = MATCH_ALL;
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'x':
			match = MATCH_REGEX;
			break;
		case 'X':
			match = MATCH_EREGEX;
			break;
		case 'r':
			reponame = optarg;
			break;
		case 'q':
			quiet = true;
			break;
		default:
			usage_fetch();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;
	
	if (argc < 1 && match != MATCH_ALL) {
		usage_fetch();
		return (EX_USAGE);
	}

	/* TODO: Allow the user to specify an output directory via -o outdir */
	if (geteuid() != 0) {
		warnx("fetching packages can only be done as root");
		return (EX_NOPERM);
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

	if (pkg_jobs_new(&jobs, PKG_JOBS_FETCH, db) != EPKG_OK) {
		goto cleanup;
	}

	if ((it = pkgdb_query_fetch(db, match, argc, argv, reponame)) == NULL)
		goto cleanup;

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_DEPS) == EPKG_OK) {
		pkg_jobs_add(jobs, pkg);
		pkg = NULL;
	}
	
	pkgdb_it_free(it);

	if (pkg_jobs_is_empty(jobs))
		goto cleanup;

	if (!quiet) {
		print_jobs_summary(jobs, PKG_JOBS_FETCH, "The following packages will be fetched:\n\n");
		
		if (!yes)
			pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);
		if (!yes)
			yes = query_yesno("\nProceed with fetching packages [y/N]: ");
	}
	
	if (yes)
		if (pkg_jobs_apply(jobs, 0) != EPKG_OK)
			goto cleanup;

	retcode = EXIT_SUCCESS;

	cleanup:
	pkg_jobs_free(jobs);
	pkgdb_close(db);

	return (retcode);
}
