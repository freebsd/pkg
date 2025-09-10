/*-
 * Copyright (c) 2012-2014 Matthew Seaman <matthew@FreeBSD.org>
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
#include <getopt.h>
#include <stdio.h>
#include <pkg.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "pkgcli.h"

void
usage_shlib(void)
{
	fprintf(stderr, "Usage: pkg shlib [-q] [-P|R] <library>\n\n");
	fprintf(stderr, "For more information see 'pkg help shlib'.\n");
}

char *
sanitize(char *buf, const char *src, size_t size)
{
	const char *sep;
	char *dst = buf;

	/* skip path */
	if ((sep = strrchr(src, '/')) != NULL)
		src = sep + 1;
	/* copy src to dst */
	while (size > 1) {
		if (isspace((unsigned char)*src)) {
			/* whitespace is not allowed */
			return (NULL);
		}
		*dst++ = *src++;
		if (*src == '\0')
			break;
		size--;
	}
	if (*src != '\0') {
		/* src is longer than buf */
		return (NULL);
	}
	*dst = '\0';

	return (buf);
}

static int
pkgs_providing_lib(struct pkgdb *db, const char *libname)
{
	struct pkgdb_it	*it = NULL;
	struct pkg	*pkg = NULL;
	int		 ret = EPKG_OK;
	int		 count = 0;

	if ((it = pkgdb_query_shlib_provide(db, libname)) == NULL) {
		return (EPKG_FATAL);
	}

	while ((ret = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK) {
		if (count == 0 && !quiet)
			printf("%s is provided by the following packages:\n",
			       libname);
		count++;
		pkg_printf("%n-%v\n", pkg, pkg);
	}

	if (ret == EPKG_END) {
		if (count == 0 && !quiet)
			printf("No packages provide %s.\n", libname);
		ret = EPKG_OK;
	}

	pkg_free(pkg);
	pkgdb_it_free(it);

	return (ret);
}

static int
pkgs_requiring_lib(struct pkgdb *db, const char *libname)
{
	struct pkgdb_it	*it = NULL;
	struct pkg	*pkg = NULL;
	int		 ret = EPKG_OK;
	int		 count = 0;

	if ((it = pkgdb_query_shlib_require(db, libname)) == NULL) {
		return (EPKG_FATAL);
	}

	while ((ret = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK) {
		if (count == 0 && !quiet)
			printf("%s is linked to by the following packages:\n",
			       libname);
		count++;
		pkg_printf("%n-%v\n", pkg, pkg);
	}

	if (ret == EPKG_END) {
		if (count == 0 && !quiet)
			printf("No packages require %s.\n", libname);
		ret = EPKG_OK;
	}

	pkg_free(pkg);
	pkgdb_it_free(it);

	return (ret);
}

int
exec_shlib(int argc, char **argv)
{
	struct pkgdb	*db = NULL;
	char		 libname[MAXPATHLEN];
	int		 retcode = EPKG_OK;
	int		 ch;
	bool		 provides_only = false;
	bool		 requires_only = false;

	struct option longopts[] = {
		{ "provides",	no_argument,	NULL,	'P' },
		{ "requires",	no_argument,	NULL,	'R' },
		{ "quiet" ,	no_argument,	NULL,	'q' },
		{ NULL,		0,		NULL,	0 },
	};

	while ((ch = getopt_long(argc, argv, "+qPR", longopts, NULL)) != -1) {
		switch (ch) {
		case 'P':
			provides_only = true;
			break;
		case 'R':
			requires_only = true;
			break;
		case 'q':
			quiet = true;
			break;
		default:
			usage_shlib();
			return (EXIT_FAILURE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1 || (provides_only && requires_only)) {
		usage_shlib();
		return (EXIT_FAILURE);
	}

	if (argc >= 2) {
		warnx("multiple libraries per run not allowed");
		return (EXIT_FAILURE);
	}

	if (sanitize(libname, argv[0], sizeof(libname)) == NULL) {
		usage_shlib();
		return (EXIT_FAILURE);
	}

	retcode = pkgdb_open(&db, PKGDB_DEFAULT);
	if (retcode != EPKG_OK)
		return (EXIT_FAILURE);

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database, it is locked by another process");
		return (EXIT_FAILURE);
	}

	if (retcode == EPKG_OK && !requires_only)
		retcode = pkgs_providing_lib(db, libname);

	if (retcode == EPKG_OK && !provides_only)
		retcode = pkgs_requiring_lib(db, libname);

	if (retcode != EPKG_OK)
		retcode = (EXIT_FAILURE);

	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	pkgdb_close(db);
	return (retcode);
}
