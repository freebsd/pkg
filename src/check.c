/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
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

#include <sys/param.h>
#include <sys/queue.h>

#include <err.h>
#include <assert.h>
#include <getopt.h>
#include <sysexits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pkg.h>

#include "pkgcli.h"

struct deps_entry {
	char *name;
	char *version;
	char *origin;
	STAILQ_ENTRY(deps_entry) next;
};

STAILQ_HEAD(deps_head, deps_entry);

static int check_deps(struct pkgdb *db, struct pkg *pkg, struct deps_head *dh, bool noinstall);
static void add_missing_dep(struct pkg_dep *d, struct deps_head *dh, int *nbpkgs);
static void deps_free(struct deps_head *dh);
static int fix_deps(struct pkgdb *db, struct deps_head *dh, int nbpkgs, bool yes);
static void check_summary(struct pkgdb *db, struct deps_head *dh);

static int
check_deps(struct pkgdb *db, struct pkg *p, struct deps_head *dh, bool noinstall)
{
	struct pkg_dep *dep = NULL;
	char *name, *version, *origin;
	int nbpkgs = 0;

	assert(db != NULL);
	assert(p != NULL);

	name = version = origin = NULL;
	pkg_get(p, PKG_NAME, &name, PKG_VERSION, &version, PKG_ORIGIN, &origin);

	while (pkg_deps(p, &dep) == EPKG_OK) {
		/* do we have a missing dependency? */
		if (pkg_is_installed(db, pkg_dep_origin(dep)) != EPKG_OK) {
			if (noinstall)
				printf("%s\n", pkg_dep_origin(dep));
			else
				printf("%s has a missing dependency: %s\n", origin,
			       pkg_dep_origin(dep));
			add_missing_dep(dep, dh, &nbpkgs);
		}
	}
	
	return (nbpkgs);
}

static void
add_missing_dep(struct pkg_dep *d, struct deps_head *dh, int *nbpkgs)
{
	struct deps_entry *e = NULL;
	const char *origin = NULL;

	assert(d != NULL);

	/* do not add duplicate entries in the queue */
	origin = pkg_dep_origin(d);

	STAILQ_FOREACH(e, dh, next)
		if (strcmp(e->origin, origin) == 0)
			return;

	if ((e = calloc(1, sizeof(struct deps_entry))) == NULL)
		err(1, "calloc(deps_entry)");

	e->name = strdup(pkg_dep_name(d));
	e->version = strdup(pkg_dep_version(d));
	e->origin = strdup(pkg_dep_origin(d));

	(*nbpkgs)++;

	STAILQ_INSERT_TAIL(dh, e, next);
}

static void
deps_free(struct deps_head *dh)
{
	struct deps_entry *e = NULL;

	while (!STAILQ_EMPTY(dh)) {
		e = STAILQ_FIRST(dh);
		STAILQ_REMOVE_HEAD(dh, next);
		free(e->name);
		free(e->version);
		free(e->origin);
		free(e);
	}
}

static int
fix_deps(struct pkgdb *db, struct deps_head *dh, int nbpkgs, bool yes)
{
	struct pkg_jobs *jobs = NULL;
	struct deps_entry *e = NULL;
	char **pkgs = NULL;
	int i = 0;
	bool rc;
	pkg_flags f = PKG_FLAG_AUTOMATIC;

	assert(db != NULL);
	assert(nbpkgs > 0);

	if ((pkgs = calloc(nbpkgs, sizeof (char *))) == NULL)
		err(1, "calloc()");

	STAILQ_FOREACH(e, dh, next)
		pkgs[i++] = e->origin;

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK) {
		free(pkgs);
		return (EPKG_ENODB);
	}

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_ADVISORY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get an advisory lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	if (pkg_jobs_new(&jobs, PKG_JOBS_INSTALL, db) != EPKG_OK) {
		goto cleanup;
	}

	pkg_jobs_set_flags(jobs, f);

	if (pkg_jobs_add(jobs, MATCH_EXACT, pkgs, nbpkgs) == EPKG_FATAL) {
		goto cleanup;
	}

	if (pkg_jobs_solve(jobs) != EPKG_OK) {
		goto cleanup;
	}

	if (pkg_jobs_count(jobs) == 0) {
		printf("\nUnable to find packages for installation.\n\n");
		goto cleanup;
	}

	/* print a summary before applying the jobs */
	print_jobs_summary(jobs, "The following packages will be installed:\n\n");
	
	rc = query_yesno(false, "\n>>> Try to fix the missing dependencies [y/N]: ");

	if (rc) {
		if (pkgdb_access(PKGDB_MODE_WRITE, PKGDB_DB_LOCAL) ==
		    EPKG_ENOACCESS) {
			warnx("Insufficient privileges to modify the package "
			      "database");

			goto cleanup;
		}

		pkg_jobs_apply(jobs);
	}

