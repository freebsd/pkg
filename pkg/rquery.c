/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
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
#include <sys/sbuf.h>

#include <ctype.h>
#include <inttypes.h>
#include <libutil.h>
#include <pkg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "pkgcli.h"

static struct query_flags accepted_rquery_flags[] = {
	{ 'd', "nov",		1, PKG_LOAD_DEPS },
	{ 'r', "nov",		1, PKG_LOAD_RDEPS },
	{ 'C', "",		1, PKG_LOAD_CATEGORIES },
	{ 'O', "kv",		1, PKG_LOAD_OPTIONS },
	{ 'L', "",		1, PKG_LOAD_LICENSES },
	{ 'B', "",		1, PKG_LOAD_SHLIBS },
	{ '?', "drCOLB",	1, PKG_LOAD_BASIC },	/* dbflags handled in analyse_query_string() */
	{ 's', "hb",		0, PKG_LOAD_BASIC },
	{ 'n', "",		0, PKG_LOAD_BASIC },
	{ 'v', "",		0, PKG_LOAD_BASIC },
	{ 'o', "",		0, PKG_LOAD_BASIC },
	{ 'R', "",		0, PKG_LOAD_BASIC },
	{ 'p', "",		0, PKG_LOAD_BASIC },
	{ 'm', "",		0, PKG_LOAD_BASIC },
	{ 'c', "",		0, PKG_LOAD_BASIC },
	{ 'w', "",		0, PKG_LOAD_BASIC },
	{ 'l', "",		0, PKG_LOAD_BASIC },
	{ 'M', "",		0, PKG_LOAD_BASIC },
};

void
usage_rquery(void)
{
	fprintf(stderr, "usage: pkg rquery [-r reponame] <query-format> <pkg-name>\n");
	fprintf(stderr, "       pkg rquery [-a] [-r reponame] <query-format>\n");
	fprintf(stderr, "       pkg rquery -e <evaluation> [-r reponame] <query-format>\n");
	fprintf(stderr, "       pkg rquery [-gxX] [-r reponame] <query-format> <pattern> <...>\n\n");
	fprintf(stderr, "For more information see 'pkg help rquery.'\n");
}

int
exec_rquery(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *pkg = NULL;
	char *pkgname = NULL;
	int query_flags = PKG_LOAD_BASIC;
	match_t match = MATCH_EXACT;
	int ch;
	int ret = EPKG_OK;
	int retcode = EX_OK;
	int i;
	char multiline = 0;
	char *condition = NULL;
	struct sbuf *sqlcond = NULL;
	const unsigned int q_flags_len = (sizeof(accepted_rquery_flags)/sizeof(accepted_rquery_flags[0]));
	const char *reponame = NULL;
	bool onematched = false;

	while ((ch = getopt(argc, argv, "agxXe:r:")) != -1) {
		switch (ch) {
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
		case 'e':
			match = MATCH_CONDITION;
			condition = optarg;
			break;
		case 'r':
			reponame = optarg;
			break;
		default:
			usage_rquery();
			return (EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0) {
		usage_rquery();
		return (EX_USAGE);
	}

	/* Default to all packages if no pkg provided */
	if (argc == 1 && pkgname == NULL && condition == NULL && match == MATCH_EXACT) {
		match = MATCH_ALL;
	} else if ((argc == 1) ^ (match == MATCH_ALL) && condition == NULL) {
		usage_rquery();
		return (EX_USAGE);
	}

	if (analyse_query_string(argv[0], accepted_rquery_flags, q_flags_len, &query_flags, &multiline) != EPKG_OK)
		return (EX_USAGE);

	if (condition != NULL) {
		sqlcond = sbuf_new_auto();
		if (format_sql_condition(condition, sqlcond, true) != EPKG_OK)
			return (EX_USAGE);
		sbuf_finish(sqlcond);
	}

	ret = pkgdb_open(&db, PKGDB_REMOTE);
	if (ret == EPKG_ENODB) {
		if (geteuid() == 0)
			return (EX_IOERR);

		/* do not fail if run as a user */
		return (EX_OK);
	}

	if (ret != EPKG_OK)
		return (EX_IOERR);

	if (match == MATCH_ALL || match == MATCH_CONDITION) {
		const char *condition_sql = NULL;
		if (match == MATCH_CONDITION && sqlcond)
			condition_sql = sbuf_data(sqlcond);
		if ((it = pkgdb_rquery(db, condition_sql, match, reponame)) == NULL)
			return (EX_IOERR);

		while ((ret = pkgdb_it_next(it, &pkg, query_flags)) == EPKG_OK)
			print_query(pkg, argv[0],  multiline);

		if (ret != EPKG_END)
			retcode = EX_SOFTWARE;

		pkgdb_it_free(it);
	} else {
		for (i = 1; i < argc; i++) {
			pkgname = argv[i];

			if ((it = pkgdb_rquery(db, pkgname, match, reponame)) == NULL)
				return (EX_IOERR);

			while ((ret = pkgdb_it_next(it, &pkg, query_flags)) == EPKG_OK) {
				onematched = true;
				print_query(pkg, argv[0], multiline);
			}

			if (ret != EPKG_END) {
				retcode = EX_SOFTWARE;
				break;
			}

			pkgdb_it_free(it);
		}
		if (!onematched && retcode == EX_OK)
			retcode = EX_UNAVAILABLE;
	}

	pkg_free(pkg);
	pkgdb_close(db);

	return (retcode);
}
