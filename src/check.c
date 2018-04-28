/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2016 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utlist.h>
#include <utstring.h>

#include <pkg.h>

#include "pkgcli.h"

struct deps_entry {
	char *name;
	struct deps_entry *next;
	struct deps_entry *prev;
};

static int check_deps(struct pkgdb *db, struct pkg *pkg, struct deps_entry **dh,
    bool noinstall, UT_string *out);
static void add_missing_dep(struct pkg_dep *d, struct deps_entry **dh, int *nbpkgs);
static void deps_free(struct deps_entry *dh);
static int fix_deps(struct pkgdb *db, struct deps_entry *dh, int nbpkgs);
static void check_summary(struct pkgdb *db, struct deps_entry *dh);

static int
check_deps(struct pkgdb *db, struct pkg *p, struct deps_entry **dh, bool noinstall, UT_string *out)
{
	struct pkg_dep *dep = NULL;
	struct pkgdb_it *it;
	char *buf = NULL;
	int nbpkgs = 0;

	assert(db != NULL);
	assert(p != NULL);

	while (pkg_deps(p, &dep) == EPKG_OK) {
		/* do we have a missing dependency? */
		if (pkg_is_installed(db, pkg_dep_name(dep)) != EPKG_OK) {
			if (quiet)
				pkg_utstring_printf(out, "%n\t%sn\n", p, dep);
			else
				pkg_utstring_printf(out, "%n has a missing dependency: %dn\n",
				    p, dep);
			if (!noinstall)
				add_missing_dep(dep, dh, &nbpkgs);
		}
	}

	/* checking libraries required */
	buf = NULL;
	while (pkg_shlibs_required(p, &buf) == EPKG_OK) {
		it = pkgdb_query_shlib_provide(db, buf);
		if (it != NULL && pkgdb_it_count(it) > 0) {
			pkgdb_it_free(it);
			continue;
		}
		pkgdb_it_free(it);
		if (quiet)
			pkg_utstring_printf(out, "%n\t%S\n", p, buf);
		else
			pkg_utstring_printf(out, "%n is missing a required shared library: %S\n",
			    p, buf);
	}

	/* checking requires */
	buf = NULL;
	while (pkg_requires(p, &buf) == EPKG_OK) {
		it = pkgdb_query_provide(db, buf);
		if (it != NULL && pkgdb_it_count(it) > 0) {
			pkgdb_it_free(it);
			continue;
		}
		pkgdb_it_free(it);
		if (quiet)
			pkg_utstring_printf(out, "%n\tS\n", p, buf);
		else
			pkg_utstring_printf(out, "%n has a missing requirement: %S\n",
			    p, buf);
	}

	return (nbpkgs);
}

static void
add_missing_dep(struct pkg_dep *d, struct deps_entry **dh, int *nbpkgs)
{
	struct deps_entry *e = NULL;
	const char *name = NULL;

	assert(d != NULL);

	/* do not add duplicate entries in the queue */
	name = pkg_dep_name(d);

	DL_FOREACH(*dh, e) {
		if (strcmp(e->name, name) == 0)
			return;
	}

	if ((e = calloc(1, sizeof(struct deps_entry))) == NULL)
		err(1, "calloc(deps_entry)");

	e->name = strdup(name);

	(*nbpkgs)++;

	DL_APPEND(*dh, e);
}

static void
deps_free(struct deps_entry *dh)
{
	struct deps_entry *e, *etmp;

	DL_FOREACH_SAFE(dh, e, etmp) {
		DL_DELETE(dh, e);
		free(e->name);
		free(e);
	}
}

static int
fix_deps(struct pkgdb *db, struct deps_entry *dh, int nbpkgs)
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

	DL_FOREACH(dh, e)
		pkgs[i++] = e->name;

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK) {
		free(pkgs);
		return (EPKG_ENODB);
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
	print_jobs_summary(jobs,
			"The following packages will be installed:\n\n");

	rc = query_yesno(false, "\n>>> Try to fix the missing dependencies? ");

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
	free(pkgs);
	if (jobs != NULL)
		pkg_jobs_free(jobs);

	return (EPKG_OK);
}

static void
check_summary(struct pkgdb *db, struct deps_entry *dh)
{
	struct deps_entry *e = NULL;
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	bool fixed = true;

	assert(db != NULL);

	printf(">>> Summary of actions performed:\n\n");

	DL_FOREACH(dh, e) {
		if ((it = pkgdb_query(db, e->name, MATCH_EXACT)) == NULL)
			return;

		if (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) != EPKG_OK) {
			fixed = false;
			printf("%s dependency failed to be fixed\n", e->name);
		} else
			printf("%s dependency has been fixed\n", e->name);

		pkgdb_it_free(it);
	}

	if (fixed) {
		printf("\n>>> Missing dependencies were fixed successfully.\n");
	} else {
		printf("\n>>> There are still missing dependencies.\n");
		printf(">>> Try fixing them manually.\n");
		printf("\n>>> Also make sure to check 'pkg updating' for known issues.\n");
	}

	pkg_free(pkg);
}

void
usage_check(void)
{
	fprintf(stderr, "Usage: pkg check [-Bdsr] [-qvy] [-a | -Cgix <pattern>]\n\n");
	fprintf(stderr, "For more information see 'pkg help check'.\n");
}

