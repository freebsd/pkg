/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2013 Bryan Drewery <bdrewery@FreeBSD.org>
 * Copyright (c) 2013-2014 Matthew Seaman <matthew@FreeBSD.org>
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
#include <err.h>
#include <getopt.h>
#include <inttypes.h>
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
	{ 'O', "kvdD",		1, PKG_LOAD_OPTIONS },
	{ 'L', "",		1, PKG_LOAD_LICENSES },
	{ 'B', "",		1, PKG_LOAD_SHLIBS_REQUIRED },
	{ 'b', "",		1, PKG_LOAD_SHLIBS_PROVIDED },
	{ 'A', "tv",		1, PKG_LOAD_ANNOTATIONS },
	{ '?', "drCOLBbA",	1, PKG_LOAD_BASIC },	/* dbflags handled in analyse_query_string() */
	{ '#', "drCOLBbA",	1, PKG_LOAD_BASIC },	/* dbflags handled in analyse_query_string() */
	{ 's', "hb",		0, PKG_LOAD_BASIC },
	{ 'n', "",		0, PKG_LOAD_BASIC },
	{ 'e', "",		0, PKG_LOAD_BASIC },
	{ 'v', "",		0, PKG_LOAD_BASIC },
	{ 'o', "",		0, PKG_LOAD_BASIC },
	{ 'R', "",		0, PKG_LOAD_BASIC },
	{ 'p', "",		0, PKG_LOAD_BASIC },
	{ 'm', "",		0, PKG_LOAD_BASIC },
	{ 'c', "",		0, PKG_LOAD_BASIC },
	{ 'w', "",		0, PKG_LOAD_BASIC },
	{ 'l', "",		0, PKG_LOAD_BASIC },
	{ 'q', "",		0, PKG_LOAD_BASIC },
	{ 'M', "",		0, PKG_LOAD_BASIC }
};

void
usage_rquery(void)
{
	fprintf(stderr, "Usage: pkg rquery [-r reponame] [-I|<query-format>] <pkg-name>\n");
	fprintf(stderr, "       pkg rquery [-a] [-r reponame] [-I|<query-format>]\n");
	fprintf(stderr, "       pkg rquery -e <evaluation> [-r reponame] <query-format>\n");
	fprintf(stderr, "       pkg rquery [-Cgix] [-r reponame] [-I|<query-format>] <pattern> <...>\n\n");
	fprintf(stderr, "For more information see 'pkg help rquery.'\n");
}

static void
print_index(struct pkg *pkg, const char *portsdir)
{

	pkg_printf(
	    "%n-%v|"			/* PKGNAME */
	    "%S/%o|"			/* PORTDIR */
	    "%p|"			/* PREFIX */
	    "%c|"			/* COMMENT */
	    "%S/%o/pkg-descr|"		/* _DESCR */
	    "%m|"			/* MAINTAINER */
	    "%C%{%Cn%| %}|"		/* CATEGORIES */
	    "|"				/* BUILD_DEPENDS */
	    "%d%{%dn-%dv%| %}|"		/* RUN_DEPENDS */
	    "%w|"			/* WWW */
	    "|"				/* EXTRACT_DEPENDS */
	    "|"				/* PATCH_DEPENDS */
	    "\n",			/* FETCH_DEPENDS */
	    pkg, pkg, portsdir, pkg, pkg, pkg, portsdir, pkg, pkg, pkg, pkg,
	    pkg);
}

