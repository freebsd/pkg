/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2013 Bryan Drewery <bdrewery@FreeBSD.org>
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

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pkg.h>

#include "pkgcli.h"

typedef struct _cliopt {
	const char *option;
	char key;
} cliopt;

/* an option string should not be a prefix of any other option string */ 
static const cliopt search_label[] = {
	{ "comment",     'c'  },
	{ "description", 'd'  },
	{ "name",        'n'  },
	{ "origin",      'o'  },
	{ "pkg-name",    'p'  },
	{ NULL,          '\0' },
};

static const cliopt modifiers[] = {
	{ "annotations",          'A'  },
	{ "arch",                 'a'  },
	{ "categories",           'C'  },
	{ "comment",              'c'  },
	{ "depends-on",           'd'  },
	{ "description",          'D'  },
	{ "full",                 'f'  },
	{ "licenses",             'l'  },
	{ "maintainer",           'm'  },
	{ "name",                 'n'  },
	{ "options",              'o'  },
	{ "pkg-size",	          'P'  },
	{ "prefix",               'p'  },
	{ "repository",           'R'  },
	{ "required-by",          'r'  },
	{ "shared-libs-required", 'B'  },
	{ "shared-libs-provided", 'b'  },
	{ "size",                 's'  },
	{ "url",                  'u'  },
	{ "version",              'v'  },
	{ "www",                  'w'  },
	{ NULL,                   '\0' },
};	

static char
match_optarg(const cliopt *optlist, const char *opt)
{
	int i, matched = -1;
	char key = '\0';
	size_t optlen;

	optlen = strlen(opt);

	/* Match any unique prefix from  optlist */
	for (i = 0; optlist[i].option != NULL; i++) {
		if (strncmp(opt, optlist[i].option, optlen) != 0)
			continue;
		if (matched > 0) {
			warnx("\"%s\" is ambiguous. Was "
			      "\"%s\" or \"%s\" meant?", opt,
			      optlist[matched].option, optlist[i].option);
			key = '\0';
			break;
		}
		matched = i;
		key = optlist[i].key;
	}
	return (key);
}

static pkgdb_field
search_label_opt(const char *optionarg)
{
	pkgdb_field field;

	/* label options */
	switch(match_optarg(search_label, optionarg)) {
	case 'o':
		field = FIELD_ORIGIN;
		break;
	case 'n':
		field = FIELD_NAME;
		break;
	case 'p':
		field = FIELD_NAMEVER;
		break;
	case 'c':
		field = FIELD_COMMENT;
		break;
	case 'd':
		field = FIELD_DESC;
		break;
	default:
		usage_search();
		errx(EXIT_FAILURE, "Unknown search/label option: %s", optionarg);
		/* NOTREACHED */
	}
	return field;
}

static unsigned int
modifier_opt(const char *optionarg)
{
	unsigned int opt;

	/* output modifiers */
	switch(match_optarg(modifiers, optionarg)) {
	case 'A':
		opt = INFO_ANNOTATIONS;
		break;
	case 'a':
		opt = INFO_ARCH;
		break;
	case 'C':
		opt = INFO_CATEGORIES;
		break;
	case 'c':
		opt = INFO_COMMENT;
		break;
	case 'd':
		opt = INFO_DEPS;
		break;
	case 'D':
		opt = INFO_DESCR;
		break;
	case 'f':
		opt = INFO_FULL;
		break;
	case 'l':
		opt = INFO_LICENSES;
		break;
	case 'm':
		opt = INFO_MAINTAINER;
		break;
	case 'n':
		opt = INFO_NAME;
		break;
	case 'o':
		opt = INFO_OPTIONS;
		break;
	case 'P':
		opt = INFO_PKGSIZE;
		break;
	case 'p':
		opt = INFO_PREFIX;
		break;
	case 'R':
		opt = INFO_REPOSITORY;
		break;
	case 'r':
		opt = INFO_RDEPS;
		break;
	case 'B':
		opt = INFO_SHLIBS_REQUIRED;
		break;
	case 'b':
		opt = INFO_SHLIBS_PROVIDED;
		break;
	case 's':
		opt = INFO_FLATSIZE;
		break;
	case 'u':
		opt = INFO_REPOURL;
		break;
	case 'v':
		opt = INFO_VERSION;
		break;
	case 'w':
		opt = INFO_WWW;
		break;
	default:
		usage_search();
		errx(EXIT_FAILURE, "Unkown modifier option %s", optionarg);
		/* NOTREACHED */
	}
	return opt;
}