int
exec_check(int argc, char **argv)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	struct pkgdb *db = NULL;
	UT_string *msg = NULL;
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
	int i, processed, total = 0;
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
		{ "quiet",              no_argument,    NULL,   'q' },
		{ "regex",		no_argument,	NULL,	'x' },
		{ "yes",		no_argument,	NULL,	'y' },
		{ NULL,			0,		NULL,	0   },
	};

	struct deps_entry *dh = NULL;

	processed = 0;

	while ((ch = getopt_long(argc, argv, "+aBCdginqrsvxy", longopts, NULL)) != -1) {
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
			flags |= PKG_LOAD_DEPS|PKG_LOAD_REQUIRES|PKG_LOAD_SHLIBS_REQUIRED;
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
		case 'q':
			quiet = true;
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
		if (!quiet)
			warnx("No packages installed.  Nothing to do!");
		return (EX_OK);
	} else if (ret == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to access the package database");
		return (EX_NOPERM);
	} else if (ret != EPKG_OK) {
		warnx("Error accessing the package database");
		return (EX_SOFTWARE);
	}

	if (pkgdb_access(PKGDB_MODE_WRITE, PKGDB_DB_LOCAL) == EPKG_ENOACCESS) {
		warnx("Insufficient privileges");
		return (EX_NOPERM);
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
	nbdone = 0;
	do {
		/* XXX: This is really quirky, it would be cleaner to pass
		 * in multiple matches and only run this top-loop once. */
		if ((it = pkgdb_query(db, argv[i], match)) == NULL) {
			rc = EX_IOERR;
			goto cleanup;
		}
		nbactions = pkgdb_it_count(it);
		if (nbactions == 0 && match != MATCH_ALL) {
			warnx("No packages matching: %s", argv[i]);
			rc = EXIT_FAILURE;
			goto cleanup;
		}

		if (msg == NULL)
			utstring_new(msg);
		if (!verbose) {
			if (!quiet) {
				if (match == MATCH_ALL)
					progressbar_start("Checking all packages");
				else {
					utstring_printf(msg, "Checking %s", argv[i]);
					progressbar_start(utstring_body(msg));
				}
			}
			processed = 0;
			total = pkgdb_it_count(it);
		}

		UT_string *out;
		utstring_new(out);
		while (pkgdb_it_next(it, &pkg, flags) == EPKG_OK) {
			if (!quiet) {
				if (!verbose)
					progressbar_tick(processed, total);
				else {
					++nbdone;
					job_status_begin(msg);
					pkg_utstring_printf(msg, "Checking %n-%v:",
					    pkg, pkg);
					utstring_flush(msg);
				}
			}

			/* check for missing dependencies */
			if (dcheck) {
				if (!quiet && verbose)
					printf(" dependencies...");
				nbpkgs += check_deps(db, pkg, &dh, noinstall, out);
				if (noinstall && nbpkgs > 0) {
					rc = EX_UNAVAILABLE;
				}
			}
			if (checksums) {
				if (!quiet && verbose)
					printf(" checksums...");
				if (pkg_test_filesum(pkg) != EPKG_OK) {
					rc = EX_DATAERR;
				}
			}
			if (recompute) {
				if (pkgdb_upgrade_lock(db, PKGDB_LOCK_ADVISORY,
						PKGDB_LOCK_EXCLUSIVE) == EPKG_OK) {
					if (!quiet && verbose)
						printf(" recomputing...");
					if (pkg_recompute(db, pkg) != EPKG_OK) {
						rc = EX_DATAERR;
					}
					pkgdb_downgrade_lock(db,
					    PKGDB_LOCK_EXCLUSIVE,
					    PKGDB_LOCK_ADVISORY);
				}
				else {
					rc = EX_TEMPFAIL;
				}
			}
			if (reanalyse_shlibs) {
				if (pkgdb_upgrade_lock(db, PKGDB_LOCK_ADVISORY,
						PKGDB_LOCK_EXCLUSIVE) == EPKG_OK) {
					if (!quiet && verbose)
						printf(" shared libraries...");
					if (pkgdb_reanalyse_shlibs(db, pkg) != EPKG_OK) {
						pkg_fprintf(stderr, "Failed to "
						    "reanalyse for shlibs: "
						    "%n-%v\n", pkg, pkg);
						rc = EX_UNAVAILABLE;
					}
					pkgdb_downgrade_lock(db,
					    PKGDB_LOCK_EXCLUSIVE,
					    PKGDB_LOCK_ADVISORY);
				}
				else {
					rc = EX_TEMPFAIL;
				}
			}

			if (!quiet) {
				if (!verbose)
					++processed;
				else
					printf(" done\n");
			}
		}
		if (!quiet && !verbose)
			progressbar_tick(processed, total);
		if (utstring_len(out) > 0) {
			printf("%s", utstring_body(out));
		}
		utstring_free(out);
		if (msg != NULL) {
			utstring_free(msg);
			msg = NULL;
		}

		if (dcheck && nbpkgs > 0 && !noinstall) {
			printf("\n>>> Missing package dependencies were detected.\n");
			printf(">>> Found %d issue(s) in the package database.\n\n", nbpkgs);
			if (pkgdb_upgrade_lock(db, PKGDB_LOCK_ADVISORY,
					PKGDB_LOCK_EXCLUSIVE) == EPKG_OK) {
				ret = fix_deps(db, dh, nbpkgs);
				if (ret == EPKG_OK)
					check_summary(db, dh);
				else if (ret == EPKG_ENODB) {
					db = NULL;
					rc = EX_IOERR;
				}
				if (rc == EX_IOERR)
					goto cleanup;
				pkgdb_downgrade_lock(db, PKGDB_LOCK_EXCLUSIVE,
				    PKGDB_LOCK_ADVISORY);
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
	if (!verbose)
		progressbar_stop();
	if (msg != NULL)
		utstring_free(msg);
	deps_free(dh);
	pkg_free(pkg);
	pkgdb_release_lock(db, PKGDB_LOCK_ADVISORY);
	pkgdb_close(db);

	return (rc);
}
