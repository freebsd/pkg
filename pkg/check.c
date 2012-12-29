/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/queue.h>

#include <err.h>
#include <assert.h>
#include <sysexits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libutil.h>

#include <pkg.h>

#include "pkgcli.h"

struct deps_entry {
	char *name;
	char *version;
	char *origin;
	STAILQ_ENTRY(deps_entry) next;
};

STAILQ_HEAD(deps_head, deps_entry);

static int check_deps(struct pkgdb *db, struct pkg *pkg, struct deps_head *dh);
static void add_missing_dep(struct pkg_dep *d, struct deps_head *dh, int *nbpkgs);
static void deps_free(struct deps_head *dh);
static int fix_deps(struct pkgdb *db, struct deps_head *dh, int nbpkgs, bool yes);
static void check_summary(struct pkgdb *db, struct deps_head *dh);

static int
check_deps(struct pkgdb *db, struct pkg *p, struct deps_head *dh)
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
			printf("%s has a missing dependency: %s\n", origin,
			       pkg_dep_origin(dep)),
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
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg_jobs *jobs = NULL;
	struct deps_entry *e = NULL;
	char **pkgs = NULL;
	int i = 0;

	assert(db != NULL);
	assert(nbpkgs > 0);

	if ((pkgs = calloc(nbpkgs, MAXPATHLEN + 1)) == NULL)
		err(1, "calloc()");

	STAILQ_FOREACH(e, dh, next)
		pkgs[i++] = e->origin;

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK)
		return (EPKG_ENODB);

	if (pkg_jobs_new(&jobs, PKG_JOBS_INSTALL, db, false, false) != EPKG_OK)
		free(pkgs);

	if ((it = pkgdb_query_installs(db, MATCH_EXACT, nbpkgs, pkgs, NULL, false, false)) == NULL) {
		free(pkgs);
		pkg_jobs_free(jobs);
	}

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_DEPS) == EPKG_OK) {
		pkg_set(pkg, PKG_AUTOMATIC, true);
		pkg_jobs_add(jobs, pkg);
		pkg = NULL;
	}

	if (pkg_jobs_is_empty(jobs)) {
		printf("\n>>> Unable to find packages for installation.\n\n");
		pkg_jobs_free(jobs);
		pkgdb_it_free(it);
		return (EPKG_FATAL);
	}

	/* print a summary before applying the jobs */
	pkg = NULL;

	print_jobs_summary(jobs, PKG_JOBS_INSTALL, "The following packages will be installed:\n\n");
	
	if (yes == false)
		yes = query_yesno("\n>>> Try to fix the missing dependencies [y/N]: ");

	if (yes == true)
		pkg_jobs_apply(jobs);

	free(pkgs);
	pkg_free(pkg);
	pkg_jobs_free(jobs);
	pkgdb_it_free(it);

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
	fprintf(stderr, "usage: pkg check [-Bdsr] [-vy] [-a | -gxX <pattern>]\n\n");
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
	int ret;
	int ch;
	bool yes = false;
	bool dcheck = false;
	bool checksums = false;
	bool recompute = false;
	bool reanalyse_shlibs = false;
	bool shlibs;
	int nbpkgs = 0;
	int i;
	int verbose = 0;

	struct deps_head dh = STAILQ_HEAD_INITIALIZER(dh);

	while ((ch = getopt(argc, argv, "yagdBxXsrv")) != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
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
		case 'y':
			yes = true;
			break;
		case 'd':
			dcheck = true;
			flags |= PKG_LOAD_DEPS;
			break;
		case 'B':
			pkg_config_bool(PKG_CONFIG_SHLIBS, &shlibs);
			if (!shlibs)
				errx(EX_USAGE, "reanalyzing shlibs requires SHLIBS"
					       " in pkg.conf.");
			reanalyse_shlibs = true;
			flags |= PKG_LOAD_FILES;
			break;
		case 's':
			checksums = true;
			flags |= PKG_LOAD_FILES;
			break;
		case 'r':
			recompute = true;
			flags |= PKG_LOAD_FILES;
			if (geteuid() != 0)
				errx(EX_USAGE, "recomputing the checksums"
				    " and size can only be done as root");
			break;
		case 'v':
			verbose = 1;
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

	ret = pkgdb_open(&db, PKGDB_DEFAULT);
	if (ret == EPKG_ENODB) {
		if (geteuid() == 0)
			return (EX_IOERR);

		return (EX_OK);
	}

	if (ret != EPKG_OK)
		return (EX_IOERR);

	i = 0;
	do {
		if ((it = pkgdb_query(db, argv[i], match)) == NULL) {
			pkgdb_close(db);
			return (EX_IOERR);
		}

		while (pkgdb_it_next(it, &pkg, flags) == EPKG_OK) {
			const char *pkgname = NULL;
			pkg_get(pkg, PKG_NAME, &pkgname);
			/* check for missing dependencies */
			if (dcheck) {
				if (verbose)
					printf("Checking dependencies: %s\n", pkgname);
				nbpkgs += check_deps(db, pkg, &dh);
			}
			if (checksums) {
				if (verbose)
					printf("Checking checksums: %s\n", pkgname);
				pkg_test_filesum(pkg);
			}
			if (recompute) {
				if (verbose)
					printf("Recomputing size and checksums: %s\n", pkgname);
				pkg_recompute(db, pkg);
			}
			if (reanalyse_shlibs) {
				if (verbose)
					printf("Reanalyzing files for shlibs: %s\n", pkgname);
				if (pkgdb_reanalyse_shlibs(db, pkg) != EPKG_OK)
					printf("Failed to reanalyse for shlibs: %s\n", pkgname);
			}
		}

		if (geteuid() == 0 && nbpkgs > 0) {
			if (yes == false)
				pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);

			printf("\n>>> Missing package dependencies were detected.\n");
			printf(">>> Found %d issue(s) in total with your package database.\n\n", nbpkgs);
			ret = fix_deps(db, &dh, nbpkgs, yes);
			if (ret == EPKG_OK)
				check_summary(db, &dh);
			else if (ret == EPKG_ENODB) {
				db = NULL;
				return (EX_IOERR);
			}
		}
		pkgdb_it_free(it);
		i++;
	} while (i < argc);

	deps_free(&dh);
	pkg_free(pkg);
	pkgdb_close(db);

	return (EX_OK);
}
