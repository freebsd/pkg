/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
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
#include <sys/mount.h>

#include <assert.h>
#include <errno.h>
#include <libutil.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"

static int get_remote_pkg(struct pkg_jobs *j, const char *pattern, match_t m, bool root);
static struct pkg *get_local_pkg(struct pkg_jobs *j, const char *origin, unsigned flag);
static int pkg_jobs_fetch(struct pkg_jobs *j);
static bool newer_than_local_pkg(struct pkg_jobs *j, struct pkg *rp, bool force);
static bool new_pkg_version(struct pkg_jobs *j);
static int order_pool(struct pkg_jobs *j, bool force);

int
pkg_jobs_new(struct pkg_jobs **j, pkg_jobs_t t, struct pkgdb *db)
{
	assert(db != NULL);
	assert(t != PKG_JOBS_INSTALL || db->type == PKGDB_REMOTE);

	if ((*j = calloc(1, sizeof(struct pkg_jobs))) == NULL) {
		pkg_emit_errno("calloc", "pkg_jobs");
		return (EPKG_FATAL);
	}

	(*j)->db = db;
	(*j)->type = t;
	(*j)->solved = false;
	(*j)->flags = PKG_FLAG_NONE;

	return (EPKG_OK);
}

void
pkg_jobs_set_flags(struct pkg_jobs *j, pkg_flags flags)
{
	j->flags = flags;
}

int
pkg_jobs_set_repository(struct pkg_jobs *j, const char *ident)
{
	struct pkg_repo *r;

	if ((r = pkg_repo_find_ident(ident)) == NULL) {
		pkg_emit_error("Unknown repository: %s", ident);
		return (EPKG_FATAL);
	}

	j->reponame = pkg_repo_name(r);

	return (EPKG_OK);
}

void
pkg_jobs_free(struct pkg_jobs *j)
{
	if (j == NULL)
		return;

	if ((j->flags & PKG_FLAG_DRY_RUN) == 0)
		pkgdb_release_lock(j->db);

	HASH_FREE(j->jobs, pkg, pkg_free);
	LL_FREE(j->patterns, job_pattern, free);

	free(j);
}

int
pkg_jobs_add(struct pkg_jobs *j, match_t match, char **argv, int argc)
{
	struct job_pattern *jp;
	int i = 0;

	if (j->solved) {
		pkg_emit_error("The job has already been solved. "
		    "Impossible to append new elements");
		return (EPKG_FATAL);
	}

	for (i = 0; i < argc; i++) {
		jp = malloc(sizeof(struct job_pattern));
		jp->pattern = argv[i];
		jp->match = match;
		LL_APPEND(j->patterns, jp);
	}

	if (argc == 0 && match == MATCH_ALL) {
		jp = malloc(sizeof(struct job_pattern));
		jp->pattern = NULL;
		jp->match = match;
		LL_APPEND(j->patterns, jp);
	}

	return (EPKG_OK);
}

static int
populate_local_rdeps(struct pkg_jobs *j, struct pkg *p)
{
	struct pkg *pkg;
	struct pkg_dep *d = NULL;
	char *origin;

	while (pkg_rdeps(p, &d) == EPKG_OK) {
		HASH_FIND_STR(j->bulk, __DECONST(char *, pkg_dep_get(d, PKG_DEP_ORIGIN)), pkg);
		if (pkg != NULL)
			continue;
		HASH_FIND_STR(j->seen, __DECONST(char *, pkg_dep_get(d, PKG_DEP_ORIGIN)), pkg);
		if (pkg != NULL)
			continue;
		if ((pkg = get_local_pkg(j, pkg_dep_get(d, PKG_DEP_ORIGIN), PKG_LOAD_BASIC|PKG_LOAD_RDEPS)) == NULL) {
			pkg_emit_error("Missing reverse dependency matching '%s'", pkg_dep_get(d, PKG_DEP_ORIGIN));
			return (EPKG_FATAL);
		}
		pkg_get(pkg, PKG_ORIGIN, &origin);
		HASH_ADD_KEYPTR(hh, j->bulk, origin, strlen(origin), pkg);
		populate_local_rdeps(j, pkg);
	}

	return (EPKG_OK);
}

static void
remove_from_rdeps(struct pkg_jobs *j, const char *origin)
{
	struct pkg *pkg, *tmp;
	struct pkg_dep *d;

	HASH_ITER(hh, j->bulk, pkg, tmp) {
		HASH_FIND_STR(pkg->rdeps, __DECONST(char *, origin), d);
		if (d != NULL) {
			HASH_DEL(pkg->rdeps, d);
			pkg_dep_free(d);
		}
	}
}

