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

#include <sys/param.h>
#include <sys/stat.h>

#include <err.h>
#include <getopt.h>
#include <pkg.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "pkgcli.h"

void
usage_which(void)
{
	fprintf(stderr, "Usage: pkg which [-qgop] <file>\n\n");
	fprintf(stderr, "For more information see 'pkg help which'.\n");
}

static bool is_there(char *);
int get_match(char **, char *, char *);

int
exec_which(int argc, char **argv)
{
	struct pkgdb	*db = NULL;
	struct pkgdb_it	*it = NULL;
	struct pkg	*pkg = NULL;
	char		 pathabs[MAXPATHLEN];
	char		*p, *path, *match;
	int		 ret = EPKG_OK, retcode = EX_SOFTWARE;
	int		 ch;
	int		 res, pathlen = 0;
	bool		 orig = false;
	bool		 glob = false;
	bool		 search = false;
	bool		 search_s = false;

	struct option longopts[] = {
		{ "glob",		no_argument,	NULL,	'g' },
		{ "origin",		no_argument,	NULL,	'o' },
		{ "path-search",	no_argument,	NULL,	'p' },
		{ "quiet",		no_argument,	NULL,	'q' },
		{ NULL,			0,		NULL,	0   },
	};

	path = NULL;

	while ((ch = getopt_long(argc, argv, "+gopq", longopts, NULL)) != -1) {
		switch (ch) {
		case 'g':
			glob = true;
			break;
		case 'o':
			orig = true;
			break;
		case 'p':
			search_s = true;
			break;
		case 'q':
			quiet = true;
			break;
		default:
			usage_which();
			return (EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		usage_which();
		return (EX_USAGE);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		return (EX_IOERR);
	}

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	if (search_s) {
		if ((path = getenv("PATH")) == NULL) {
			printf("$PATH is not set, falling back to non-search behaviour\n");
			search_s = false;
		} else {
			pathlen = strlen(path) + 1;
		}
	}

	while (argc >= 1) {
		retcode = EX_SOFTWARE;
		if (search_s) {
			if ((argv[0][0] == '.') || (argv[0][0] == '/')) {
				search = false;
			} else {
				search = true;

				if (strlen(argv[0]) >= FILENAME_MAX) {
					retcode = EX_USAGE;
					goto cleanup;
				}

				p = malloc(pathlen);
				if (p == NULL) {
					retcode = EX_OSERR;
					goto cleanup;
				}
				strlcpy(p, path, pathlen);

				match = NULL;
				res = get_match(&match, p, argv[0]);
				free(p);

				if (res == (EX_USAGE)) {
					printf("%s was not found in PATH, falling back to non-search behaviour\n", argv[0]);
					search = false;
				} else if (res == (EX_OSERR)) {
					retcode = EX_OSERR;
					goto cleanup;
				} else {
					absolutepath(match, pathabs, sizeof(pathabs));
					free(match);
				}
			}
		}

		if (!glob && !search)
			absolutepath(argv[0], pathabs, sizeof(pathabs));
		else if (!search) {
			if (strlcpy(pathabs, argv[0], sizeof(pathabs)) >= sizeof(pathabs)) {
				retcode = EX_USAGE;
				goto cleanup;
                        }
		}


		if ((it = pkgdb_query_which(db, pathabs, glob)) == NULL) {
			retcode = EX_IOERR;
			goto cleanup;
		}

		pkg = NULL;
		while ((ret = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK) {
			retcode = EX_OK;
			if (quiet && orig)
				pkg_printf("%o\n", pkg);
			else if (quiet && !orig)
				pkg_printf("%n-%v\n", pkg, pkg);
			else if (!quiet && orig)
				pkg_printf("%S was installed by package %o\n", pathabs, pkg);
			else if (!quiet && !orig)
				pkg_printf("%S was installed by package %n-%v\n", pathabs, pkg, pkg);
		}

		if (retcode != EX_OK && !quiet)
			printf("%s was not found in the database\n", pathabs);

		pkg_free(pkg);
		pkgdb_it_free(it);

		argc--;
		argv++;

	}

	cleanup:
		pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
		pkgdb_close(db);

	return (retcode);
}


static bool
is_there(char *candidate)
{
	return (access(candidate, F_OK) == 0);
}

int
get_match(char **pathabs, char *path, char *filename)
{
	char candidate[PATH_MAX];
	const char *d;
	int len;

	while ((d = strsep(&path, ":")) != NULL) {
		if (snprintf(candidate, sizeof(candidate), "%s/%s", d,
		    filename) >= (int)sizeof(candidate))
			continue;
		if (is_there(candidate)) {
			len = strlen(candidate) + 1;
			*pathabs = malloc(len);
			if (*pathabs == NULL)
				return (EX_OSERR);
			strlcpy(*pathabs, candidate, len);
			return (EX_OK);
		}
	}
	return (EX_USAGE);
}
