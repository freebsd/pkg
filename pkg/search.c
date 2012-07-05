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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_search(void)
{
	fprintf(stderr, "usage: pkg search [-r reponame] <pkg-name>\n");
	fprintf(stderr, "       pkg search [-r reponame] [-fDsqop] <pkg-name>\n");
	fprintf(stderr, "       pkg search [-r reponame] [-egxXcdfDsqop] <pattern>\n\n");
	fprintf(stderr, "For more information see 'pkg help search'.\n");
}

int
exec_search(int argc, char **argv)
{
	const char *pattern = NULL;
	const char *reponame = NULL;
	int ret = EPKG_OK, ch;
	int flags = PKG_LOAD_BASIC;
	unsigned int opt = 0;
	match_t match = MATCH_REGEX;
	pkgdb_field field = FIELD_NAME;
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *pkg = NULL;
	bool atleastone = false;

	while ((ch = getopt(argc, argv, "gxXcdr:fDsqop")) != -1) {
		switch (ch) {
			case 'e':
				match = MATCH_EXACT;
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
			case 'c':
				field = FIELD_COMMENT;
				break;
			case 'd':
				field = FIELD_DESC;
				break;
			case 'r':
				reponame = optarg;
			case 'f':
				opt |= INFO_FULL;
				flags |= PKG_LOAD_CATEGORIES|PKG_LOAD_LICENSES|PKG_LOAD_OPTIONS|PKG_LOAD_SHLIBS;
				break;
			case 'D':
				opt |= INFO_PRINT_DEP;
				flags |= PKG_LOAD_DEPS;
				break;
			case 's':
				opt |= INFO_SIZE;
				break;
			case 'q':
				opt |= INFO_QUIET;
				break;
			case 'o':
				opt |= INFO_ORIGIN;
				break;
			case 'p':
				opt |= INFO_PREFIX;
				break;
			default:
				usage_search();
				return (EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		usage_search();
		return (EX_USAGE);
	}

	pattern = argv[0];
	if (pattern[0] == '\0') {
		fprintf(stderr, "Pattern must not be empty!\n");
		return (EX_USAGE);
	}
	if (strchr(pattern, '/') != NULL)
		field = FIELD_ORIGIN;

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK)
		return (EX_IOERR);

	if ((it = pkgdb_search(db, pattern, match, field, reponame)) == NULL) {
		pkgdb_close(db);
		return (EX_IOERR);
	}

	while ((ret = pkgdb_it_next(it, &pkg, flags)) == EPKG_OK) {
		print_info(pkg, opt);
		atleastone = true;
	}

	pkg_free(pkg);
	pkgdb_it_free(it);
	pkgdb_close(db);

	if (!atleastone)
		ret = EPKG_FATAL;

	if (ret == EPKG_END)
		ret = EPKG_OK;

	return ((ret == EPKG_OK) ? EX_OK : EX_SOFTWARE);
}