int
exec_rquery(int argc, char **argv)
{
	struct pkgdb		*db = NULL;
	struct pkgdb_it		*it = NULL;
	struct pkg		*pkg = NULL;
	char			*pkgname = NULL;
	int			 query_flags = PKG_LOAD_BASIC;
	match_t			 match = MATCH_EXACT;
	int			 ch;
	int			 ret = EPKG_OK;
	int			 retcode = EX_OK;
	int			 i;
	char			 multiline = 0;
	char			*condition = NULL;
	const char		*portsdir;
	struct sbuf		*sqlcond = NULL;
	const unsigned int	 q_flags_len = NELEM(accepted_rquery_flags);
	const char		*reponame = NULL;
	bool			 auto_update;
	bool			 onematched = false;
	bool			 old_quiet;
	bool			 index_output = false;

	struct option longopts[] = {
		{ "all",		no_argument,		NULL,	'a' },
		{ "case-sensitive",	no_argument,		NULL,	'C' },
		{ "evaluate",		required_argument,	NULL,	'e' },
		{ "glob",		no_argument,		NULL,	'g' },
		{ "case-insensitive",	no_argument,		NULL,	'i' },
		{ "index-line",		no_argument,		NULL,	'I' },
		{ "repository",		required_argument,	NULL,	'r' },
		{ "no-repo-update",	no_argument,		NULL,	'U' },
		{ "regex",		no_argument,		NULL,	'x' },
		{ NULL,			0,			NULL,	0   },
	};

	portsdir = pkg_object_string(pkg_config_get("PORTSDIR"));

	while ((ch = getopt_long(argc, argv, "+aCgiIxe:r:U", longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
			break;
		case 'C':
			pkgdb_set_case_sensitivity(true);
			break;
		case 'e':
			match = MATCH_CONDITION;
			condition = optarg;
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'i':
			pkgdb_set_case_sensitivity(false);
			break;
		case 'I':
			index_output = true;
			break;
		case 'r':
			reponame = optarg;
			break;
		case 'U':
			auto_update = false;
			break;
		case 'x':
			match = MATCH_REGEX;
			break;
		default:
			usage_rquery();
			return (EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0 && !index_output) {
		usage_rquery();
		return (EX_USAGE);
	}

	/* Default to all packages if no pkg provided */
	if (!index_output) {
		if (argc == 1 && condition == NULL && match == MATCH_EXACT) {
			match = MATCH_ALL;
		} else if (((argc == 1) ^ (match == MATCH_ALL )) && condition == NULL) {
			usage_rquery();
			return (EX_USAGE);
		}
	} else {
		if (argc == 0)
			match = MATCH_ALL;
	}

	if (!index_output && analyse_query_string(argv[0], accepted_rquery_flags, q_flags_len, &query_flags, &multiline) != EPKG_OK)
		return (EX_USAGE);

	if (condition != NULL) {
		sqlcond = sbuf_new_auto();
		if (format_sql_condition(condition, sqlcond, true) != EPKG_OK)
			return (EX_USAGE);
		sbuf_finish(sqlcond);
	}

	ret = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_REPO);
	if (ret == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to query the package database");
		return (EX_NOPERM);
	} else if (ret != EPKG_OK)
		return (EX_IOERR);

	/* first update the remote repositories if needed */
	old_quiet = quiet;
	quiet = true;
	if (auto_update && (ret = pkgcli_update(false, false, reponame)) != EPKG_OK)
		return (ret);
	quiet = old_quiet;

	ret = pkgdb_open_all(&db, PKGDB_REMOTE, reponame);
	if (ret != EPKG_OK)
		return (EX_IOERR);

	if (index_output)
		query_flags = PKG_LOAD_BASIC|PKG_LOAD_CATEGORIES|PKG_LOAD_DEPS;

	if (match == MATCH_ALL || match == MATCH_CONDITION) {
		const char *condition_sql = NULL;
		if (match == MATCH_CONDITION && sqlcond)
			condition_sql = sbuf_data(sqlcond);
		if ((it = pkgdb_repo_query(db, condition_sql, match, reponame)) == NULL)
			return (EX_IOERR);

		while ((ret = pkgdb_it_next(it, &pkg, query_flags)) == EPKG_OK) {
			if (index_output)
				print_index(pkg, portsdir);
			else
				print_query(pkg, argv[0],  multiline);
		}

		if (ret != EPKG_END)
			retcode = EX_SOFTWARE;

		pkgdb_it_free(it);
	} else {
		for (i = (index_output ? 0 : 1); i < argc; i++) {
			pkgname = argv[i];

			if ((it = pkgdb_repo_query(db, pkgname, match, reponame)) == NULL)
				return (EX_IOERR);

			while ((ret = pkgdb_it_next(it, &pkg, query_flags)) == EPKG_OK) {
				onematched = true;
				if (index_output)
					print_index(pkg, portsdir);
				else
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
