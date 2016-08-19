/*-
 * Copyright (c) 2011-2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#ifdef HAVE_CAPSICUM
#include <sys/capability.h>
#endif

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
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
	fprintf(stderr, "Usage: pkg info <pkg-name>\n");
	fprintf(stderr, "       pkg info -a\n");
	fprintf(stderr, "       pkg info [-AbBDdefIklOqRrs] [-Cgix] <pkg-name>\n");
	fprintf(stderr, "       pkg info [-AbBDdfIlqRrs] -F <pkg-file>\n\n");
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
	int query_flags;
	struct pkg *pkg = NULL;
	uint64_t opt = INFO_TAG_NAMEVER;
	match_t match = MATCH_GLOB;
	char *pkgname;
	char *pkgversion = NULL, *pkgversion2 = NULL;
	const char *file = NULL;
	int ch, fd;
	int ret = EPKG_OK;
	int retcode = 0;
	bool gotone = false;
	int i, j;
	int sign = 0;
	int sign2 = 0;
	int open_flags = 0;
	bool pkg_exists = false;
	bool origin_search = false;
	bool e_flag = false;
	struct pkg_manifest_key *keys = NULL;
#ifdef HAVE_CAPSICUM
	cap_rights_t rights;
#endif

	struct option longopts[] = {
		{ "all",		no_argument,		NULL,	'a' },
		{ "annotations",	no_argument,		NULL,	'A' },
		{ "provided-shlibs",	no_argument,		NULL,	'b' },
		{ "required-shlibs",	no_argument,		NULL,	'B' },
		{ "case-sensitive",	no_argument,		NULL,	'C' },
		{ "dependencies",	no_argument,		NULL,	'd' },
		{ "pkg-message",	no_argument,		NULL,	'D' },
		{ "exists",		no_argument,		NULL,	'e' },
		{ "show-name-only",	no_argument,		NULL,	'E' },
		{ "full",		no_argument,		NULL,	'f' },
		{ "file",		required_argument,	NULL,	'F' },
		{ "glob",		no_argument,		NULL,	'g' },
		{ "case-insensitive",	no_argument,		NULL,	'i' },
		{ "comment",		no_argument,		NULL,	'I' },
		{ "locked",		no_argument,		NULL,	'k' },
		{ "list-files",		no_argument,		NULL,	'l' },
		{ "origin",		no_argument,		NULL,	'o' },
		{ "by-origin",		no_argument,		NULL,	'O' },
		{ "prefix",		no_argument,		NULL,	'p' },
		{ "quiet",		no_argument,		NULL,	'q' },
		{ "required-by",	no_argument,		NULL,	'r' },
		{ "raw",		no_argument,		NULL,	'R' },
		{ "size",		no_argument,		NULL,	's' },
		{ "regex",		no_argument,		NULL,	'x' },
		{ "raw-format",		required_argument,	NULL, 	1   },
		{ NULL,			0,			NULL,	0   },
	};

	/* TODO: exclusive opts ? */
	while ((ch = getopt_long(argc, argv, "+aAbBCdDeEfF:giIkloOpqrRsx", longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
			break;
		case 'A':
			opt |= INFO_ANNOTATIONS;
			break;
		case 'b':
			opt |= INFO_SHLIBS_PROVIDED;
			break;
		case 'B':
			opt |= INFO_SHLIBS_REQUIRED;
			break;
		case 'C':
			pkgdb_set_case_sensitivity(true);
			break;
		case 'd':
			opt |= INFO_DEPS;
			break;
		case 'D':
			opt |= INFO_MESSAGE;
			break;
		case 'e':
			pkg_exists = true;;
			retcode = 1;
			break;
		case 'E': /* ports compatibility */
			e_flag = true;
			break;
		case 'f':
			opt |= INFO_FULL;
			break;
		case 'F':
			file = optarg;
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'i':
			pkgdb_set_case_sensitivity(false);
			break;
		case 'I':
			opt |= INFO_COMMENT;
			break;
		case 'k':
			opt |= INFO_LOCKED;
			break;
		case 'l':
			opt |= INFO_FILES;
			break;
		case 'o':
			opt |= INFO_ORIGIN;
			break;
		case 'O':
			origin_search = true;  /* only for ports compat */
			break;
		case 'p':
			opt |= INFO_PREFIX;
			break;
		case 'q':
			quiet = true;
			break;
		case 'r':
			opt |= INFO_RDEPS;
			break;
		case 'R':
			opt |= INFO_RAW;
			break;
		case 's':
			opt |= INFO_FLATSIZE;
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
				errx(EX_USAGE, "Invalid format '%s' for the "
				    "raw output, expecting json, json-compact "
				    "or yaml", optarg);
			break;
		default:
			usage_info();
			return(EX_USAGE);
		}
	}

	if (argc == 1 || (argc == 2 && quiet))
		match = MATCH_ALL;

	argc -= optind;
	argv += optind;

	if (argc == 0 && file == NULL && match != MATCH_ALL) {
		/* which -O bsd.*.mk always execpt clean output */
		if (origin_search)
			return (EX_OK);
		usage_info();
		return (EX_USAGE);
	}

	/* When no other data is requested, default is to print
	 * 'name-ver comment' For -O, just print name-ver */
	if (!origin_search && (opt & INFO_ALL) == 0 && match == MATCH_ALL &&
	    !quiet)
		opt |= INFO_COMMENT;

	/* Special compatibility: handle -O and -q -O */
	if (origin_search) {
		if (quiet) {
			opt = INFO_TAG_NAMEVER;
			quiet = false;
		} else {
			opt = INFO_TAG_NAMEVER|INFO_COMMENT;
		}
	}

	if (match == MATCH_ALL && opt == INFO_TAG_NAMEVER)
		quiet = false;

	if (file != NULL) {
		if ((fd = open(file, O_RDONLY)) == -1) {
			warn("Unable to open %s", file);
			return (EX_IOERR);
		}

		drop_privileges();
#ifdef HAVE_CAPSICUM
		cap_rights_init(&rights, CAP_READ, CAP_FSTAT);
		if (cap_rights_limit(fd, &rights) < 0 && errno != ENOSYS ) {
			warn("cap_rights_limit() failed");
			close(fd);
			return (EX_SOFTWARE);
		}

		if (cap_enter() < 0 && errno != ENOSYS) {
			warn("cap_enter() failed");
			close(fd);
			return (EX_SOFTWARE);
		}
#endif
		if (opt == INFO_TAG_NAMEVER)
			opt |= INFO_FULL;
		pkg_manifest_keys_new(&keys);
		if (opt & INFO_RAW) {
			if ((opt & (INFO_RAW_JSON|INFO_RAW_JSON_COMPACT|INFO_RAW_UCL)) == 0)
				opt |= INFO_RAW_YAML;
		}

		if ((opt & (INFO_RAW | INFO_FILES |
				INFO_DIRS)) == 0)
			open_flags = PKG_OPEN_MANIFEST_COMPACT;

		if (pkg_open_fd(&pkg, fd, keys, open_flags) != EPKG_OK) {
			close(fd);
			return (1);
		}
		pkg_manifest_keys_free(keys);
		print_info(pkg, opt);
		close(fd);
		pkg_free(pkg);
		return (EX_OK);
	}

	ret = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_LOCAL);
	if (ret == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to query the package database");
		return (EX_NOPERM);
	} else if (ret == EPKG_ENODB) {
		if (match == MATCH_ALL)
			return (EX_OK);
		if (origin_search)
			return (EX_OK);
		if (!quiet)
			warnx("No packages installed");
		return (EX_UNAVAILABLE);
	} else if (ret != EPKG_OK)
		return (EX_IOERR);
	ret = pkgdb_open(&db, PKGDB_DEFAULT);
	if (ret != EPKG_OK)
		return (EX_IOERR);

	drop_privileges();
	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

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
		}

		if (match != MATCH_ALL && pkgname[0] == '\0') {
			fprintf(stderr, "Pattern must not be empty.\n");
			i++;
			continue;
		}

		if ((it = pkgdb_query(db, pkgname, match)) == NULL) {
			goto cleanup;
		}

		/* this is place for compatibility hacks */

		/* ports infrastructure expects pkg info -q -O to
		 * always return 0 even if the ports doesn't exists */

		if (origin_search)
			gotone = true;

		/* end of compatibility hacks */

		/*
		 * only show full version in case of match glob with a
		 * single argument specified which does not contains
		 * any glob pattern
		 */
		if (argc == 1 && !origin_search && !quiet && !e_flag &&
		    match == MATCH_GLOB &&
		    strcspn(pkgname, "*[]{}()") == strlen(pkgname) &&
		    opt == INFO_TAG_NAMEVER)
			opt |= INFO_FULL;

		query_flags = info_flags(opt, false);
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
					if (sign != LE &&
					    sign != GE &&
					    sign != EQ) {
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
				switch (pkg_version_cmp(version, pkgversion2)) {
				case -1:
					if (sign2 != LT && sign2 != LE) {
						gotone = false;
						continue;
					}
					break;
				case 0:
					if (sign2 != LE &&
					    sign2 != GE &&
					    sign2 != EQ) {
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
			if (pkg_exists)
				retcode = EX_OK;
			else
				print_info(pkg, opt);
		}
		if (ret != EPKG_END) {
			retcode = EX_IOERR;
		}

		if (retcode == EX_OK && !gotone && match != MATCH_ALL) {
			if (!quiet)
				warnx("No package(s) matching %s", argv[i]);
			retcode = EX_SOFTWARE;
		}

		pkgdb_it_free(it);

		i++;
	} while (i < argc);

cleanup:
	pkg_free(pkg);

	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	pkgdb_close(db);

	return (retcode);
}
