/*-
 * Copyright (c) 2011-2025 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include <xmalloc.h>

#include "pkgcli.h"

static int check_deps(struct pkgdb *db, struct pkg *pkg, charv_t *dh,
    bool noinstall, sb_t *out);
static void add_missing_dep(struct pkg_dep *d, charv_t *dh, int *nbpkgs);
static int fix_deps(struct pkgdb *db, charv_t *dh, int nbpkgs);
static void check_summary(struct pkgdb *db, charv_t *dh);

static int
check_deps(struct pkgdb *db, struct pkg *p, charv_t *dh, bool noinstall, sb_t *out)
{
	struct pkg_dep *dep = NULL;
	struct pkgdb_it *it;
	const char *buf;
	int nbpkgs = 0;
	const char *pname = NULL;
	struct pkg_stringlist *sl = NULL;
	struct pkg_stringlist_iterator	*slit;
	struct pkgbase *pb;

	assert(db != NULL);
	assert(p != NULL);
	pkg_get(p, PKG_ATTR_NAME, &pname);

	while (pkg_deps(p, &dep) == EPKG_OK) {
		const char *depname = pkg_dep_name(dep);
		/* do we have a missing dependency? */
		if (pkg_is_installed(db, depname) != EPKG_OK) {
			if (quiet)
				sb_printf(out, "%s\t%s\n", pname, depname);
			else
				sb_printf(out, "%s has a missing dependency: %s\n",
				    pname, depname);
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
			sb_printf(out, "%s\t%s\n", pname, buf);
		else
			sb_printf(out, "%s depends on a missing or unregistered shared library: %s\n",
			    pname, buf);
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
			sb_printf(out, "%s\t%s\n", pname, buf);
		else
			sb_printf(out, "%s has a missing requirement: %s\n",
			    pname, buf);
	}
	pkgbase_free(pb);
	free(slit);
	free(sl);

	return (nbpkgs);
}

static void
add_missing_dep(struct pkg_dep *d, charv_t *dh, int *nbpkgs)
{
	const char *name = NULL;

	assert(d != NULL);

	/* do not add duplicate entries in the queue */
	name = pkg_dep_name(d);

	vec_foreach(*dh, i) {
		if (STREQ(dh->d[i], name))
			return;
	}
	(*nbpkgs)++;

	vec_push(dh, xstrdup(name));
}

static int
fix_deps(struct pkgdb *db, charv_t *dh, int nbpkgs)
{
	struct pkg_jobs *jobs = NULL;
	bool rc;
	pkg_flags f = PKG_FLAG_AUTOMATIC;

	assert(db != NULL);
	assert(nbpkgs > 0);

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK) {
		return (EPKG_ENODB);
	}

	if (pkg_jobs_new(&jobs, PKG_JOBS_INSTALL, db) != EPKG_OK) {
		goto cleanup;
	}

	pkg_jobs_set_flags(jobs, f);

	if (pkg_jobs_add(jobs, MATCH_EXACT, dh->d, dh->len) == EPKG_FATAL) {
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
	if (jobs != NULL)
		pkg_jobs_free(jobs);

	return (EPKG_OK);
}