static int
reverse_order_pool(struct pkg_jobs *j, bool force)
{
	struct pkg *pkg, *tmp;
	struct pkg_dep *d, *dtmp;
	char *origin;
	unsigned int nb;
	struct sbuf *errb;

	nb = HASH_COUNT(j->bulk);
	HASH_ITER(hh, j->bulk, pkg, tmp) {
		pkg_get(pkg, PKG_ORIGIN, &origin);
		if (HASH_COUNT(pkg->rdeps) == 0) {
			HASH_DEL(j->bulk, pkg);
			HASH_ADD_KEYPTR(hh, j->jobs, origin, strlen(origin), pkg);
			remove_from_rdeps(j, origin);
		}
	}

	if (nb == HASH_COUNT(j->bulk)) {
		errb = sbuf_new_auto();
		HASH_ITER(hh, j->bulk, pkg, tmp) {
			pkg_get(pkg, PKG_ORIGIN, &origin);
			sbuf_printf(errb, "%s: ", origin);
			HASH_ITER(hh, pkg->rdeps, d, dtmp) {
				if (d->hh.next != NULL)
					sbuf_printf(errb, "%s, ", pkg_dep_get(d, PKG_DEP_ORIGIN));
				else
					sbuf_printf(errb, "%s\n", pkg_dep_get(d, PKG_DEP_ORIGIN));
			}
			if (force) {
				HASH_DEL(j->bulk, pkg);
				HASH_ADD_KEYPTR(hh, j->jobs, origin, strlen(origin), pkg);
				remove_from_rdeps(j, origin);
			}

		}
		sbuf_finish(errb);
		if (!force) {
			pkg_emit_error("Error while trying to delete packages, "
					"dependencies that are still required:\n%s", sbuf_data(errb));
			sbuf_delete(errb);
			return (EPKG_FATAL);
		}
		else {
			pkg_emit_notice("You are trying to delete package(s) which has "
							"dependencies that are still required:\n%s"
							"... delete these packages anyway in forced mode",
							sbuf_data(errb));
			sbuf_delete(errb);
			return (EPKG_END);
		}
	}

	return (EPKG_OK);
}
static int
jobs_solve_deinstall(struct pkg_jobs *j)
{
	struct job_pattern *jp = NULL;
	struct pkg *pkg = NULL;
	struct pkg *tmp, *p;
	struct pkg_dep *d, *dtmp;
	struct pkgdb_it *it;
	int64_t oldsize;
	char *origin;
	int ret;
	bool recursive = false;

	if ((j->flags & PKG_FLAG_RECURSIVE) == PKG_FLAG_RECURSIVE)
		recursive = true;

	LL_FOREACH(j->patterns, jp) {
		if ((it = pkgdb_query(j->db, jp->pattern, jp->match)) == NULL)
			return (EPKG_FATAL);

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_RDEPS) == EPKG_OK) {
			pkg_get(pkg, PKG_ORIGIN, &origin, PKG_FLATSIZE, &oldsize);
			pkg_set(pkg, PKG_OLD_FLATSIZE, oldsize, PKG_FLATSIZE, (int64_t)0);
			HASH_ADD_KEYPTR(hh, j->bulk, origin, strlen(origin), pkg);
			if (recursive)
				populate_local_rdeps(j, pkg);
			pkg = NULL;
		}
		pkgdb_it_free(it);
	}

	/* remove everything seen from deps */
	HASH_ITER(hh, j->bulk, pkg, tmp) {
		d = NULL;
		HASH_ITER(hh, pkg->rdeps, d, dtmp) {
			HASH_FIND_STR(j->seen, __DECONST(char *, pkg_dep_get(d, PKG_DEP_ORIGIN)), p);
			if (p != NULL) {
				HASH_DEL(pkg->rdeps, d);
				pkg_dep_free(d);
			}
		}
	}
	HASH_FREE(j->seen, pkg, pkg_free);

	while (HASH_COUNT(j->bulk) > 0) {
		if ((ret = reverse_order_pool(j, (j->flags & PKG_FLAG_FORCE) == PKG_FLAG_FORCE))
				!= EPKG_OK) {
			if (ret == EPKG_END)
				break;
			else
				return (EPKG_FATAL);
		}
	}

	j->solved = true;

	return( EPKG_OK);
}

static bool
recursive_autoremove(struct pkg_jobs *j)
{
	struct pkg *pkg1, *tmp1;
	int64_t oldsize;
	char *origin;

	HASH_ITER(hh, j->bulk, pkg1, tmp1) {
		if (HASH_COUNT(pkg1->rdeps) == 0) {
			HASH_DEL(j->bulk, pkg1);
			pkg_get(pkg1, PKG_ORIGIN, &origin, PKG_FLATSIZE, &oldsize);
			pkg_set(pkg1, PKG_OLD_FLATSIZE, oldsize, PKG_FLATSIZE, (int64_t)0);
			HASH_ADD_KEYPTR(hh, j->jobs, origin, strlen(origin), pkg1);
			remove_from_rdeps(j, origin);
			return (true);
		}
	}

	return (false);
}

static int
jobs_solve_autoremove(struct pkg_jobs *j)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	char *origin;

	if ((it = pkgdb_query(j->db, " WHERE automatic=1 ", MATCH_CONDITION)) == NULL)
		return (EPKG_FATAL);

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_RDEPS) == EPKG_OK) {
		pkg_get(pkg, PKG_ORIGIN, &origin);
		HASH_ADD_KEYPTR(hh, j->bulk, origin, strlen(origin), pkg);
		pkg = NULL;
	}
	pkgdb_it_free(it);

	while (recursive_autoremove(j));

	HASH_FREE(j->bulk, pkg, pkg_free);

	j->solved = true;

	return (EPKG_OK);
}

