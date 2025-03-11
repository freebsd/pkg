/*-
 * Copyright (c) 2011-2024 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2016 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/param.h>

#include <err.h>
#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pkg.h>
#include <tllist.h>
#include <xmalloc.h>

#include "pkgcli.h"

typedef tll(char *) deps_entries;

static int check_deps(struct pkgdb *db, struct pkg *pkg, deps_entries *dh,
    bool noinstall, xstring *out);
static void add_missing_dep(struct pkg_dep *d, deps_entries *dh, int *nbpkgs);
static int fix_deps(struct pkgdb *db, deps_entries *dh, int nbpkgs);
static void check_summary(struct pkgdb *db, deps_entries *dh);

static int
check_deps(struct pkgdb *db, struct pkg *p, deps_entries *dh, bool noinstall, xstring *out)
{
	struct pkg_dep *dep = NULL;
	struct pkgdb_it *it;
	const char *buf;
	int nbpkgs = 0;
	struct pkg_stringlist *sl = NULL;
	struct pkg_stringlist_iterator	*slit;
	struct pkgbase *pb;

	assert(db != NULL);
	assert(p != NULL);

	while (pkg_deps(p, &dep) == EPKG_OK) {
		/* do we have a missing dependency? */
		if (pkg_is_installed(db, pkg_dep_name(dep)) != EPKG_OK) {
			if (quiet)
				pkg_fprintf(out->fp, "%n\t%dn\n", p, dep);
			else
				pkg_fprintf(out->fp, "%n has a missing dependency: %dn\n",
				    p, dep);
			if (!noinstall)
				add_missing_dep(dep, dh, &nbpkgs);
		}
	}

	/* checking libraries required */
	pkg_get(p, PKG_ATTR_SHLIBS_REQUIRED, &sl);
	pb = pkgbase_new(db);
	slit = pkg_stringlist_iterator(sl);
	while ((buf = pkg_stringlist_next(slit))) {
		if (pkgbase_provide_shlib(pb, buf))
			continue;
		it = pkgdb_query_shlib_provide(db, buf);
		if (it != NULL && pkgdb_it_count(it) > 0) {
			pkgdb_it_free(it);
			continue;
		}
		pkgdb_it_free(it);
		if (quiet)
			pkg_fprintf(out->fp, "%n\t%S\n", p, buf);
		else
			pkg_fprintf(out->fp, "%n is missing a required shared library: %S\n",
			    p, buf);
	}
	free(slit);
	free(sl);

	/* checking requires */
	buf = NULL;
	pkg_get(p, PKG_ATTR_REQUIRES, &sl);
	slit = pkg_stringlist_iterator(sl);
	while ((buf = pkg_stringlist_next(slit))) {
		if (pkgbase_provide(pb, buf))
			continue;
		it = pkgdb_query_provide(db, buf);
		if (it != NULL && pkgdb_it_count(it) > 0) {
			pkgdb_it_free(it);
			continue;
		}
		pkgdb_it_free(it);
		if (quiet)
			pkg_fprintf(out->fp, "%n\t%S\n", p, buf);
		else
			pkg_fprintf(out->fp, "%n has a missing requirement: %S\n",
			    p, buf);
	}
	pkgbase_free(pb);
	free(slit);
	free(sl);

	return (nbpkgs);
}

static void
add_missing_dep(struct pkg_dep *d, deps_entries *dh, int *nbpkgs)
{
	const char *name = NULL;

	assert(d != NULL);

	/* do not add duplicate entries in the queue */
	name = pkg_dep_name(d);

	tll_foreach(*dh, it) {
		if (STREQ(it->item, name))
			return;
	}
	(*nbpkgs)++;

	tll_push_back(*dh, xstrdup(name));
}

static int
fix_deps(struct pkgdb *db, deps_entries *dh, int nbpkgs)
{
	struct pkg_jobs *jobs = NULL;
	char **pkgs = NULL;
	int i = 0;
	bool rc;
	pkg_flags f = PKG_FLAG_AUTOMATIC;

	assert(db != NULL);
	assert(nbpkgs > 0);

	if ((pkgs = calloc(nbpkgs, sizeof (char *))) == NULL)
		err(1, "calloc()");

	tll_foreach(*dh, it)
		pkgs[i++] = it->item;

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
check_summary(struct pkgdb *db, deps_entries *dh)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	bool fixed = true;

	assert(db != NULL);

	printf(">>> Summary of actions performed:\n\n");

	tll_foreach(*dh, e) {
		if ((it = pkgdb_query(db, e->item, MATCH_EXACT)) == NULL)
			return;

		if (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) != EPKG_OK) {
			fixed = false;
			printf("%s dependency failed to be fixed\n", e->item);
		} else
			printf("%s dependency has been fixed\n", e->item);

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
	fprintf(stderr,
	    "Usage: pkg check -d[n]|-s [-qvy] -a\n");
	fprintf(stderr,
	    "       pkg check -d[n]|-s [-qvy] [-Cgix] <pattern>\n\n");
	fprintf(stderr, "For more information see 'pkg help check'.\n");
}