static void
check_summary(struct pkgdb *db, charv_t *dh)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	bool fixed = true;

	assert(db != NULL);

	printf(">>> Summary of actions performed:\n\n");

	vec_foreach(*dh, i) {
		if ((it = pkgdb_query(db, dh->d[i], MATCH_EXACT)) == NULL)
			return;

		if (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) != EPKG_OK) {
			fixed = false;
			printf("%s dependency failed to be fixed\n", dh->d[i]);
		} else
			printf("%s dependency has been fixed\n", dh->d[i]);

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
	sb_t msg = sb_init();
	sb_t out = sb_init();
	match_t match = MATCH_EXACT;
	int flags = PKG_LOAD_BASIC;
	int ret, rc = EXIT_SUCCESS;
	int ch;
	bool dcheck = false;
	bool checksums = false;
	bool metadata = false;
	bool noinstall = false;
	int nbpkgs = 0;
	int i, processed, total = 0;
	int verbose = 0;
	int nbactions;
	charv_t dh = vec_init();

	struct option longopts[] = {
		{ "all",		no_argument,	NULL,	'a' },
		{ "shlibs",		no_argument,	NULL,	'B' },
		{ "case-sensitive",	no_argument,	NULL,	'C' },
		{ "dependencies",	no_argument,	NULL,	'd' },
		{ "glob",		no_argument,	NULL,	'g' },
		{ "case-insensitive",	no_argument,	NULL,	'i' },
		{ "metadata",		no_argument,	NULL,	'm' },
		{ "dry-run",		no_argument,	NULL,	'n' },
		{ "recompute",		no_argument,	NULL,	'r' },
		{ "checksums",		no_argument,	NULL,	's' },
		{ "verbose",		no_argument,	NULL,	'v' },
		{ "quiet",              no_argument,    NULL,   'q' },
		{ "regex",		no_argument,	NULL,	'x' },
		{ "yes",		no_argument,	NULL,	'y' },
		{ NULL,			0,		NULL,	0   },
	};

	processed = 0;

	while ((ch = getopt_long(argc, argv, "+aBCdgimnqrsvxy", longopts, NULL)) != -1) {
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
		case 'm':
			metadata = true;
			flags |= PKG_LOAD_FILES|PKG_LOAD_DIRS;
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

	if (!(dcheck || checksums || metadata)) {
		checksums = true;
		flags |= PKG_LOAD_FILES;
	}
	/* Default to all packages if no pkg provided */
	if (argc == 0 && (dcheck || checksums || metadata)) {
		match = MATCH_ALL;
	} else if ((argc == 0 && match != MATCH_ALL) || !(dcheck || checksums || metadata)) {
		usage_check();
		return (EXIT_FAILURE);
	}

	bool readonly = !dcheck || noinstall;
	int mode = readonly ? PKGDB_MODE_READ :
	    PKGDB_MODE_READ|PKGDB_MODE_WRITE;

	ret = pkgdb_access(mode, PKGDB_DB_LOCAL);

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

	ret = pkgdb_open(&db, readonly ? PKGDB_DEFAULT_READONLY : PKGDB_DEFAULT);
	if (ret != EPKG_OK)
		return (EXIT_FAILURE);

	i = 0;
	do {
		/* XXX: This is really quirky, it would be cleaner to pass
		 * in multiple matches and only run this top-loop once. */
		if ((it = pkgdb_query(db, argv[i], match)) == NULL) {
			rc = EXIT_FAILURE;
			break;
		}
		nbactions = pkgdb_it_count(it);
		if (nbactions == 0 && match != MATCH_ALL) {
			warnx("No packages matching: %s", argv[i]);
			rc = EXIT_FAILURE;
			pkgdb_it_free(it);
			it = NULL;
			break;
		}

		sb_reset(&msg);
		if (!verbose) {
			if (!quiet) {
				if (match == MATCH_ALL)
					progressbar_start("Checking all packages");
				else {
					sb_printf(&msg, "Checking %s", argv[i]);

					progressbar_start(sb_str(&msg));
				}
			}
			processed = 0;
			total = pkgdb_it_count(it);
		}

		sb_reset(&out);
		while (pkgdb_it_next(it, &pkg, flags) == EPKG_OK) {
			if (!quiet) {
				if (!verbose)
					progressbar_tick(processed, total);
				else {
					const char *n, *v;
					pkg_get(pkg, PKG_ATTR_NAME, &n);
					pkg_get(pkg, PKG_ATTR_VERSION, &v);
					job_status_begin(&msg);
					sb_printf(&msg, "Checking %s-%s:", n, v);

					printf("%s", sb_str(&msg));
					sb_reset(&msg);
				}
			}

			/* check for missing dependencies */
			if (dcheck) {
				if (!quiet && verbose)
					printf(" dependencies...");
				nbpkgs += check_deps(db, pkg, &dh, noinstall, &out);
				if (noinstall && nbpkgs > 0) {
					rc = EXIT_FAILURE;
				}
			}
			if (checksums || metadata) {
				if (!quiet && verbose)
					printf("%s%s", checksums ? " checksums..." : "",
					       metadata ? " metadata...": "");
				if (pkg_check_files(pkg, checksums, metadata) != EPKG_OK) {
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
		pkgdb_it_free(it);
		it = NULL;

		if (!quiet && !verbose)
			progressbar_tick(processed, total);

		if (out.len > 0 )
			printf("%s", sb_str(&out));

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
					break;
				pkgdb_downgrade_lock(db, PKGDB_LOCK_EXCLUSIVE,
				    PKGDB_LOCK_ADVISORY);
			}
			else {
				rc = EXIT_FAILURE;
				break;
			}
		}
		i++;
	} while (i < argc);
	assert(it == NULL);

	if (!verbose)
		progressbar_stop();
	sb_fini(&msg);
	sb_fini(&out);
	vec_free_and_free(&dh, free);
	pkg_free(pkg);
	pkgdb_close(db);

	return (rc);
}