static int
jobs_solve_upgrade(struct pkg_jobs *j)
{
	struct pkg *pkg = NULL;
	struct pkg *p, *tmp;
	struct pkgdb_it *it;
	char *origin;
	struct pkg_dep *d, *dtmp;
	int ret;

	if ((j->flags & PKG_FLAG_PKG_VERSION_TEST) != PKG_FLAG_PKG_VERSION_TEST)
		if (new_pkg_version(j)) {
			pkg_emit_newpkgversion();
			goto order;
		}

	if ((it = pkgdb_query(j->db, NULL, MATCH_ALL)) == NULL)
		return (EPKG_FATAL);

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		pkg_get(pkg, PKG_ORIGIN, &origin);
		/* Do not test we ignore what doesn't exists remotely */
		get_remote_pkg(j, origin, MATCH_EXACT, false);
		pkg = NULL;
	}
	pkgdb_it_free(it);

	/* remove everything seen from deps */
	HASH_ITER(hh, j->bulk, pkg, tmp) {
		d = NULL;
		HASH_ITER(hh, pkg->deps, d, dtmp) {
			HASH_FIND_STR(j->seen, __DECONST(char *, pkg_dep_get(d, PKG_DEP_ORIGIN)), p);
			if (p != NULL) {
				HASH_DEL(pkg->deps, d);
				pkg_dep_free(d);
			}
		}
	}
order:
	HASH_FREE(j->seen, pkg, pkg_free);

	/* now order the pool */
	while (HASH_COUNT(j->bulk) > 0) {
		/* XXX: see comment at jobs_solve_install */
		ret = order_pool(j, false);
		if (ret == EPKG_FATAL)
			return (EPKG_FATAL);
		else if (ret == EPKG_END)
			break;
	}

	j->solved = true;

	return (EPKG_OK);
}

static void
remove_from_deps(struct pkg_jobs *j, const char *origin)
{
	struct pkg *pkg, *tmp;
	struct pkg_dep *d;

	HASH_ITER(hh, j->bulk, pkg, tmp) {
		HASH_FIND_STR(pkg->deps, __DECONST(char *, origin), d);
		if (d != NULL) {
			HASH_DEL(pkg->deps, d);
			pkg_dep_free(d);
		}
	}
}

