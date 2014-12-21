/*-
 * Copyright (c) 2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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
#include <stdbool.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include <bsd_compat.h>

#include "pkgcli.h"

#define AUTOMATIC 1U<<0
#define ORIGIN 1U<<1
#define NAME 1U<<2

void
usage_set(void)
{
	fprintf(stderr, "Usage: pkg set [-a] [-A [01]] [-o <oldorigin>:<neworigin>] [-n <oldname>:<newname>] [-y] [-Cgix] <pkg-name>\n\n");
	fprintf(stderr, "For more information see 'pkg help set'. \n");
}

static bool
check_change_values(const char *opt, char **oldv, char **newv, char guard)
{
	const char *semicolon;

	if (opt == NULL)
		return (false);

	semicolon = strrchr(opt, ':');

	if (semicolon == NULL)
		return (false);

	*oldv = malloc(semicolon - opt + 1);
	strlcpy(*oldv, opt, semicolon - opt + 1);
	*newv = strdup(semicolon + 1);

	if (guard != '\0') {
		/* Check guard symbol in both new and old values */
		if (strrchr(*oldv, guard) == NULL ||
			strrchr(*newv, guard) == NULL) {
			free(*oldv);
			free(*newv);
			*oldv = NULL;
			*newv = NULL;

			return (false);
		}
	}

	return (true);
}

