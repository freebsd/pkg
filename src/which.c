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

#include <sys/param.h>
#include <sys/stat.h>

#include <stdio.h>
#include <pkg.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>
#include <err.h>

#include "pkgcli.h"

void
usage_which(void)
{
	fprintf(stderr, "Usage: pkg which [-qgop] <file>\n\n");
	fprintf(stderr, "For more information see 'pkg help which'.\n");
}

static int is_there(char *);
int get_match(char **, char *, char *);

int
exec_which(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *pkg = NULL;
	char pathabs[MAXPATHLEN];
	char *p, *path;
	int ret = EPKG_OK, retcode = EX_SOFTWARE;
	int ch;
	int res;
	bool orig = false;
	bool glob = false;
	bool search = false;

	while ((ch = getopt(argc, argv, "qgop")) != -1) {
		switch (ch) {
		case 'q':
			quiet = true;
			break;
		case 'g':
			glob = true;
			break;
		case 'o':
			orig = true;
			break;
		case 'p':
			search = true;
			break;
		default:
			usage_which();
			return (EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		usage_which();
		return (EX_USAGE);
	}

	if (!glob && !search)
		absolutepath(argv[0], pathabs, sizeof(pathabs));
	else if (!search) {
		if (strlcpy(pathabs, argv[0], sizeof(pathabs)) >= sizeof(pathabs))
			return (EX_USAGE);
	} else {

		if ((p = getenv("PATH")) == NULL)
			return (EX_USAGE);
		path = malloc(strlen(p)+1);
		if (path == NULL)
			return (EX_OSERR);

		memcpy(path, p, strlen(p)+1);

		if (strlen(argv[0]) >= FILENAME_MAX)
			return (EX_USAGE);

		p = NULL;
		res = get_match(&p, path, argv[0]);
		if (res == (EX_USAGE)) {
			printf("%s was not found in PATH\n", argv[0]);
			return (EX_USAGE);
		} else if (res == (EX_OSERR)) {
			return (EX_OSERR);
		}

		strncpy(pathabs, p, strlen(p)+1);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		return (EX_IOERR);
	}

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY, 0, 0) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	if ((it = pkgdb_query_which(db, pathabs, glob)) == NULL) {
		retcode = EX_IOERR;
		goto cleanup;
	}

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

cleanup:
	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	pkgdb_close(db);

	return (retcode);
}


static int
is_there(char *candidate)
{
	struct stat fin;

	/* XXX work around access(2) false positives for superuser */
	if (access(candidate, X_OK) == 0 &&
	    stat(candidate, &fin) == 0 &&
	    S_ISREG(fin.st_mode) &&
	    (getuid() != 0 ||
	    (fin.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0)) {
		return (1);
	}
	return (0);
}

int
get_match(char **pathabs, char *path, char *filename)
{
	char candidate[PATH_MAX];
	const char *d;

	while ((d = strsep(&path, ":")) != NULL) {
		if (*d == '\0')
			d = ".";
		if (snprintf(candidate, sizeof(candidate), "%s/%s", d,
		    filename) >= (int)sizeof(candidate))
			continue;
		if (is_there(candidate)) {
			*pathabs = malloc(strlen(candidate)+1);
			if (*pathabs == NULL)
				return (EX_OSERR);
			strncpy(*pathabs, candidate, strlen(candidate)+1);
			return (EX_OK);
		}
	}
	return (EX_USAGE);
}