static int
order_pool(struct pkg_jobs *j, bool force)
{
	struct pkg *pkg, *tmp;
	char *origin;
	unsigned int nb;
	struct sbuf *errb;
	struct pkg_dep *d, *dtmp;

	nb = HASH_COUNT(j->bulk);
	HASH_ITER(hh, j->bulk, pkg, tmp) {
		pkg_get(pkg, PKG_ORIGIN, &origin);
		if (HASH_COUNT(pkg->deps) == 0) {
			HASH_DEL(j->bulk, pkg);
			HASH_ADD_KEYPTR(hh, j->jobs, origin, strlen(origin), pkg);
			remove_from_deps(j, origin);
		}
	}

	if (nb == HASH_COUNT(j->bulk)) {
		errb = sbuf_new_auto();
		HASH_ITER(hh, j->bulk, pkg, tmp) {
			pkg_get(pkg, PKG_ORIGIN, &origin);
			sbuf_printf(errb, "%s: ", origin);
			HASH_ITER(hh, pkg->deps, d, dtmp) {
				if (d->hh.next != NULL)
					sbuf_printf(errb, "%s, ", pkg_dep_get(d, PKG_DEP_ORIGIN));
				else
					sbuf_printf(errb, "%s\n", pkg_dep_get(d, PKG_DEP_ORIGIN));
			}
			if (force) {
				HASH_DEL(j->bulk, pkg);
				HASH_ADD_KEYPTR(hh, j->jobs, origin, strlen(origin), pkg);
				remove_from_rdeps(j, origin);
			}

		}
		sbuf_finish(errb);
		if (force) {
			pkg_emit_notice("Warning while trying to install/upgrade packages, "
					"as there are unresolved dependencies, "
					"but installation is forced:\n%s",
					sbuf_data(errb));
			sbuf_delete(errb);
			return (EPKG_END);
		}
		else {
			pkg_emit_error("Error while trying to install/upgrade packages, "
					"as there are unresolved dependencies:\n%s", sbuf_data(errb));
			sbuf_delete(errb);
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

static int
populate_rdeps(struct pkg_jobs *j, struct pkg *p)
{
	struct pkg *pkg;
	struct pkg_dep *d = NULL;

	while (pkg_rdeps(p, &d) == EPKG_OK) {
		HASH_FIND_STR(j->bulk, __DECONST(char *, pkg_dep_get(d, PKG_DEP_ORIGIN)), pkg);
		if (pkg != NULL)
			continue;
		HASH_FIND_STR(j->seen, __DECONST(char *, pkg_dep_get(d, PKG_DEP_ORIGIN)), pkg);
		if (pkg != NULL)
			continue;
		if (get_remote_pkg(j, pkg_dep_get(d, PKG_DEP_ORIGIN), MATCH_EXACT, true) != EPKG_OK) {
			pkg_emit_error("Missing reverse dependency matching '%s'", pkg_dep_get(d, PKG_DEP_ORIGIN));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

static int
populate_deps(struct pkg_jobs *j, struct pkg *p)
{
	struct pkg *pkg;
	struct pkg_dep *d = NULL;

	while (pkg_deps(p, &d) == EPKG_OK) {
		HASH_FIND_STR(j->bulk, __DECONST(char *, pkg_dep_get(d, PKG_DEP_ORIGIN)), pkg);
		if (pkg != NULL)
			continue;
		HASH_FIND_STR(j->seen, __DECONST(char *, pkg_dep_get(d, PKG_DEP_ORIGIN)), pkg);
		if (pkg != NULL)
			continue;
		if (get_remote_pkg(j, pkg_dep_get(d, PKG_DEP_ORIGIN), MATCH_EXACT, false) != EPKG_OK) {
			pkg_emit_error("Missing dependency matching '%s'", pkg_dep_get(d, PKG_DEP_ORIGIN));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

static bool
new_pkg_version(struct pkg_jobs *j)
{
	struct pkg *p;
	const char *origin = "ports-mgmt/pkg";

	/* determine local pkgng */
	p = get_local_pkg(j, origin, PKG_LOAD_BASIC);

	if (p == NULL) {
		origin = "ports-mgmt/pkg-devel";
		p = get_local_pkg(j, origin, PKG_LOAD_BASIC);
	}

	/* you are using git version skip */
	if (p == NULL)
		return (false);

	if (get_remote_pkg(j, origin, MATCH_EXACT, true) == EPKG_OK)
		return (true);

	return (false);
}

static int
get_remote_pkg(struct pkg_jobs *j, const char *pattern, match_t m, bool root)
{
	struct pkg *p = NULL;
	struct pkg *p1;
	struct pkgdb_it *it;
	char *origin;
	const char *buf1, *buf2;
	bool force = false;
	int rc = EPKG_FATAL;
	unsigned flags = PKG_LOAD_BASIC|PKG_LOAD_OPTIONS|PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_ANNOTATIONS;

	if (root && (j->flags & PKG_FLAG_FORCE) == PKG_FLAG_FORCE)
		force = true;

	if (((j->flags & PKG_FLAG_FORCE) == PKG_FLAG_FORCE) &&
	    ((j->flags & PKG_FLAG_RECURSIVE) == PKG_FLAG_RECURSIVE))
	    force = true;

	if (j->type == PKG_JOBS_UPGRADE && (j->flags & PKG_FLAG_FORCE) == PKG_FLAG_FORCE)
		force = true;

	if (j->type == PKG_JOBS_FETCH) {
		if ((j->flags & PKG_FLAG_WITH_DEPS) == PKG_FLAG_WITH_DEPS)
			flags |= PKG_LOAD_DEPS;
		if ((j->flags & PKG_FLAG_UPGRADES_FOR_INSTALLED) == PKG_FLAG_UPGRADES_FOR_INSTALLED)
			flags |= PKG_LOAD_DEPS;
	} else {
		flags |= PKG_LOAD_DEPS;
	}

	if ((it = pkgdb_rquery(j->db, pattern, m, j->reponame)) == NULL)
		return (rc);

	while (pkgdb_it_next(it, &p, flags) == EPKG_OK) {
		pkg_get(p, PKG_ORIGIN, &origin);
		HASH_FIND_STR(j->bulk, origin, p1);
		if (p1 == NULL)
			HASH_FIND_STR(j->seen, origin, p1);

		if (p1 != NULL) {
			pkg_get(p1, PKG_VERSION, &buf1);
			pkg_get(p, PKG_VERSION, &buf2);
			p->direct = root;
			if (pkg_version_cmp(buf1, buf2) != 1)
				continue;
			HASH_DEL(j->bulk, p1);
			pkg_free(p1);
		}

		if (j->type != PKG_JOBS_FETCH) {
			if (!newer_than_local_pkg(j, p, force)) {
				if (root)
					pkg_emit_already_installed(p);
				rc = EPKG_OK;
				HASH_ADD_KEYPTR(hh, j->seen, origin, strlen(origin), p);
				continue;
			}
		}

		rc = EPKG_OK;
		p->direct = root;
		HASH_ADD_KEYPTR(hh, j->bulk, origin, strlen(origin), p);
		if (populate_deps(j, p) == EPKG_FATAL) {
			rc = EPKG_FATAL;
			break;
		}

		if (populate_rdeps(j, p) == EPKG_FATAL) {
			rc = EPKG_FATAL;
			break;
		}
		p = NULL;
	}

	pkgdb_it_free(it);

	return (rc);
}

static struct pkg *
get_local_pkg(struct pkg_jobs *j, const char *origin, unsigned flag)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;

	if (flag == 0) {
		flag = PKG_LOAD_BASIC|PKG_LOAD_DEPS|PKG_LOAD_OPTIONS|PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_ANNOTATIONS;
	}

	if ((it = pkgdb_query(j->db, origin, MATCH_EXACT)) == NULL)
		return (NULL);

	if (pkgdb_it_next(it, &pkg, flag) != EPKG_OK)
		pkg = NULL;

	pkgdb_it_free(it);

	return (pkg);
}

static bool
newer_than_local_pkg(struct pkg_jobs *j, struct pkg *rp, bool force)
{
	char *origin, *newversion, *oldversion, *reponame;
	struct pkg_note *an;
	int64_t oldsize;
	struct pkg *lp;
	struct pkg_option *lo = NULL, *ro = NULL;
	struct pkg_dep *ld = NULL, *rd = NULL;
	struct pkg_shlib *ls = NULL, *rs = NULL;
	bool automatic;
	int	ret1, ret2;
	pkg_change_t cmp;

	pkg_get(rp, PKG_ORIGIN, &origin,
	    PKG_REPONAME, &reponame);
	lp = get_local_pkg(j, origin, 0);

	/* obviously yes because local doesn't exists */
	if (lp == NULL) {
		pkg_set(rp, PKG_AUTOMATIC, (int64_t)true);
		return (true);
	}

	if (pkg_is_locked(lp)) {
		pkg_free(lp);
		return (false);
	}

	pkg_get(lp, PKG_AUTOMATIC, &automatic,
	    PKG_VERSION, &oldversion,
	    PKG_FLATSIZE, &oldsize);

	an = pkg_annotation_lookup(lp, "repository");
	if (an != NULL)  {
		if (strcmp(pkg_repo_ident(pkg_repo_find_name(reponame)),
		    pkg_annotation_value(an)) != 0)  {
			pkg_free(lp);
			return (false);
		} else {
			pkg_addannotation(rp, "repository", pkg_annotation_value(an));
		}
	}

	pkg_get(rp, PKG_VERSION, &newversion);
	pkg_set(rp, PKG_OLD_VERSION, oldversion,
	    PKG_OLD_FLATSIZE, oldsize,
	    PKG_AUTOMATIC, (int64_t)automatic);

	if (force) {
		pkg_free(lp);
		return (true);
	}

	/* compare versions */
	cmp = pkg_version_change(rp);

	if (cmp == PKG_UPGRADE) {
		pkg_free(lp);
		return (true);
	}

	if (cmp == PKG_REINSTALL && (j->flags & PKG_FLAG_RECURSIVE) == PKG_FLAG_RECURSIVE) {
		pkg_free(lp);
		return (true);
	}

	if (cmp == PKG_DOWNGRADE) {
		pkg_free(lp);
		return (false);
	}

	/* compare options */
	for (;;) {
		ret1 = pkg_options(rp, &ro);
		ret2 = pkg_options(lp, &lo);
		if (ret1 != ret2) {
			pkg_free(lp);
			pkg_set(rp, PKG_REASON, "options changed");
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if (strcmp(pkg_option_opt(lo), pkg_option_opt(ro)) != 0 ||
				strcmp(pkg_option_value(lo), pkg_option_value(ro)) != 0) {
				pkg_free(lp);
				pkg_set(rp, PKG_REASON, "options changed");
				return (true);
			}
		}
		else
			break;
	}


	/* What about the direct deps */
	for (;;) {
		ret1 = pkg_deps(rp, &rd);
		ret2 = pkg_deps(lp, &ld);
		if (ret1 != ret2) {
			pkg_free(lp);
			pkg_set(rp, PKG_REASON, "direct dependency changed");
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if (strcmp(pkg_dep_get(rd, PKG_DEP_NAME),
			    pkg_dep_get(ld, PKG_DEP_NAME)) != 0) {
				pkg_free(lp);
				pkg_set(rp, PKG_REASON, "direct dependency changed");
				return (true);
			}
		}
		else
			break;
	}

	/* Finish by the shlibs */
	for (;;) {
		ret1 = pkg_shlibs_required(rp, &rs);
		ret2 = pkg_shlibs_required(lp, &ls);
		if (ret1 != ret2) {
			pkg_free(lp);
			pkg_set(rp, PKG_REASON, "needed shared library changed");
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if (strcmp(pkg_shlib_name(rs),
			    pkg_shlib_name(ls)) != 0) {
				pkg_free(lp);
				pkg_set(rp, PKG_REASON, "needed shared library changed");
				return (true);
			}
		}
		else
			break;
	}

	return (false);
}

static int
jobs_solve_install(struct pkg_jobs *j)
{
	struct job_pattern *jp = NULL;
	struct pkg *pkg, *tmp, *p;
	struct pkg_dep *d, *dtmp;
	struct pkgdb_it *it;
	const char *origin;
	int ret;

	if ((j->flags & PKG_FLAG_PKG_VERSION_TEST) != PKG_FLAG_PKG_VERSION_TEST)
		if (new_pkg_version(j)) {
			pkg_emit_newpkgversion();
			goto order;
		}

	LL_FOREACH(j->patterns, jp) {
		if ((j->flags & PKG_FLAG_RECURSIVE) == PKG_FLAG_RECURSIVE) {
			if ((it = pkgdb_query(j->db, jp->pattern, jp->match)) == NULL)
				return (EPKG_FATAL);

			pkg = NULL;
			while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_RDEPS) == EPKG_OK) {
				d = NULL;
				pkg_get(pkg, PKG_ORIGIN, &origin);
				if (get_remote_pkg(j, origin, MATCH_EXACT, true) == EPKG_FATAL)
					pkg_emit_error("No packages matching '%s', has been found in the repositories", pkg_dep_origin(d));

				while (pkg_rdeps(pkg, &d) == EPKG_OK) {
					if (get_remote_pkg(j, pkg_dep_origin(d), MATCH_EXACT, false) == EPKG_FATAL)
						pkg_emit_error("No packages matching '%s', has been found in the repositories", pkg_dep_origin(d));
				}
			}
			pkgdb_it_free(it);
		} else {
			if (get_remote_pkg(j, jp->pattern, jp->match, true) == EPKG_FATAL)
				pkg_emit_error("No packages matching '%s' has been found in the repositories", jp->pattern);
		}
	}

	if (HASH_COUNT(j->bulk) == 0)
		return (EPKG_OK);

	/* remove everything seen from deps */
	HASH_ITER(hh, j->bulk, pkg, tmp) {
		d = NULL;
		HASH_ITER(hh, pkg->deps, d, dtmp) {
			HASH_FIND_STR(j->seen, __DECONST(char *, pkg_dep_get(d, PKG_DEP_ORIGIN)), p);
			if (p != NULL) {
				HASH_DEL(pkg->deps, d);
				pkg_dep_free(d);
			}
		}
		if (pkg->direct) {
			if ((j->flags & PKG_FLAG_AUTOMATIC) == PKG_FLAG_AUTOMATIC)
				pkg_set(pkg, PKG_AUTOMATIC, (int64_t)true);
			else
				pkg_set(pkg, PKG_AUTOMATIC, (int64_t)false);
		}
	}

order:
	HASH_FREE(j->seen, pkg, pkg_free);

	/* now order the pool */
	while (HASH_COUNT(j->bulk) > 0) {
		/*
		 * XXX: create specific flag that allows to install or upgrade
		 * a package even if it misses some dependencies, PKG_FORCE
		 * should not logically apply to this situation, as it is
		 * designed only for reinstalling packages, but not for
		 * installing packages with missing dependencies...
		 */
		ret = order_pool(j, false);
		if (ret == EPKG_FATAL)
			return (EPKG_FATAL);
		else if (ret == EPKG_END)
			break;
	}

	j->solved = true;

	return (EPKG_OK);
}

static int
jobs_solve_fetch(struct pkg_jobs *j)
{
	struct job_pattern *jp = NULL;
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	char *origin;
	unsigned flag = PKG_LOAD_BASIC;

	if ((j->flags & PKG_FLAG_WITH_DEPS) == PKG_FLAG_WITH_DEPS)
		flag |= PKG_LOAD_DEPS;

	if ((j->flags & PKG_FLAG_UPGRADES_FOR_INSTALLED) == PKG_FLAG_UPGRADES_FOR_INSTALLED) {
		if ((it = pkgdb_query(j->db, NULL, MATCH_ALL)) == NULL)
			return (EPKG_FATAL);

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
			pkg_get(pkg, PKG_ORIGIN, &origin);
			/* Do not test we ignore what doesn't exists remotely */
			get_remote_pkg(j, origin, MATCH_EXACT, false);
			pkg = NULL;
		}
		pkgdb_it_free(it);
	} else {
		LL_FOREACH(j->patterns, jp) {
			if (get_remote_pkg(j, jp->pattern, jp->match, true) == EPKG_FATAL)
				pkg_emit_error("No packages matching '%s' has been found in the repositories", jp->pattern);
		}
	}

	HASH_FREE(j->seen, pkg, pkg_free);
	/* No need to order we are just fetching */
	j->jobs = j->bulk;

	j->solved = true;

	return (EPKG_OK);
}

int
pkg_jobs_solve(struct pkg_jobs *j)
{
	bool dry_run = false;

	if ((j->flags & PKG_FLAG_DRY_RUN) == PKG_FLAG_DRY_RUN)
		dry_run = true;

	if (!dry_run && pkgdb_obtain_lock(j->db) != EPKG_OK)
		return (EPKG_FATAL);


	switch (j->type) {
	case PKG_JOBS_AUTOREMOVE:
		return (jobs_solve_autoremove(j));
	case PKG_JOBS_DEINSTALL:
		return (jobs_solve_deinstall(j));
	case PKG_JOBS_UPGRADE:
		return (jobs_solve_upgrade(j));
	case PKG_JOBS_INSTALL:
		return (jobs_solve_install(j));
	case PKG_JOBS_FETCH:
		return (jobs_solve_fetch(j));
	default:
		return (EPKG_FATAL);
	}
}

int
pkg_jobs_find(struct pkg_jobs *j, const char *origin, struct pkg **p)
{
	struct pkg *pkg;

	HASH_FIND_STR(j->jobs, __DECONST(char *, origin), pkg);
	if (pkg == NULL)
		return (EPKG_FATAL);

	if (p != NULL)
		*p = pkg;

	return (EPKG_OK);
}

int
pkg_jobs_count(struct pkg_jobs *j)
{
	assert(j != NULL);

	return (HASH_COUNT(j->jobs));
}

pkg_jobs_t
pkg_jobs_type(struct pkg_jobs *j)
{
	return (j->type);
}

int
pkg_jobs(struct pkg_jobs *j, struct pkg **pkg)
{
	assert(j != NULL);

	HASH_NEXT(j->jobs, (*pkg));
}

static int
pkg_jobs_keep_files_to_del(struct pkg *p1, struct pkg *p2)
{
	struct pkg_file *f = NULL;
	struct pkg_dir *d = NULL;

	while (pkg_files(p1, &f) == EPKG_OK) {
		if (f->keep)
			continue;

		f->keep = pkg_has_file(p2, pkg_file_path(f));
	}

	while (pkg_dirs(p1, &d) == EPKG_OK) {
		if (d->keep)
			continue;

		d->keep = pkg_has_dir(p2, pkg_dir_path(d));
	}

	return (EPKG_OK);
}

static int
pkg_jobs_install(struct pkg_jobs *j)
{
	struct pkg *p = NULL;
	struct pkg *pkg = NULL;
	struct pkg *newpkg = NULL;
	struct pkg *pkg_temp = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *pkg_queue = NULL;
	struct pkg_manifest_key *keys = NULL;
	char path[MAXPATHLEN + 1];
	const char *cachedir = NULL;
	int flags = 0;
	int retcode = EPKG_FATAL;
	int lflags = PKG_LOAD_BASIC | PKG_LOAD_FILES | PKG_LOAD_SCRIPTS |
	    PKG_LOAD_DIRS;
	bool handle_rc = false;

	/* Fetch */
	if (pkg_jobs_fetch(j) != EPKG_OK)
		return (EPKG_FATAL);

	if (j->flags & PKG_FLAG_SKIP_INSTALL)
		return (EPKG_OK);

	if (pkg_config_string(PKG_CONFIG_CACHEDIR, &cachedir) != EPKG_OK)
		return (EPKG_FATAL);
	
	pkg_config_bool(PKG_CONFIG_HANDLE_RC_SCRIPTS, &handle_rc);

	p = NULL;
	pkg_manifest_keys_new(&keys);
	/* Install */
	pkgdb_transaction_begin(j->db->sqlite, "upgrade");

	while (pkg_jobs(j, &p) == EPKG_OK) {
		const char *pkgorigin, *oldversion, *origin;
		struct pkg_note *an;
		bool automatic;
		flags = 0;

		pkg_get(p, PKG_ORIGIN, &pkgorigin,
		    PKG_OLD_VERSION, &oldversion, PKG_AUTOMATIC, &automatic);
		an = pkg_annotation_lookup(p, "repository");

		if (oldversion != NULL) {
			pkg = NULL;
			it = pkgdb_query(j->db, pkgorigin, MATCH_EXACT);
			if (it != NULL) {
				if (pkgdb_it_next(it, &pkg, lflags) == EPKG_OK) {
					if (pkg_is_locked(pkg)) {
						pkg_emit_locked(pkg);
						pkgdb_it_free(it);
						retcode = EPKG_LOCKED;
						pkgdb_transaction_rollback(j->db->sqlite, "upgrade");
						goto cleanup; /* Bail out */
					}

					LL_APPEND(pkg_queue, pkg);
					if ((j->flags & PKG_FLAG_NOSCRIPT) == 0)
						pkg_script_run(pkg,
						    PKG_SCRIPT_PRE_DEINSTALL);
					pkg_get(pkg, PKG_ORIGIN, &origin);
					/*
					 * stop the different related services
					 * if the user wants that and the
					 * service is running
					 */
					if (handle_rc)
						pkg_start_stop_rc_scripts(pkg,
						    PKG_RC_STOP);
					pkgdb_unregister_pkg(j->db, origin);
					pkg = NULL;
				}
				pkgdb_it_free(it);
			}
		}

		it = pkgdb_integrity_conflict_local(j->db, pkgorigin);

		if (it != NULL) {
			pkg = NULL;
			while (pkgdb_it_next(it, &pkg, lflags) == EPKG_OK) {

				if (pkg_is_locked(pkg)) {
					pkg_emit_locked(pkg);
					pkgdb_it_free(it);
					retcode = EPKG_LOCKED;
					pkgdb_transaction_rollback(j->db->sqlite, "upgrade");
					goto cleanup; /* Bail out */
				}

				LL_APPEND(pkg_queue, pkg);
				if ((j->flags & PKG_FLAG_NOSCRIPT) == 0)
					pkg_script_run(pkg,
					    PKG_SCRIPT_PRE_DEINSTALL);
				pkg_get(pkg, PKG_ORIGIN, &origin);
				/*
				 * stop the different related services if the
				 * user wants that and the service is running
				 */
				if (handle_rc)
					pkg_start_stop_rc_scripts(pkg,
					    PKG_RC_STOP);
				pkgdb_unregister_pkg(j->db, origin);
				pkg = NULL;
			}
			pkgdb_it_free(it);
		}
		pkg_snprintf(path, sizeof(path), "%S/%R", cachedir, p);

		pkg_open(&newpkg, path, keys, 0);
		if (oldversion != NULL) {
			pkg_emit_upgrade_begin(p);
		} else {
			pkg_emit_install_begin(newpkg);
		}
		LL_FOREACH(pkg_queue, pkg)
			pkg_jobs_keep_files_to_del(pkg, newpkg);

		LL_FOREACH_SAFE(pkg_queue, pkg, pkg_temp) {
			pkg_get(pkg, PKG_ORIGIN, &origin);
			if (strcmp(pkgorigin, origin) == 0) {
				LL_DELETE(pkg_queue, pkg);
				pkg_delete_files(pkg, 1);
				if ((j->flags & PKG_FLAG_NOSCRIPT) == 0)
					pkg_script_run(pkg,
					    PKG_SCRIPT_POST_DEINSTALL);
				pkg_delete_dirs(j->db, pkg, 0);
				pkg_free(pkg);
				break;
			}
		}

		if ((j->flags & PKG_FLAG_FORCE) == PKG_FLAG_FORCE)
			flags |= PKG_ADD_FORCE | PKG_FLAG_FORCE;
		if ((j->flags & PKG_FLAG_NOSCRIPT) == PKG_FLAG_NOSCRIPT)
			flags |= PKG_ADD_NOSCRIPT;
		flags |= PKG_ADD_UPGRADE;
		if (automatic)
			flags |= PKG_ADD_AUTOMATIC;

		if (pkg_add(j->db, path, flags, keys) != EPKG_OK) {
			pkgdb_transaction_rollback(j->db->sqlite, "upgrade");
			goto cleanup;
		}

		if (an != NULL) {
			pkgdb_add_annotation(j->db, p, "repository", pkg_annotation_value(an));
		}

		if (oldversion != NULL)
			pkg_emit_upgrade_finished(p);
		else
			pkg_emit_install_finished(newpkg);

		if (pkg_queue == NULL) {
			pkgdb_transaction_commit(j->db->sqlite, "upgrade");
			pkgdb_transaction_begin(j->db->sqlite, "upgrade");
		}
	}

	retcode = EPKG_OK;

	cleanup:
	pkgdb_transaction_commit(j->db->sqlite, "upgrade");
	pkg_free(newpkg);
	pkg_manifest_keys_free(keys);

	return (retcode);
}

static int
pkg_jobs_deinstall(struct pkg_jobs *j)
{
	struct pkg *p = NULL;
	int retcode;
	int flags = 0;

	if ((j->flags & PKG_FLAG_DRY_RUN) == PKG_FLAG_DRY_RUN)
		return (EPKG_OK); /* Do nothing */

	if ((j->flags & PKG_FLAG_FORCE) == PKG_FLAG_FORCE)
		flags = PKG_DELETE_FORCE;

	if ((j->flags & PKG_FLAG_NOSCRIPT) == PKG_FLAG_NOSCRIPT)
		flags |= PKG_DELETE_NOSCRIPT;

	while (pkg_jobs(j, &p) == EPKG_OK) {
		retcode = pkg_delete(p, j->db, flags);

		if (retcode != EPKG_OK)
			return (retcode);
	}

	return (EPKG_OK);
}

int
pkg_jobs_apply(struct pkg_jobs *j)
{
	int rc;

	if (!j->solved) {
		pkg_emit_error("The jobs hasn't been solved");
		return (EPKG_FATAL);
	}

	switch (j->type) {
	case PKG_JOBS_INSTALL:
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PRE_INSTALL, j, j->db);
		rc = pkg_jobs_install(j);
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_POST_INSTALL, j, j->db);
		break;
	case PKG_JOBS_DEINSTALL:
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PRE_DEINSTALL, j, j->db);
		rc = pkg_jobs_deinstall(j);
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_POST_DEINSTALL, j, j->db);
		break;
	case PKG_JOBS_FETCH:
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PRE_FETCH, j, j->db);
		rc = pkg_jobs_fetch(j);
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_POST_FETCH, j, j->db);
		break;
	case PKG_JOBS_UPGRADE:
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PRE_UPGRADE, j, j->db);
		rc = pkg_jobs_install(j);
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_POST_UPGRADE, j, j->db);
		break;
	case PKG_JOBS_AUTOREMOVE:
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PRE_AUTOREMOVE, j, j->db);
		rc = pkg_jobs_deinstall(j);
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_POST_AUTOREMOVE, j, j->db);
		break;
	default:
		rc = EPKG_FATAL;
		pkg_emit_error("bad jobs argument");
	}

	return (rc);
}

