/*
 * Copyright (c) 2012 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <err.h>
#include <stdio.h>
#include <stdbool.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_set(void)
{
	fprintf(stderr, "usage pkg set -a [01] -yxXg <pattern>\n\n");
	fprintf(stderr, "For more information see 'pkg help set'. \n");
}

int
exec_set(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *pkg = NULL;
	int ch;
	int i;
	bool yes = false;
	match_t match = MATCH_EXACT;
	int newautomatic = -1;
	bool automatic = false;
	const char *errstr;
	const char *name;
	const char *version;

	while ((ch = getopt(argc, argv, "ya:kxXg")) != -1) {
		switch (ch) {
			case 'y':
				yes = true;
				break;
			case 'x':
				match = MATCH_REGEX;
				break;
			case 'X':
				match = MATCH_EREGEX;
				break;
			case 'g':
				match = MATCH_GLOB;
				break;
			case 'a':
				newautomatic = strtonum(optarg, 0, 1, &errstr);
				if (errstr)
					errx(EX_USAGE, "Wrong value for -a expecting 0 or 1, got: %s", errstr);
				break;
			default:
				usage_set();
				return (EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1 || newautomatic == -1) {
		usage_set();
		return (EX_USAGE);
	}

	if (geteuid() != 0) {
		warnx("modifying local database can only be done as root");
		return (EX_NOPERM);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (EX_IOERR);

	i = 0;
	do {
		if ((it = pkgdb_query(db, argv[i], match)) == NULL) {
			pkgdb_close(db);
			return (EX_IOERR);
		}

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
			pkg_get(pkg, PKG_AUTOMATIC, &automatic);
			if (automatic == newautomatic)
				continue;
			if (!yes)
				pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);
			if (!yes) {
				pkg_get(pkg, PKG_NAME, &name, PKG_VERSION, &version);
				if (newautomatic)
					yes = query_yesno("Mark %s-%s as automatically installed? [y/N]: ", name, version);
				else
					yes = query_yesno("Mark %s-%s as not automatically installed? [y/N]: ", name, version);
			}
			if (yes)
				pkgdb_set(db, pkg, PKG_AUTOMATIC, newautomatic);
		}
		i++;
	} while (i < argc);

	return (EXIT_SUCCESS);
}
