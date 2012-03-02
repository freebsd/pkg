/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
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

#include <err.h>
#include <inttypes.h>
#include <pkg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "pkgcli.h"

enum sign {
	LT,
	LE,
	GT,
	GE,
	EQ
};

void
usage_info(void)
{
	fprintf(stderr, "usage: pkg info <pkg-name>\n");
	fprintf(stderr, "       pkg info -a\n");
	fprintf(stderr, "       pkg info [-egxXdrlsqOf] <pkg-name>\n");
	fprintf(stderr, "       pkg info [-drlsqfR] -F <pkg-file>\n\n");
	fprintf(stderr, "For more information see 'pkg help info'.\n");
}

/*
 * list of options
 * -S <type> : show scripts, type can be pre-install etc: TODO
 */

int
exec_info(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	int query_flags = PKG_LOAD_BASIC;
	struct pkg *pkg = NULL;
	unsigned int opt = 0;
	match_t match = MATCH_EXACT;
	char *pkgname;
	char *pkgversion = NULL, *pkgversion2 = NULL;
	const char *file = NULL;
	int ch;
	int ret = EPKG_OK;
	int retcode = 0;
	bool gotone = false;
	int i, j;
	int sign = 0;
	int sign2 = 0;

	/* TODO: exclusive opts ? */
	while ((ch = getopt(argc, argv, "aegxXEdrlsqopOfF:R")) != -1) {
		switch (ch) {
			case 'a':
				match = MATCH_ALL;
				break;
			case 'O':
				opt |= INFO_ORIGIN_SEARCH;  /* this is only for ports compat */
				break;
			case 'e':
				opt |= INFO_EXISTS;
				retcode = 1;
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
			case 'd':
				opt |= INFO_PRINT_DEP;
				query_flags |= PKG_LOAD_DEPS;
				break;
			case 'r':
				opt |= INFO_PRINT_RDEP;
				query_flags |= PKG_LOAD_RDEPS;
				break;
			case 'l':
				opt |= INFO_LIST_FILES;
				query_flags |= PKG_LOAD_FILES;
				break;
			case 's':
				opt |= INFO_SIZE;
				break;
			case 'E': /* ports compatibility */
			case 'q':
				opt |= INFO_QUIET;
				break;
			case 'o':
				opt |= INFO_ORIGIN;
				break;
			case 'p':
				opt |= INFO_PREFIX;
				break;
			case 'f':
				opt |= INFO_FULL;
				query_flags |= PKG_LOAD_CATEGORIES|PKG_LOAD_LICENSES|PKG_LOAD_OPTIONS;
				break;
			case 'F':
				file = optarg;
				break;
			case 'R':
				opt |= INFO_RAW;
				query_flags |= PKG_LOAD_FILES|PKG_LOAD_DIRS|PKG_LOAD_CATEGORIES|PKG_LOAD_LICENSES|PKG_LOAD_OPTIONS|PKG_LOAD_SCRIPTS|PKG_LOAD_USERS|PKG_LOAD_GROUPS;
				break;
			default:
				usage_info();
				return(EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0 && file == NULL && match != MATCH_ALL) {
		/* which -O bsd.*.mk always execpt clean output */
		if (opt & INFO_ORIGIN_SEARCH)
			return (EX_OK);
		usage_info();
		return (EX_USAGE);
	}

	if (file != NULL) {
		if (pkg_open(&pkg, file, NULL) != EPKG_OK) {
			return (1);
		}
		print_info(pkg, opt);
		pkg_free(pkg);
		return (0);
	}

	ret = pkgdb_open(&db, PKGDB_DEFAULT);
	if (ret == EPKG_ENODB) {
		if (geteuid() == 0)
			return (EX_IOERR);

		if (match == MATCH_ALL)
			return (EXIT_SUCCESS);

		if ((opt & INFO_QUIET) == 0)
			printf("No package installed\n");

		return (EXIT_FAILURE);
	}

	if (ret != EPKG_OK)
		return (EX_IOERR);

	i = 0;
	do {
		gotone = false;
		pkgname = argv[i];

		/*
		 * allow to search for origin with a trailing /
		 * likes audio/linux-vsound depending on ${PORTSDIR}/audio/sox/
		 */

		if (argc > 0 && pkgname[strlen(pkgname) -1] == '/')
			pkgname[strlen(pkgname) -1] = '\0';

		if (argc > 0) {
			j=0;
			while (pkgname[j] != '\0') {
				if (pkgname[j] == '<') {
					if (pkgversion) {
						pkgversion2 = pkgname + j;
						sign2 = LT;
						pkgversion2[0] = '\0';
						pkgversion2++;
						if (pkgversion2[0] == '=') {
							pkgversion2++;
							sign=LE;
							j++;
						}
					} else {
						pkgversion = pkgname + j;
						sign = LT;
						pkgversion[0] = '\0';
						pkgversion++;
						if (pkgversion[0] == '=') {
							pkgversion++;
							sign=LE;
							j++;
						}
					}
				} else if (pkgname[j] == '>') {
					if (pkgversion) {
						pkgversion2 = pkgname + j;
						sign2 = GT;
						pkgversion2[0] = '\0';
						pkgversion2++;
						if (pkgversion2[0] == '=') {
							pkgversion2++;
							sign=GE;
							j++;
						}
					} else {
						pkgversion = pkgname + j;
						sign = GT;
						pkgversion[0] = '\0';
						pkgversion++;
						if (pkgversion[0] == '=') {
							pkgversion++;
							sign=GE;
							j++;
						}
					}
				} else if (pkgname[j] == '=') {
					if (pkgversion) {
						pkgversion2 = pkgname + j;
						sign2 = EQ;
						pkgversion2[0] = '\0';
						pkgversion2++;
						if (pkgversion2[0] == '=') {
							pkgversion2++;
							sign=EQ;
							j++;
						}
					} else {
						pkgversion = pkgname + j;
						sign = EQ;
						pkgversion[0] = '\0';
						pkgversion++;
						if (pkgversion[0] == '=') {
							pkgversion++;
							sign=EQ;
							j++;
						}
					}
				}
				j++;
			}
		} else
			pkgversion = NULL;

		if ((it = pkgdb_query(db, pkgname, match)) == NULL) {
			return (EX_IOERR);
		}

		/* this is place for compatibility hacks */

		/* ports infrastructure expects pkg info -q -O to always return 0 even
		 * if the ports doesn't exists */
		if (opt & INFO_ORIGIN_SEARCH)
			gotone = true;

		/* end of compatibility hacks */

		while ((ret = pkgdb_it_next(it, &pkg, query_flags)) == EPKG_OK) {
			gotone = true;
			const char *version;

			pkg_get(pkg, PKG_VERSION, &version);
			if (pkgversion != NULL) {
				switch (pkg_version_cmp(version, pkgversion)) {
					case -1:
						if (sign != LT && sign != LE) {
							gotone = false;
							continue;
						}
						break;
					case 0:
						if (sign != LE && sign != GE && sign != EQ) {
							gotone = false;
							continue;
						}
						break;
					case 1:
						if (sign != GT && sign != GE) {
							gotone = false;
							continue;
						}
						break;
				}
			}
			if (pkgversion2 != NULL) {
				switch(pkg_version_cmp(version, pkgversion2)) {
					case -1:
						if (sign2 != LT && sign2 != LE) {
							gotone = false;
							continue;
						}
						break;
					case 0:
						if (sign2 != LE && sign2 != GE && sign2 != EQ) {
							gotone = false;
							continue;
						}
						break;
					case 1:
						if (sign2 != GT && sign2 != GE) {
							gotone = false;
							continue;
						}
						break;
				}
			}
			if (opt & INFO_EXISTS)
				retcode = 0;
			else
				print_info(pkg, opt);
		}
		if (ret != EPKG_END) {
			retcode = 1;
		}

		if (retcode == 0 && !gotone && match != MATCH_ALL) {
			if ((opt & INFO_QUIET) == 0)
				warnx("No package(s) matching %s", argv[i]);
			retcode = EX_SOFTWARE;
		}

		pkgdb_it_free(it);

		i++;
	} while (i < argc);

	pkg_free(pkg);
	pkgdb_close(db);

	return (retcode);
}