cleanup:
	if (pkgs != NULL)
		free(pkgs);
	if (jobs != NULL)
		pkg_jobs_free(jobs);
	pkgdb_release_lock(db, PKGDB_LOCK_ADVISORY);
	pkgdb_close(db);

	return (EPKG_OK);
}

static void
check_summary(struct pkgdb *db, struct deps_head *dh)
{
	struct deps_entry *e = NULL;
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	bool fixed = true;

	assert(db != NULL);

	printf(">>> Summary of actions performed:\n\n");
		
	STAILQ_FOREACH(e, dh, next) {
		if ((it = pkgdb_query(db, e->origin, MATCH_EXACT)) == NULL)
			return;
		
		if (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) != EPKG_OK) {
			fixed = false;
			printf("%s dependency failed to be fixed\n", e->origin);
		} else
			printf("%s dependency has been fixed\n", e->origin);

		pkgdb_it_free(it);
	}
	
	if (fixed) {
		printf("\n>>> Missing dependencies were fixed successfully.\n");
	} else {
		printf("\n>>> There are still missing dependencies.\n");
		printf(">>> You are advised to try fixing them manually.\n");
		printf("\n>>> Also make sure to check 'pkg updating' for known issues.\n");
	}

	pkg_free(pkg);
}

void
usage_check(void)
{
	fprintf(stderr, "Usage: pkg check [-Bdsr] [-vy] [-a | -Cgix <pattern>]\n\n");
	fprintf(stderr, "For more information see 'pkg help check'.\n");
}