void
usage_search(void)
{
	int i, n;

	fprintf(stderr, "Usage: pkg search [-eU] [-r repo] [-S search] "
	    "[-L label] [-Q mod]... [-Cgix] <pkg-name>\n");
	fprintf(stderr, "       pkg search [-cDdefopqRU] [-r repo] "
	    "[-Cgix] <pattern>\n\n");
	n = fprintf(stderr, "       Search and Label options:");
	for (i = 0; search_label[i].option != NULL; i++) {
		if (n > 72)
			n = fprintf(stderr, "\n            ");
		n += fprintf(stderr, " %s", search_label[i].option);
	}
	fprintf(stderr, "\n");
	n = fprintf(stderr, "       Output Modifiers:");
	for (i = 0; modifiers[i].option != NULL; i++) {
		if (n > 68)
			n = fprintf(stderr, "\n            ");
		n += fprintf(stderr, " %s", modifiers[i].option);
	}
	fprintf(stderr, "\n");
	fprintf(stderr, "For more information see 'pkg help search'.\n");
}

int
exec_search(int argc, char **argv)
{
	const char	*pattern = NULL;
	const char	*reponame = NULL;
	int		 ret = EPKG_OK, ch;
	int		 flags;
	uint64_t	 opt = 0;
	match_t		 match = MATCH_REGEX;
	pkgdb_field	 search = FIELD_NONE;
	pkgdb_field	 label = FIELD_NONE;
	struct pkgdb	*db = NULL;
	struct pkgdb_it	*it = NULL;
	struct pkg	*pkg = NULL;
	bool		 atleastone = false;
	bool		 old_quiet;

	struct option longopts[] = {
		{ "case-sensitive",	no_argument,		NULL,	'C' },
		{ "comment",		no_argument,		NULL,	'c' },
		{ "description",	no_argument,		NULL,	'D' },
		{ "depends-on",		no_argument,		NULL,	'd' },
		{ "exact",		no_argument,		NULL,	'e' },
		{ "full",		no_argument,		NULL,	'f' },
		{ "glob",		no_argument,		NULL,	'g' },
		{ "case-insensitive",	no_argument,		NULL,	'i' },
		{ "label",		required_argument,	NULL,	'L' },
		{ "origins",		no_argument,		NULL,	'o' },
		{ "prefix",		no_argument,		NULL,	'p' },
		{ "quiet",		no_argument,		NULL,	'q' },
		{ "query-modifier",	required_argument,	NULL,	'Q' },
		{ "repository",		required_argument,	NULL,	'r' },
		{ "raw",		no_argument,		NULL,	'R' },
		{ "search",		required_argument,	NULL,	'S' },
		{ "size",		no_argument,		NULL,	's' },
		{ "no-repo-update",	no_argument,		NULL,	'U' },
		{ "regex",		no_argument,		NULL,	'x' },
		{ "raw-format",		required_argument,	NULL, 	1   },
		{ NULL,			0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+CcDdefgiL:opqQ:r:RS:sUx", longopts, NULL)) != -1) {
		switch (ch) {
		case 'C':
			pkgdb_set_case_sensitivity(true);
			break;
		case 'c':	/* Same as -S comment */
			search = search_label_opt("comment");
			break;
		case 'D':	/* Same as -S description */
			search = search_label_opt("description");
			break;
		case 'd':	/* Same as -Q depends-on  */
			opt |= modifier_opt("depends-on");
			break;
		case 'e':
			match = MATCH_EXACT;
			break;
		case 'f':	/* Same as -Q full */
			opt |= modifier_opt("full");
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'i':
			pkgdb_set_case_sensitivity(false);
			break;
		case 'L':
			label = search_label_opt(optarg);
			break;
		case 'o':	/* Same as -L origin */
			label = search_label_opt("origin");
			break;
		case 'p':	/* Same as -Q prefix */
			opt |= modifier_opt("prefix");
			break;
		case 'q':
			quiet = true;
			break;
		case 'Q':
			opt |= modifier_opt(optarg);
			break;
		case 'r':
			reponame = optarg;
			break;
		case 'R':
			opt = INFO_RAW;
			break;
		case 'S':
			search = search_label_opt(optarg);
			break;
		case 's':	/* Same as -Q size */
			opt |= modifier_opt("size");
			break;
		case 'U':
			auto_update = false;
			break;
		case 'x':
			match = MATCH_REGEX;
			break;
		case 1:
			if (strcasecmp(optarg, "json") == 0)
				opt |= INFO_RAW_JSON;
			else if (strcasecmp(optarg, "json-compact") == 0)
				opt |= INFO_RAW_JSON_COMPACT;
			else if (strcasecmp(optarg, "yaml") == 0)
				opt |= INFO_RAW_YAML;
			else if (strcasecmp(optarg, "ucl") == 0)
				opt |= INFO_RAW_UCL;
			else
				errx(EXIT_FAILURE, "Invalid format '%s' for the "
				    "raw output, expecting json, json-compact "
				    "or yaml", optarg);
			break;
		default:
			usage_search();
			return (EXIT_FAILURE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		usage_search();
		return (EXIT_FAILURE);
	}

	pattern = argv[0];
	if (pattern[0] == '\0') {
		fprintf(stderr, "Pattern must not be empty.\n");
		return (EXIT_FAILURE);
	}
	if (search == FIELD_NONE) {
		if (strchr(pattern, '/') != NULL) {
			if (strchr(pattern, '@') != NULL)
				search = FIELD_FLAVOR;
			else
				search = FIELD_ORIGIN;
		} else
			search = FIELD_NAMEVER; /* Default search */
	}
	if (label == FIELD_NONE)
		label = search; /* By default, show what was searched  */

	switch(label) {
	case FIELD_NONE:
		break;		/* should never happen */
	case FIELD_ORIGIN:
		if (quiet) {
			opt = INFO_TAG_ORIGIN;
			quiet = false;
		} else {
			opt |= INFO_TAG_ORIGIN|INFO_COMMENT;
		}
		break;
	case FIELD_FLAVOR:
	case FIELD_NAME:
		opt |= INFO_TAG_NAME|INFO_COMMENT;
		break;
	case FIELD_NAMEVER:
		opt |= INFO_TAG_NAMEVER|INFO_COMMENT;
		break;
	case FIELD_COMMENT:
		opt |= INFO_TAG_NAMEVER|INFO_COMMENT;
		break;
	case FIELD_DESC:
		opt |= INFO_TAG_NAMEVER|INFO_DESCR;
		break;
	}

	if (quiet) {
		opt = INFO_TAG_NAMEVER;
		quiet = false;
	}

	ret = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_REPO);
	switch(ret) {
	case EPKG_ENOACCESS:
		warnx("Insufficient privileges to query the package database");
		return (EXIT_FAILURE);
	case EPKG_ENODB:
		if (!auto_update) {
			warnx("Unable to open remote repository catalogues. Try running '%s update' first.", getprogname());
			return (EXIT_FAILURE);
		}
		break;
	case EPKG_OK:
		break;
	default:
		return (EXIT_FAILURE);
	}

	/* first update the remote repositories if needed */
	old_quiet = quiet;
	quiet = true;
	if (auto_update && (ret = pkgcli_update(false, false, reponame)) != EPKG_OK)
		return (ret);
	quiet = old_quiet;

	if (pkgdb_open_all(&db, PKGDB_REMOTE, reponame) != EPKG_OK)
		return (EXIT_FAILURE);

	if ((it = pkgdb_repo_search(db, pattern, match, search, search,
	    reponame)) == NULL) {
		pkgdb_close(db);
		return (EXIT_FAILURE);
	}

	if (opt & INFO_RAW) {
		if ((opt & (INFO_RAW_JSON|INFO_RAW_JSON_COMPACT|INFO_RAW_UCL)) == 0)
			opt |= INFO_RAW_YAML;
	}

	flags = info_flags(opt, true);
	while ((ret = pkgdb_it_next(it, &pkg, flags)) == EPKG_OK) {
		print_info(pkg, opt);
		atleastone = true;
	}

	pkg_free(pkg);
	pkgdb_it_free(it);
	pkgdb_close(db);

	if (!atleastone)
		ret = EPKG_FATAL;

	return ((ret == EPKG_OK || ret == EPKG_END) ? EXIT_SUCCESS : EXIT_FAILURE);
}
