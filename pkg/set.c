/*-
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
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <pkg.h>

#include "pkgcli.h"

#define AUTOMATIC 1U<<0
#define ORIGIN 1U<<1

void
usage_set(void)
{
	fprintf(stderr, "usage: pkg set [-a] [-A [01]] [-o <oldorigin>:<neworigin>] [-y] [-gix] <pkg-name>\n\n");
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
	bool yes;
	match_t match = MATCH_EXACT;
	int64_t newautomatic = -1;
	bool automatic = false;
	const char *errstr;
	char *neworigin = NULL;
	char *oldorigin = NULL;
	unsigned int loads = PKG_LOAD_BASIC;
	unsigned int sets = 0;
	int retcode;

	pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);

	while ((ch = getopt(argc, argv, "A:agio:xy")) != -1) {
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
			oldorigin = strdup(optarg);
			neworigin = strrchr(oldorigin, ':');
			if (neworigin == NULL) {
				free(oldorigin);
				errx(EX_USAGE, "Wrong format for -o. "
				    "Expecting oldorigin:neworigin, got: %s",
				    optarg);
			}
			*neworigin = '\0';
			neworigin++;
			if (strrchr(oldorigin, '/') == NULL ||
			    strrchr(neworigin, '/') == NULL) {
				free(oldorigin);
				errx(EX_USAGE,
				    "Bad origin format, got: %s", optarg);
			}
			break;
		case 'x':
			match = MATCH_REGEX;
			break;
		case 'y':
			yes = true;
			break;
		default:
			if (oldorigin != NULL)
				free(oldorigin);
			
			usage_set();
			return (EX_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if ((argc < 1 && match != MATCH_ALL) || (newautomatic == -1 && neworigin == NULL)) {
		usage_set();
		return (EX_USAGE);
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
		warnx("Insufficient privilege to modify package database");
		return (EX_NOPERM);
	} else if (retcode != EPKG_OK) {
		warnx("Error accessing package database");
		return (EX_SOFTWARE);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (EX_IOERR);
 
	if (oldorigin != NULL) {
		match = MATCH_ALL;
		if ((it = pkgdb_query(db, oldorigin, MATCH_EXACT)) == NULL) {
			pkgdb_close(db);
			return (EX_IOERR);
		}

		if (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) != EPKG_OK) {
			pkg = NULL;
/*			fprintf(stderr, "%s not installed\n", oldorigin);
			free(oldorigin);
			pkgdb_it_free(it);
			pkgdb_close(db);
			return (EX_SOFTWARE);*/
		}

		if (!yes) {
			if (pkg != NULL)
				yes = query_yesno("Change origin from %S to %S for %n-%v? [y/N]: ",
				    oldorigin, neworigin, pkg, pkg);
			else
				yes = query_yesno("Change origin from %S to %S for all dependencies? "
				    "[y/N]: ", oldorigin, neworigin);
		}
		if (pkg != NULL && yes) {
			if (pkgdb_set(db, pkg, PKG_SET_ORIGIN, neworigin) != EPKG_OK)
				return (EX_IOERR);
		}
		pkgdb_it_free(it);
	}
	i = 0;
	do {
		bool save_yes = yes;

		if ((it = pkgdb_query(db, argv[i], match)) == NULL) {
			free(oldorigin);
			pkgdb_close(db);
			return (EX_IOERR);
		}

		while (pkgdb_it_next(it, &pkg, loads) == EPKG_OK) {
			if ((sets & AUTOMATIC) == AUTOMATIC) {
				pkg_get(pkg, PKG_AUTOMATIC, &automatic);
				if (automatic == newautomatic)
					continue;
				if (!yes) {
					if (newautomatic)
						yes = query_yesno("Mark %n-%v as automatically installed? [y/N]: ", pkg, pkg);
					else
						yes = query_yesno("Mark %n-%v as not automatically installed? [y/N]: ", pkg, pkg);
				}
				if (yes)
					pkgdb_set(db, pkg, PKG_SET_AUTOMATIC, newautomatic);
				yes = save_yes;
			}
			if ((sets & ORIGIN) == ORIGIN) {
				struct pkg_dep *d = NULL;
				while (pkg_deps(pkg, &d) == EPKG_OK) {
					/*
					 * Do not query user when he has already
					 * been queried.
					 */
					if (pkgdb_set(db, pkg, PKG_SET_DEPORIGIN, oldorigin, neworigin) != EPKG_OK)
						return (EX_IOERR);
				}
			}
		}
		pkgdb_it_free(it);
		i++;
	} while (i < argc);

	free(oldorigin);
	pkg_free(pkg);
	pkgdb_close(db);

	return (EX_OK);
}