int
exec_check(int argc, char **argv)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	struct pkgdb *db = NULL;
	xstring *msg = NULL;
	match_t match = MATCH_EXACT;
	int flags = PKG_LOAD_BASIC;
	int ret, rc = EXIT_SUCCESS;
	int ch;
	bool dcheck = false;
	bool checksums = false;
	bool noinstall = false;
	int nbpkgs = 0;
	int i, processed, total = 0;
	int verbose = 0;
	int nbactions;

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

	deps_entries dh = tll_init();

	processed = 0;

	while ((ch = getopt_long(argc, argv, "+aBCdginqrsvxy", longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
			break;
		case 'B':
			/* backward compatibility but do nothing */
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
			/* backward compatibility but do nothing */
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
			return (EXIT_FAILURE);
		}
	}
	argc -= optind;
	argv += optind;

	if (!(dcheck || checksums)) {
		checksums = true;
		flags |= PKG_LOAD_FILES;
	}
	/* Default to all packages if no pkg provided */
	if (argc == 0 && (dcheck || checksums)) {
		match = MATCH_ALL;
	} else if ((argc == 0 && match != MATCH_ALL) || !(dcheck || checksums)) {
		usage_check();
		return (EXIT_FAILURE);
	}

	ret = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_LOCAL);

	if (ret == EPKG_ENODB) {
		if (!quiet)
			warnx("No packages installed.  Nothing to do!");
		return (EXIT_SUCCESS);
	} else if (ret == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to access the package database");
		return (EXIT_FAILURE);
	} else if (ret != EPKG_OK) {
		warnx("Error accessing the package database");
		return (EXIT_FAILURE);
	}

	ret = pkgdb_open(&db, PKGDB_DEFAULT);
	if (ret != EPKG_OK)
		return (EXIT_FAILURE);

	i = 0;
	do {
		/* XXX: This is really quirky, it would be cleaner to pass
		 * in multiple matches and only run this top-loop once. */
		if ((it = pkgdb_query(db, argv[i], match)) == NULL) {
			rc = EXIT_FAILURE;
			goto cleanup;
		}
		nbactions = pkgdb_it_count(it);
		if (nbactions == 0 && match != MATCH_ALL) {
			warnx("No packages matching: %s", argv[i]);
			rc = EXIT_FAILURE;
			goto cleanup;
		}

		if (msg == NULL)
			msg = xstring_new();
		if (!verbose) {
			if (!quiet) {
				if (match == MATCH_ALL)
					progressbar_start("Checking all packages");
				else {
					fprintf(msg->fp, "Checking %s", argv[i]);
					fflush(msg->fp);
					progressbar_start(msg->buf);
				}
			}
			processed = 0;
			total = pkgdb_it_count(it);
		}

		xstring *out = xstring_new();
		while (pkgdb_it_next(it, &pkg, flags) == EPKG_OK) {
			if (!quiet) {
				if (!verbose)
					progressbar_tick(processed, total);
				else {
					job_status_begin(msg);
					pkg_fprintf(msg->fp, "Checking %n-%v:",
					    pkg, pkg);
					fflush(msg->fp);
					printf("%s", msg->buf);
					xstring_reset(msg);
				}
			}

			/* check for missing dependencies */
			if (dcheck) {
				if (!quiet && verbose)
					printf(" dependencies...");
				nbpkgs += check_deps(db, pkg, &dh, noinstall, out);
				if (noinstall && nbpkgs > 0) {
					rc = EXIT_FAILURE;
				}
			}
			if (checksums) {
				if (!quiet && verbose)
					printf(" checksums...");
				if (pkg_test_filesum(pkg) != EPKG_OK) {
					rc = EXIT_FAILURE;
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
		fflush(out->fp);
		if (out->buf[0] != '\0') {
			printf("%s", out->buf);
		}
		xstring_free(out);
		xstring_free(msg);
		msg = NULL;

		if (dcheck && nbpkgs > 0 && !noinstall) {
			printf("\n>>> Missing package dependencies were detected.\n");
			printf(">>> Found %d issue(s) in the package database.\n\n", nbpkgs);
			if (pkgdb_upgrade_lock(db, PKGDB_LOCK_ADVISORY,
					PKGDB_LOCK_EXCLUSIVE) == EPKG_OK) {
				ret = fix_deps(db, &dh, nbpkgs);
				if (ret == EPKG_OK)
					check_summary(db, &dh);
				else if (ret == EPKG_ENODB) {
					db = NULL;
					rc = EXIT_FAILURE;
				}
				if (rc == EXIT_FAILURE)
					goto cleanup;
				pkgdb_downgrade_lock(db, PKGDB_LOCK_EXCLUSIVE,
				    PKGDB_LOCK_ADVISORY);
			}
			else {
				rc = EXIT_FAILURE;
				goto cleanup;
			}
		}
		pkgdb_it_free(it);
		i++;
	} while (i < argc);

cleanup:
	if (!verbose)
		progressbar_stop();
	xstring_free(msg);
	tll_free_and_free(dh, free);
	pkg_free(pkg);
	pkgdb_close(db);

	return (rc);
}
