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

#include <stdio.h>
#include <pkg.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>

#include "pkgcli.h"

void
usage_which(void)
{
	fprintf(stderr, "usage: pkg which [-qgo] <file>\n\n");
	fprintf(stderr, "For more information see 'pkg help which'.\n");
}

int
exec_which(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *pkg = NULL;
	char pathabs[MAXPATHLEN + 1];
	int ret = EPKG_OK, retcode = EX_OK;
	int ch;
	bool orig = false;
	bool glob = false;

	while ((ch = getopt(argc, argv, "qgo")) != -1) {
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

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		pkgdb_close(db);
		return (EX_IOERR);
	}

	if (!glob)
		absolutepath(argv[0], pathabs, sizeof(pathabs));
	else
		strlcpy(pathabs, argv[0], sizeof(pathabs));

	if ((it = pkgdb_query_which(db, pathabs, glob)) == NULL)
		return (EX_IOERR);

	while ((ret = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK) {
		if (quiet && orig)
			pkg_printf("%o\n", pkg);
		else if (quiet && !orig)
			pkg_printf("%n-%v\n", pkg, pkg);
		else if (!quiet && orig)
			pkg_printf("%S was installed by package %o\n", pathabs, pkg);
		else if (!quiet && !orig)
			pkg_printf("%S was installed by package %n-%v\n", pathabs, pkg, pkg);
		if (!glob)
			break;
	}

	if (ret != EPKG_END) {
		retcode = EX_SOFTWARE;
	}
	else if (!glob) {
		if (!quiet)
			printf("%s was not found in the database\n", pathabs);
		retcode = EX_DATAERR;
	}
		
	pkg_free(pkg);
	pkgdb_it_free(it);

	pkgdb_close(db);
	return (retcode);
}