int
exec_check(int argc, char **argv)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	struct pkgdb *db = NULL;
	match_t match = MATCH_EXACT;
	int flags = PKG_LOAD_BASIC;
	int ret, rc = EX_OK;
	int ch;
	bool dcheck = false;
	bool checksums = false;
	bool recompute = false;
	bool reanalyse_shlibs = false;
	bool noinstall = false;
	int nbpkgs = 0;
	int i;
	int verbose = 0;

	struct option longopts[] = {
		{ "all",		no_argument,	NULL,	'a' },
		{ "shlibs",		no_argument,	NULL,	'B' },
		{ "case-sensitive",	no_argument,	NULL,	'C' },
		{ "dependencies",	no_argument,	NULL,	'd' },
		{ "glob",		no_argument,	NULL,	'g' },
		{ "case-insensitive",	no_argument,	NULL,	'i' },
		{ "dry-run",		no_argument,	NULL,	'n' },
		{ "recompute",		no_argument,	NULL,	'r' },
		{ "checksums",		no_argument,	NULL,	's' },
		{ "verbose",		no_argument,	NULL,	'v' },
		{ "regex",		no_argument,	NULL,	'x' },
		{ "yes",		no_argument,	NULL,	'y' },
		{ NULL,			0,		NULL,	0   },
	};

	struct deps_head dh = STAILQ_HEAD_INITIALIZER(dh);

	while ((ch = getopt_long(argc, argv, "aBCdginrsvxy", longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
			break;
		case 'B':
			reanalyse_shlibs = true;
			flags |= PKG_LOAD_FILES;
			break;
		case 'C':
			pkgdb_set_case_sensitivity(true);
			break;
		case 'd':
			dcheck = true;
			flags |= PKG_LOAD_DEPS;
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'i':
			pkgdb_set_case_sensitivity(false);
			break;
		case 'n':
			noinstall = true;
			break;
		case 'r':
			recompute = true;
			flags |= PKG_LOAD_FILES;
			break;
		case 's':
			checksums = true;
			flags |= PKG_LOAD_FILES;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'x':
			match = MATCH_REGEX;
			break;
		case 'y':
			yes = true;
			break;
		default:
			usage_check();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	/* Default to all packages if no pkg provided */
	if (argc == 0 && (dcheck || checksums || recompute || reanalyse_shlibs)) {
		match = MATCH_ALL;
	} else if ((argc == 0 && match != MATCH_ALL) || !(dcheck || checksums || recompute || reanalyse_shlibs)) {
		usage_check();
		return (EX_USAGE);
	}

	if (recompute || reanalyse_shlibs)
		ret = pkgdb_access(PKGDB_MODE_READ|PKGDB_MODE_WRITE,
				   PKGDB_DB_LOCAL);
	else
		ret = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_LOCAL);

	if (ret == EPKG_ENODB) {
		warnx("No packages installed.  Nothing to do!");
		return (EX_OK);
	} else if (ret == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to access the package database");
		return (EX_NOPERM);
	} else if (ret != EPKG_OK) {
		warnx("Error accessing the package database");
		return (EX_SOFTWARE);
	}

	ret = pkgdb_open(&db, PKGDB_DEFAULT);
	if (ret != EPKG_OK)
		return (EX_IOERR);

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_ADVISORY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get an advisory lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	i = 0;
	do {
		if ((it = pkgdb_query(db, argv[i], match)) == NULL) {
			rc = EX_IOERR;
			goto cleanup;
		}

		while (pkgdb_it_next(it, &pkg, flags) == EPKG_OK) {
			/* check for missing dependencies */
			if (dcheck) {
				if (verbose)
					pkg_printf("Checking dependencies: %n\n", pkg);
				nbpkgs += check_deps(db, pkg, &dh, noinstall);
				if (noinstall && nbpkgs > 0) {
					rc = EX_UNAVAILABLE;
				}
			}
			if (checksums) {
				if (verbose)
					pkg_printf("Checking checksums: %n\n", pkg);
				if (pkg_test_filesum(pkg) != EPKG_OK) {
					rc = EX_DATAERR;
				}
			}
			if (recompute) {
				if (pkgdb_upgrade_lock(db, PKGDB_LOCK_ADVISORY,
						PKGDB_LOCK_EXCLUSIVE) == EPKG_OK) {
					if (verbose)
						pkg_printf("Recomputing size and checksums: %n\n", pkg);
					if (pkg_recompute(db, pkg) != EPKG_OK) {
						rc = EX_DATAERR;
					}
					pkgdb_release_lock(db, PKGDB_LOCK_EXCLUSIVE);
				}
				else {
					rc = EX_TEMPFAIL;
				}
			}
			if (reanalyse_shlibs) {
				if (pkgdb_upgrade_lock(db, PKGDB_LOCK_ADVISORY,
						PKGDB_LOCK_EXCLUSIVE) == EPKG_OK) {
					if (verbose)
						pkg_printf("Reanalyzing files for shlibs: %n\n", pkg);
					if (pkgdb_reanalyse_shlibs(db, pkg) != EPKG_OK) {
						pkg_printf("Failed to reanalyse for shlibs: %n\n", pkg);
						rc = EX_UNAVAILABLE;
					}
					pkgdb_release_lock(db, PKGDB_LOCK_EXCLUSIVE);
				}
				else {
					rc = EX_TEMPFAIL;
				}
			}
		}

		if (dcheck && nbpkgs > 0 && !noinstall) {
			printf("\n>>> Missing package dependencies were detected.\n");
			printf(">>> Found %d issue(s) in the package database.\n\n", nbpkgs);
			if (pkgdb_upgrade_lock(db, PKGDB_LOCK_ADVISORY,
					PKGDB_LOCK_EXCLUSIVE) == EPKG_OK) {
				ret = fix_deps(db, &dh, nbpkgs, yes);
				if (ret == EPKG_OK)
					check_summary(db, &dh);
				else if (ret == EPKG_ENODB) {
					db = NULL;
					rc = EX_IOERR;
				}
				pkgdb_release_lock(db, PKGDB_LOCK_EXCLUSIVE);
				if (rc == EX_IOERR)
					goto cleanup;
			}
			else {
				rc = EX_TEMPFAIL;
				goto cleanup;
			}
		}
		pkgdb_it_free(it);
		i++;
	} while (i < argc);

cleanup:
	deps_free(&dh);
	pkg_free(pkg);
	pkgdb_release_lock(db, PKGDB_LOCK_ADVISORY);
	pkgdb_close(db);

	return (rc);
}