static int
pkg_jobs_fetch(struct pkg_jobs *j)
{
	struct pkg *p = NULL;
	struct pkg *pkg = NULL;
	struct statfs fs;
	struct stat st;
	char path[MAXPATHLEN + 1];
	int64_t dlsize = 0;
	const char *cachedir = NULL;
	const char *repopath = NULL;
	char cachedpath[MAXPATHLEN];
	int ret = EPKG_OK;
	struct pkg_manifest_key *keys = NULL;
	
	if (pkg_config_string(PKG_CONFIG_CACHEDIR, &cachedir) != EPKG_OK)
		return (EPKG_FATAL);

	/* check for available size to fetch */
	while (pkg_jobs(j, &p) == EPKG_OK) {
		int64_t pkgsize;
		pkg_get(p, PKG_PKGSIZE, &pkgsize, PKG_REPOPATH, &repopath);
		snprintf(cachedpath, MAXPATHLEN, "%s/%s", cachedir, repopath);
		if (stat(cachedpath, &st) == -1)
			dlsize += pkgsize;
		else
			dlsize += pkgsize - st.st_size;
	}

	while (statfs(cachedir, &fs) == -1) {
		if (errno == ENOENT) {
			if (mkdirs(cachedir) != EPKG_OK)
				return (EPKG_FATAL);
		} else {
			pkg_emit_errno("statfs", cachedir);
			return (EPKG_FATAL);
		}
	}

	if (dlsize > ((int64_t)fs.f_bsize * (int64_t)fs.f_bfree)) {
		int64_t fsize = (int64_t)fs.f_bsize * (int64_t)fs.f_bfree;
		char dlsz[7], fsz[7];

		humanize_number(dlsz, sizeof(dlsz), dlsize, "B", HN_AUTOSCALE, 0);
		humanize_number(fsz, sizeof(fsz), fsize, "B", HN_AUTOSCALE, 0);
		pkg_emit_error("Not enough space in %s, needed %s available %s",
		    cachedir, dlsz, fsz);
		return (EPKG_FATAL);
	}

	if ((j->flags & PKG_FLAG_DRY_RUN) == PKG_FLAG_DRY_RUN)
		return (EPKG_OK); /* don't download anything */

	/* Fetch */
	p = NULL;
	while (pkg_jobs(j, &p) == EPKG_OK) {
		if (pkg_repo_fetch(p) != EPKG_OK)
			return (EPKG_FATAL);
	}

	p = NULL;
	/* integrity checking */
	pkg_emit_integritycheck_begin();

	pkg_manifest_keys_new(&keys);
	while (pkg_jobs(j, &p) == EPKG_OK) {
		const char *pkgrepopath;

		pkg_get(p, PKG_REPOPATH, &pkgrepopath);
		snprintf(path, sizeof(path), "%s/%s", cachedir,
		    pkgrepopath);
		if (pkg_open(&pkg, path, keys, 0) != EPKG_OK) {
			return (EPKG_FATAL);
		}

		if (pkgdb_integrity_append(j->db, pkg) != EPKG_OK)
			ret = EPKG_FATAL;
	}
	pkg_manifest_keys_free(keys);

	pkg_free(pkg);

	if (pkgdb_integrity_check(j->db) != EPKG_OK || ret != EPKG_OK)
		return (EPKG_FATAL);

	pkg_emit_integritycheck_finished();

	return (EPKG_OK);
}