int
exec_set(int argc, char **argv)
{
	struct pkgdb	*db = NULL;
	struct pkgdb_it	*it = NULL;
	struct pkg	*pkg = NULL;
	int		 ch;
	int		 i;
	match_t		 match = MATCH_EXACT;
	int64_t		 newautomatic = -1;
	bool		 automatic = false;
	bool		 rc = false;
	const char	*errstr;
	const char	*changed = NULL;
	char		*newvalue = NULL;
	char		*oldvalue = NULL;
	unsigned int	 loads = PKG_LOAD_BASIC;
	unsigned int	 sets = 0;
	unsigned int	 field = 0, depfield = 0;
	int		 retcode;

	struct option longopts[] = {
		{ "automatic",		required_argument,	NULL,	'A' },
		{ "all",		no_argument,		NULL,	'a' },
		{ "case-sensitive",	no_argument,		NULL,	'C' },
		{ "glob",		no_argument,		NULL,	'g' },
		{ "case-insensitive",	no_argument,		NULL,	'i' },
		{ "change-origin",	required_argument,	NULL,	'o' },
		{ "change-name",	required_argument,	NULL,	'n' },
		{ "regex",		no_argument,		NULL,	'x' },
		{ "yes",		no_argument,		NULL,	'y' },
		{ NULL,			0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+A:aCgio:xyn:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'A':
			sets |= AUTOMATIC;
			newautomatic = strtonum(optarg, 0, 1, &errstr);
			if (errstr)
				errx(EX_USAGE, "Wrong value for -A. "
				    "Expecting 0 or 1, got: %s (%s)",
				    optarg, errstr);
			break;
		case 'a':
			match = MATCH_ALL;
			break;
		case 'C':
			pkgdb_set_case_sensitivity(true);
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'i':
			pkgdb_set_case_sensitivity(false);
			break;
		case 'o':
			sets |= ORIGIN;
			loads |= PKG_LOAD_DEPS;
			match = MATCH_ALL;
			changed = "origin";
			if (!check_change_values(optarg, &oldvalue, &newvalue, '/')) {
				 errx(EX_USAGE, "Wrong format for -o. "
					 "Expecting oldorigin:neworigin, got: %s",
					 optarg);
			}
			break;
		case 'n':
			sets |= NAME;
			loads |= PKG_LOAD_DEPS;
			match = MATCH_ALL;
			changed = "name";
			if (!check_change_values(optarg, &oldvalue, &newvalue, '\0')) {
				 errx(EX_USAGE, "Wrong format for -n. "
					 "Expecting oldname:newname, got: %s",
					 optarg);
			}
			break;
		case 'x':
			match = MATCH_REGEX;
			break;
		case 'y':
			yes = true;
			break;
		default:
			free(oldvalue);
			
			usage_set();
			return (EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if ((argc < 1 && match != MATCH_ALL) ||
		(newautomatic == -1 && newvalue == NULL) ||
		(sets & (NAME|ORIGIN)) == (NAME|ORIGIN)) {
		usage_set();
		return (EX_USAGE);
	}

	if (sets & NAME) {
		field = PKG_SET_NAME;
		depfield = PKG_SET_DEPNAME;
	}
	else if (sets & ORIGIN) {
		field = PKG_SET_ORIGIN;
		depfield = PKG_SET_DEPORIGIN;
	}

	retcode = pkgdb_access(PKGDB_MODE_READ|PKGDB_MODE_WRITE,
			       PKGDB_DB_LOCAL);
	if (retcode == EPKG_ENODB) {
		if (match == MATCH_ALL)
			return (EX_OK);
		if (!quiet)
			warnx("No packages installed.  Nothing to do!");
		return (EX_OK);
	} else if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to modify the package database");
		return (EX_NOPERM);
	} else if (retcode != EPKG_OK) {
		warnx("Error accessing package database");
		return (EX_SOFTWARE);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (EX_IOERR);

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_EXCLUSIVE) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get an exclusive lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	if (pkgdb_transaction_begin(db, NULL) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot start transaction for update");
		return (EX_TEMPFAIL);
	}

 
	if (oldvalue != NULL) {
		match = MATCH_ALL;
		if ((it = pkgdb_query(db, oldvalue, MATCH_EXACT)) == NULL) {
			retcode = EX_IOERR;
			goto cleanup;
		}

		if (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) != EPKG_OK) {
			pkg = NULL;
/*			fprintf(stderr, "%s not installed\n", oldorigin);
			free(oldorigin);
			pkgdb_it_free(it);
			pkgdb_close(db);
			return (EX_SOFTWARE);*/
		}

		rc = yes;
		if (!yes) {
			if (pkg != NULL)
				rc = query_yesno(false, "Change %S from %S to %S for %n-%v? [y/N]: ",
						changed, oldvalue, newvalue, pkg, pkg);
			else
				rc = query_yesno(false, "Change %S from %S to %S for all dependencies? "
						"[y/N]: ", changed, oldvalue, newvalue);
		}
		if (pkg != NULL && rc) {
			if (pkgdb_set(db, pkg, field, newvalue) != EPKG_OK) {
				retcode = EX_IOERR;
				goto cleanup;
			}
		}
		pkgdb_it_free(it);
	}
	i = 0;
	do {
		bool saved_rc = rc;

		if ((it = pkgdb_query(db, argv[i], match)) == NULL) {
			retcode = EX_IOERR;
			goto cleanup;
		}

		while (pkgdb_it_next(it, &pkg, loads) == EPKG_OK) {
			if ((sets & AUTOMATIC) == AUTOMATIC) {
				pkg_get(pkg, PKG_AUTOMATIC, &automatic);
				if (automatic == newautomatic)
					continue;
				if (!rc) {
					if (newautomatic)
						rc = query_yesno(false,
								"Mark %n-%v as automatically installed? [y/N]: ",
								pkg, pkg);
					else
						rc = query_yesno(false,
								"Mark %n-%v as not automatically installed? [y/N]: ",
								pkg, pkg);
				}
				if (rc)
					pkgdb_set(db, pkg, PKG_SET_AUTOMATIC, newautomatic);
				rc = saved_rc;
			}
			if (sets & (ORIGIN|NAME)) {
				struct pkg_dep *d = NULL;
				while (pkg_deps(pkg, &d) == EPKG_OK) {
					/*
					 * Do not query user when he has already
					 * been queried.
					 */
					if (pkgdb_set(db, pkg, depfield, oldvalue, newvalue) != EPKG_OK) {
						retcode = EX_IOERR;
						goto cleanup;
					}
				}
			}
		}
		pkgdb_it_free(it);
		i++;
	} while (i < argc);

cleanup:
	free(oldvalue);
	pkg_free(pkg);

	if (retcode == 0) {
		pkgdb_transaction_commit(db, NULL);
	}
	else {
		pkgdb_transaction_rollback(db, NULL);
	}

	pkgdb_release_lock(db, PKGDB_LOCK_EXCLUSIVE);
	pkgdb_close(db);

	return (retcode);
}
