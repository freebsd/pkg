/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2013 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <libutil.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"

static int find_remote_pkg(struct pkg_jobs *j, const char *pattern, match_t m, bool root, int priority);
static struct pkg *get_local_pkg(struct pkg_jobs *j, const char *origin, unsigned flag);
static struct pkg *get_remote_pkg(struct pkg_jobs *j, const char *origin, unsigned flag);
static int pkg_jobs_fetch(struct pkg_jobs *j);
static bool newer_than_local_pkg(struct pkg_jobs *j, struct pkg *rp, bool force);
static bool pkg_need_upgrade(struct pkg *rp, struct pkg *lp, bool recursive);
static bool new_pkg_version(struct pkg_jobs *j);

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
	if ((pkg_repo_find_ident(ident)) == NULL) {
		pkg_emit_error("Unknown repository: %s", ident);
		return (EPKG_FATAL);
	}

	j->reponame = ident;

	return (EPKG_OK);
}

void
pkg_jobs_free(struct pkg_jobs *j)
{
	struct pkg_job_request *req, *tmp;
	struct pkg_job_universe_item *un, *untmp, *cur;

	if (j == NULL)
		return;

	if ((j->flags & PKG_FLAG_DRY_RUN) == 0 &&
		j->type != PKG_JOBS_FETCH)
		pkgdb_release_lock(j->db);

	HASH_ITER(hh, j->request_add, req, tmp) {
		HASH_DEL(j->request_add, req);
		free(req);
	}
	HASH_ITER(hh, j->request_delete, req, tmp) {
		HASH_DEL(j->request_delete, req);
		free(req);
	}
	HASH_ITER(hh, j->universe, un, untmp) {
		HASH_DEL(j->universe, un);
		LL_FOREACH(un, cur) {
			pkg_free(cur->pkg);
		}
		free(un);
	}
	HASH_FREE(j->seen, pkg_job_seen, free);
	LL_FREE(j->patterns, job_pattern, free);
	LL_FREE(j->jobs_add, pkg_solved, free);
	LL_FREE(j->jobs_delete, pkg_solved, free);

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

struct pkg *
pkg_jobs_add_iter(struct pkg_jobs *jobs, void **iter)
{
	struct pkg_solved *s;
	struct pkg *res;
	assert(iter != NULL);

	if (jobs->jobs_add == NULL) {
		return NULL;
	}

	if (*iter == NULL) {
		s = jobs->jobs_add;
	}
	else if (*iter == jobs->jobs_add) {
		return (NULL);
	}
	else {
		s = *iter;
	}

	res = s->pkg;

	*iter = s->next ? s->next : jobs->jobs_add;

	return (res);
}

struct pkg *
pkg_jobs_delete_iter(struct pkg_jobs *jobs, void **iter)
{
	struct pkg_solved *s;
	struct pkg *res;
	assert(iter != NULL);

	if (jobs->jobs_delete == NULL) {
		return NULL;
	}

	if (*iter == NULL) {
		s = jobs->jobs_delete;
	}
	else if (*iter == jobs->jobs_delete) {
		return (NULL);
	}
	else {
		s = *iter;
	}

	res = s->pkg;

	*iter = s->next ? s->next : jobs->jobs_delete;

	return (res);
}

static void
pkg_jobs_add_req(struct pkg_jobs *j, const char *origin, struct pkg *pkg, bool add, int priority)
{
	struct pkg_job_request *req;

	req = calloc(1, sizeof (struct pkg_job_request));
	if (req == NULL) {
		pkg_emit_errno("malloc", "struct pkg_job_request");
		return;
	}
	req->pkg = pkg;
	req->priority = priority;

	if (add)
		HASH_ADD_KEYPTR(hh, j->request_add, origin, strlen(origin), req);
	else
		HASH_ADD_KEYPTR(hh, j->request_delete, origin, strlen(origin), req);
}

/**
 * Check whether a package is in the universe already or add it
 * @return item or NULL
 */
static int
pkg_jobs_handle_pkg_universe(struct pkg_jobs *j, struct pkg *pkg, int priority)
{
	struct pkg_job_universe_item *item, *cur, *tmp = NULL;
	const char *origin, *digest, *digest_cur, *version, *name;
	char *new_digest;
	int rc;
	struct sbuf *sb;
	struct pkg_job_seen *seen;

	pkg_get(pkg, PKG_ORIGIN, &origin, PKG_DIGEST, &digest,
			PKG_VERSION, &version, PKG_NAME, &name);
	if (digest == NULL) {
		/* We need to calculate digest of this package */
		sb = sbuf_new_auto();
		rc = pkg_emit_manifest_sbuf(pkg, sb, PKG_MANIFEST_EMIT_COMPACT, &new_digest);
		if (rc == EPKG_OK) {
			pkg_set(pkg, PKG_DIGEST, new_digest);
			pkg_get(pkg, PKG_DIGEST, &digest);
			free(new_digest);
			sbuf_delete(sb);
		}
		else {
			sbuf_delete(sb);
			return (rc);
		}
	}

	HASH_FIND_STR(j->seen, digest, seen);
	if (seen != NULL)
		return (EPKG_END);

	seen = calloc(1, sizeof(struct pkg_job_seen));
	seen->digest = digest;
	seen->pkg = pkg;
	HASH_ADD_KEYPTR(hh, j->seen, seen->digest, strlen(seen->digest), seen);

	HASH_FIND_STR(j->universe, __DECONST(char *, origin), item);
	if (item == NULL) {
		/* Insert new origin */
		item = calloc(1, sizeof (struct pkg_job_universe_item));

		if (item == NULL) {
			pkg_emit_errno("pkg_jobs_handle_universe", "calloc: struct pkg_job_universe_item");
			return (EPKG_FATAL);
		}
		item->pkg = pkg;
		item->priority = priority;
		HASH_ADD_KEYPTR(hh, j->universe, __DECONST(char *, origin), strlen(origin), item);
	}
	else {
		/* Search for the same package added */
		LL_FOREACH(item, cur) {
			pkg_get(cur->pkg, PKG_DIGEST, &digest_cur);
			if (strcmp (digest, digest_cur) == 0) {
				/* Adjust priority */
				if (priority > cur->priority) {
					pkg_debug(2, "universe: update priority of %s: %d -> %d",
								origin, cur->priority, priority);
					cur->priority = priority;
				}
				pkg_free(pkg);
				return (EPKG_OK);
			}
			tmp = cur;
		}
	}

	item = calloc(1, sizeof (struct pkg_job_universe_item));
	if (item == NULL) {
		pkg_emit_errno("pkg_jobs_pkg_insert_universe", "calloc: struct pkg_job_universe_item");
		return (EPKG_FATAL);
	}

	pkg_debug(2, "universe: add new %s pkg: %s(%d), (%s-%s)",
			(pkg->type == PKG_INSTALLED ? "local" : "remote"), origin,
			priority, name, version);
	item->pkg = pkg;
	item->priority = priority;
	if (tmp != NULL)
		tmp->next = item;

	return (EPKG_OK);
}

static int
pkg_jobs_add_universe(struct pkg_jobs *j, struct pkg *pkg, int priority, bool recursive)
{
	struct pkg_dep *d = NULL;
	struct pkg_conflict *c = NULL;
	struct pkg *npkg, *rpkg;
	int ret;
	struct pkg_job_universe_item *unit;

	/* Add the requested package itself */
	ret = pkg_jobs_handle_pkg_universe(j, pkg, priority);

	if (ret == EPKG_END)
		return (EPKG_OK);
	else if (ret == EPKG_OK && !recursive)
		return (EPKG_OK);
	else if (ret != EPKG_OK)
		return (EPKG_FATAL);

	/* Go through all depends */
	while (pkg_deps(pkg, &d) == EPKG_OK) {
		/* XXX: this assumption can be applied only for the current plain dependencies */
		HASH_FIND_STR(j->universe, __DECONST(char *, pkg_dep_get(d, PKG_DEP_ORIGIN)), unit);
		if (unit != NULL)
			continue;

		rpkg = NULL;
		npkg = get_local_pkg(j, pkg_dep_get(d, PKG_DEP_ORIGIN), 0);
		if (npkg == NULL) {
			/*
			 * We have a package installed, but its dependencies are not,
			 * try to search a remote dependency
			 */
			npkg = get_remote_pkg(j, pkg_dep_get(d, PKG_DEP_ORIGIN), 0);
			if (npkg == NULL) {
				/* Cannot continue */
				pkg_emit_error("Missing dependency matching '%s'", pkg_dep_get(d, PKG_DEP_ORIGIN));
				return (EPKG_FATAL);
			}
		}
		else if (j->type == PKG_JOBS_UPGRADE) {
			/* For upgrade jobs we need to ensure that we do not have a newer version */
			rpkg = get_remote_pkg(j, pkg_dep_get(d, PKG_DEP_ORIGIN), 0);
			if (rpkg != NULL) {
				if (!pkg_need_upgrade(rpkg, npkg, j->flags & PKG_FLAG_RECURSIVE)) {
					pkg_free(rpkg);
					rpkg = NULL;
				}
			}
		}
		if (pkg_jobs_add_universe(j, npkg, priority + 1, recursive) != EPKG_OK)
			return (EPKG_FATAL);
		if (rpkg != NULL && pkg_jobs_add_universe(j, rpkg, priority + 1, recursive) != EPKG_OK)
			return (EPKG_FATAL);
	}

	/* Go through all rdeps */
	d = NULL;
	while (pkg_rdeps(pkg, &d) == EPKG_OK) {
		/* XXX: this assumption can be applied only for the current plain dependencies */
		HASH_FIND_STR(j->universe, __DECONST(char *, pkg_dep_get(d, PKG_DEP_ORIGIN)), unit);
		if (unit != NULL)
			continue;

		npkg = get_local_pkg(j, pkg_dep_get(d, PKG_DEP_ORIGIN), 0);
		if (npkg == NULL) {
			/*
			 * We have a package installed, but its dependencies are not,
			 * try to search a remote dependency
			 */
			npkg = get_remote_pkg(j, pkg_dep_get(d, PKG_DEP_ORIGIN), 0);
			if (npkg == NULL) {
				/* Cannot continue */
				pkg_emit_error("Missing dependency matching '%s'", pkg_dep_get(d, PKG_DEP_ORIGIN));
				return (EPKG_FATAL);
			}
		}
		if (pkg_jobs_add_universe(j, npkg, priority - 1, recursive) != EPKG_OK)
			return (EPKG_FATAL);
	}

	/* Examine conflicts */
	while (pkg_conflicts(pkg, &c) == EPKG_OK) {
		/* XXX: this assumption can be applied only for the current plain dependencies */
		HASH_FIND_STR(j->universe, __DECONST(char *, pkg_conflict_origin(c)), unit);
		if (unit != NULL)
			continue;

		/* Check both local and remote conflicts */
		npkg = get_remote_pkg(j, pkg_conflict_origin(c), 0);
		if (pkg_jobs_add_universe(j, npkg, priority, recursive) != EPKG_OK)
			return (EPKG_FATAL);
		npkg = get_local_pkg(j, pkg_conflict_origin(c), 0);
		if (npkg == NULL) {
			continue;
		}

		if (pkg_jobs_add_universe(j, npkg, priority, recursive) != EPKG_OK)
			return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

struct pkg_conflict_chain {
	struct pkg_job_request *req;
	struct pkg_conflict_chain *next;
};

static int
conflict_chain_cmp_cb(struct pkg_conflict_chain *a, struct pkg_conflict_chain *b)
{
	const char *vera, *verb;

	pkg_get(a->req->pkg, PKG_VERSION, &vera);
	pkg_get(b->req->pkg, PKG_VERSION, &verb);

	/* Inverse sort to get the maximum version as the first element */
	return (pkg_version_cmp(verb, vera));
}

static int
resolve_request_conflicts_chain(struct pkg *req, struct pkg_conflict_chain *chain)
{
	struct pkg_conflict_chain *elt, *selected = NULL;
	const char *name, *origin, *slash_pos;

	pkg_get(req, PKG_NAME, &name);
	/*
	 * First of all prefer pure origins, where the last element of
	 * an origin is pkg name
	 */
	LL_FOREACH(chain, elt) {
		pkg_get(elt->req->pkg, PKG_ORIGIN, &origin);
		slash_pos = strrchr(origin, '/');
		if (slash_pos != NULL) {
			if (strcmp(slash_pos + 1, name) == 0) {
				selected = elt;
				break;
			}
		}
	}

	if (selected == NULL) {
		/* XXX: add manual selection here */
		/* Sort list by version of package */
		LL_SORT(chain, conflict_chain_cmp_cb);
		selected = chain;
	}

	/* Disable conflicts from a request */
	LL_FOREACH(chain, elt) {
		if (elt != selected)
			elt->req->skip = true;
	}

	return (EPKG_OK);
}

static int
resolve_request_conflicts(struct pkg_jobs *j)
{
	struct pkg_job_request *req, *rtmp, *found;
	struct pkg_conflict *c, *ctmp;
	struct pkg_conflict_chain *chain, *elt;

	HASH_ITER(hh, j->request_add, req, rtmp) {
		chain = NULL;
		HASH_ITER(hh, req->pkg->conflicts, c, ctmp) {
			HASH_FIND_STR(j->request_add, pkg_conflict_origin(c), found);
			if (found && !found->skip) {
				elt = calloc(1, sizeof(struct pkg_conflict_chain));
				if (elt == NULL) {
					pkg_emit_errno("resolve_request_conflicts", "calloc: struct pkg_conflict_chain");
					return (EPKG_FATAL);
				}
				elt->req = found;
				LL_PREPEND(chain, elt);
			}
			if (chain != NULL) {
				/* We need to handle conflict chain here */
				if (resolve_request_conflicts_chain (req->pkg, chain) != EPKG_OK) {
					LL_FREE(chain, pkg_conflict_chain, free);
					return (EPKG_FATAL);
				}
				LL_FREE(chain, pkg_conflict_chain, free);
			}
		}
	}

	return (EPKG_OK);
}

static int
jobs_solve_deinstall(struct pkg_jobs *j)
{
	struct job_pattern *jp = NULL;
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	int64_t oldsize;
	char *origin;
	bool recursive = false;

	if ((j->flags & PKG_FLAG_RECURSIVE) == PKG_FLAG_RECURSIVE)
		recursive = true;

	LL_FOREACH(j->patterns, jp) {
		if ((it = pkgdb_query(j->db, jp->pattern, jp->match)) == NULL)
			return (EPKG_FATAL);

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_RDEPS) == EPKG_OK) {
			// Check if the pkg is locked
			if(pkg_is_locked(pkg)) {
				pkg_emit_locked(pkg);
			}
			else {
				pkg_get(pkg, PKG_ORIGIN, &origin, PKG_FLATSIZE, &oldsize);
				pkg_set(pkg, PKG_OLD_FLATSIZE, oldsize, PKG_FLATSIZE, (int64_t)0);
				pkg_jobs_add_req(j, origin, pkg, false, 0);
				/* TODO: use repository priority here */
				pkg_jobs_add_universe(j, pkg, 0, recursive);
			}
			pkg = NULL;
		}
		pkgdb_it_free(it);
	}

	j->solved = true;

	return( EPKG_OK);
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
		// Check if the pkg is locked
		if(pkg_is_locked(pkg)) {
			pkg_emit_locked(pkg);
		}
		else {
			pkg_get(pkg, PKG_ORIGIN, &origin);
			pkg_jobs_add_req(j, origin, pkg, false, 0);
			/* TODO: use repository priority here */
			pkg_jobs_add_universe(j, pkg, 0, false);
		}
		pkg = NULL;
	}
	pkgdb_it_free(it);

	j->solved = true;

	return (EPKG_OK);
}

static int
jobs_solve_upgrade(struct pkg_jobs *j)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	char *origin;

	if ((j->flags & PKG_FLAG_PKG_VERSION_TEST) == PKG_FLAG_PKG_VERSION_TEST)
		if (new_pkg_version(j)) {
			pkg_emit_newpkgversion();
			goto order;
		}

	if ((it = pkgdb_query(j->db, NULL, MATCH_ALL)) == NULL)
		return (EPKG_FATAL);

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		// Check if the pkg is locked
		if(pkg_is_locked(pkg)) {
			pkg_emit_locked(pkg);
		}

		pkg_get(pkg, PKG_ORIGIN, &origin);
		/* Do not test we ignore what doesn't exists remotely */
		find_remote_pkg(j, origin, MATCH_EXACT, false, 0);
		pkg = NULL;
	}
	pkgdb_it_free(it);

order:

	j->solved = true;

	return (EPKG_OK);
}

static bool
new_pkg_version(struct pkg_jobs *j)
{
	struct pkg *p;
	const char *origin = "ports-mgmt/pkg";
	pkg_flags old_flags;
	bool ret = false;

	/* Disable -f for pkg self-check, and restore at end. */
	old_flags = j->flags;
	j->flags &= ~(PKG_FLAG_FORCE|PKG_FLAG_RECURSIVE);

	/* determine local pkgng */
	p = get_local_pkg(j, origin, PKG_LOAD_BASIC);

	if (p == NULL) {
		origin = "ports-mgmt/pkg-devel";
		p = get_local_pkg(j, origin, PKG_LOAD_BASIC);
	}

	/* you are using git version skip */
	if (p == NULL) {
		ret = false;
		goto end;
	}

	/* Use maximum priority for pkg */
	if (find_remote_pkg(j, origin, MATCH_EXACT, false, INT_MAX) == EPKG_OK) {
		ret = true;
		goto end;
	}

end:
	j->flags = old_flags;

	return (ret);
}

static int
find_remote_pkg(struct pkg_jobs *j, const char *pattern, match_t m, bool root, int priority)
{
	struct pkg *p = NULL;
	struct pkg *p1;
	struct pkgdb_it *it;
	struct pkg_job_universe_item *jit;
	char *origin;
	const char *buf1, *buf2;
	bool force = false, seen = false;
	int rc = EPKG_FATAL;
	unsigned flags = PKG_LOAD_BASIC|PKG_LOAD_OPTIONS|
			PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;

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
		seen = false;
		p1 = NULL;
		pkg_get(p, PKG_ORIGIN, &origin);
		HASH_FIND_STR(j->universe, origin, jit);
		if (jit != NULL) {
			p1 = jit->pkg;
			seen = true;
		}

		if (p1 != NULL) {
			pkg_get(p1, PKG_VERSION, &buf1);
			pkg_get(p, PKG_VERSION, &buf2);
			p->direct = root;
			/* We have a more recent package */
			if (pkg_version_cmp(buf1, buf2) >= 0)
				continue;
		}

		if (j->type != PKG_JOBS_FETCH) {
			if (!newer_than_local_pkg(j, p, force)) {
				if (root)
					pkg_emit_already_installed(p);
				rc = EPKG_OK;
				p = NULL;
				continue;
			}
		}

		rc = EPKG_OK;
		p->direct = root;
		/* Add a package to request chain and populate universe */
		pkg_jobs_add_req(j, origin, p, true, priority);
		rc = pkg_jobs_add_universe(j, p, priority, true);

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
		flag = PKG_LOAD_BASIC|PKG_LOAD_DEPS|PKG_LOAD_RDEPS|PKG_LOAD_OPTIONS|
				PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_ANNOTATIONS|
				PKG_LOAD_CONFLICTS;
	}

	if ((it = pkgdb_query(j->db, origin, MATCH_EXACT)) == NULL)
		return (NULL);

	if (pkgdb_it_next(it, &pkg, flag) != EPKG_OK)
		pkg = NULL;

	pkgdb_it_free(it);

	return (pkg);
}

static struct pkg *
get_remote_pkg(struct pkg_jobs *j, const char *origin, unsigned flag)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;

	if (flag == 0) {
		flag = PKG_LOAD_BASIC|PKG_LOAD_DEPS|PKG_LOAD_OPTIONS|
				PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_ANNOTATIONS|
				PKG_LOAD_CONFLICTS;
	}

	if ((it = pkgdb_rquery(j->db, origin, MATCH_EXACT, j->reponame)) == NULL)
		return (NULL);

	if (pkgdb_it_next(it, &pkg, flag) != EPKG_OK)
		pkg = NULL;

	pkgdb_it_free(it);

	return (pkg);
}

static bool
pkg_need_upgrade(struct pkg *rp, struct pkg *lp, bool recursive)
{
	int ret, ret1, ret2;
	const char *lversion, *rversion;
	struct pkg_option *lo = NULL, *ro = NULL;
	struct pkg_dep *ld = NULL, *rd = NULL;
	struct pkg_shlib *ls = NULL, *rs = NULL;
	struct pkg_conflict *lc = NULL, *rc = NULL;
	struct pkg_provide *lpr = NULL, *rpr = NULL;

	/* Do not upgrade locked packages */
	if (pkg_is_locked(lp))
		return (false);

	pkg_get(lp, PKG_VERSION, &lversion);
	pkg_get(rp, PKG_VERSION, &rversion);

	ret = pkg_version_cmp(lversion, rversion);
	if (ret == 0 && recursive)
		return (true);
	else if (ret < 0)
		return (true);
	else
		return (false);


	/* compare options */
	for (;;) {
		ret1 = pkg_options(rp, &ro);
		ret2 = pkg_options(lp, &lo);
		if (ret1 != ret2) {
			pkg_set(rp, PKG_REASON, "options changed");
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if (strcmp(pkg_option_opt(lo), pkg_option_opt(ro)) != 0 ||
					strcmp(pkg_option_value(lo), pkg_option_value(ro)) != 0) {
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
			pkg_set(rp, PKG_REASON, "direct dependency changed");
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if (strcmp(pkg_dep_get(rd, PKG_DEP_NAME),
					pkg_dep_get(ld, PKG_DEP_NAME)) != 0) {
				pkg_set(rp, PKG_REASON, "direct dependency changed");
				return (true);
			}
		}
		else
			break;
	}

	/* Conflicts */
	for (;;) {
		ret1 = pkg_conflicts(rp, &rc);
		ret2 = pkg_conflicts(lp, &lc);
		if (ret1 != ret2) {
			pkg_set(rp, PKG_REASON, "direct conflict changed");
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if (strcmp(pkg_conflict_origin(rc),
					pkg_conflict_origin(lc)) != 0) {
				pkg_set(rp, PKG_REASON, "direct conflict changed");
				return (true);
			}
		}
		else
			break;
	}

	/* Provides */
	for (;;) {
		ret1 = pkg_provides(rp, &rpr);
		ret2 = pkg_provides(lp, &lpr);
		if (ret1 != ret2) {
			pkg_set(rp, PKG_REASON, "provides changed");
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if (strcmp(pkg_provide_name(rpr),
					pkg_provide_name(lpr)) != 0) {
				pkg_set(rp, PKG_REASON, "provides changed");
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
			pkg_set(rp, PKG_REASON, "needed shared library changed");
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if (strcmp(pkg_shlib_name(rs),
					pkg_shlib_name(ls)) != 0) {
				pkg_set(rp, PKG_REASON, "needed shared library changed");
				return (true);
			}
		}
		else
			break;
	}

	return (false);
}

static bool
newer_than_local_pkg(struct pkg_jobs *j, struct pkg *rp, bool force)
{
	char *origin, *newversion, *oldversion, *reponame;
	struct pkg_note *an;
	int64_t oldsize;
	struct pkg *lp;
	bool automatic;
	int ret;

	pkg_get(rp, PKG_ORIGIN, &origin,
	    PKG_REPONAME, &reponame);
	lp = get_local_pkg(j, origin, 0);

	/* obviously yes because local doesn't exists */
	if (lp == NULL) {
		pkg_set(rp, PKG_AUTOMATIC, (int64_t)true);
		return (true);
	}

	pkg_get(lp, PKG_AUTOMATIC, &automatic,
	    PKG_VERSION, &oldversion,
	    PKG_FLATSIZE, &oldsize);

	/* Add repo name to the annotation */
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

	ret = pkg_need_upgrade(rp, lp, j->flags & PKG_FLAG_RECURSIVE);
	pkg_free(lp);

	return (ret);
}

static int
jobs_solve_install(struct pkg_jobs *j)
{
	struct job_pattern *jp = NULL;
	struct pkg *pkg;
	struct pkgdb_it *it;
	const char *origin;

	if ((j->flags & PKG_FLAG_PKG_VERSION_TEST) == PKG_FLAG_PKG_VERSION_TEST)
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
				// Check if the pkg is locked
				if (pkg_is_locked(pkg)) {
					pkg_emit_locked(pkg);
					pkgdb_it_free(it);
					return (EPKG_LOCKED);
				}

				pkg_get(pkg, PKG_ORIGIN, &origin);
				/* TODO: use repository priority here */
				if (find_remote_pkg(j, origin, MATCH_EXACT, true, 0) == EPKG_FATAL)
					pkg_emit_error("No packages matching '%s', has been found in the repositories", origin);
			}
			pkgdb_it_free(it);
		} else {
			// Check if the pkg is locked before trying to install it
			if ((it = pkgdb_query(j->db, jp->pattern, jp->match)) == NULL)
				return (EPKG_FATAL);
			pkg = NULL;
			if (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
				if (pkg_is_locked(pkg)) {
					pkg_emit_locked(pkg);
					pkgdb_it_free(it);
					return (EPKG_LOCKED);
				}
			}
			pkgdb_it_free(it);
			/* TODO: use repository priority here */
			if (find_remote_pkg(j, jp->pattern, jp->match, true, 0) == EPKG_FATAL)
				pkg_emit_error("No packages matching '%s' has been found in the repositories", jp->pattern);
		}
	}

	if (resolve_request_conflicts(j) != EPKG_OK) {
		pkg_emit_error("Cannot resolve conflicts in a request");
		return (EPKG_FATAL);
	}

order:

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
			// Check if the pkg is locked
			if(pkg_is_locked(pkg)) {
				pkg_emit_locked(pkg);
				pkgdb_it_free(it);
				return(EPKG_LOCKED);
			}

			pkg_get(pkg, PKG_ORIGIN, &origin);
			/* Do not test we ignore what doesn't exists remotely */
			find_remote_pkg(j, origin, MATCH_EXACT, false, 0);
			pkg = NULL;
		}
		pkgdb_it_free(it);
	} else {
		LL_FOREACH(j->patterns, jp) {
			/* TODO: use repository priority here */
			if (find_remote_pkg(j, jp->pattern, jp->match, true, 0) == EPKG_FATAL)
				pkg_emit_error("No packages matching '%s' has been found in the repositories", jp->pattern);
		}
	}

	j->solved = true;

	return (EPKG_OK);
}

static int
jobs_sort_priority_dec(struct pkg_solved *r1, struct pkg_solved *r2)
{
	return (r1->priority - r2->priority);
}

static int
jobs_sort_priority_inc(struct pkg_solved *r1, struct pkg_solved *r2)
{
	return (r2->priority - r1->priority);
}

int
pkg_jobs_solve(struct pkg_jobs *j)
{
	bool dry_run = false;
	int ret, pstatus;
	struct pkg_solve_problem *problem;
	const char *solver;
	FILE *spipe[2];
	pid_t pchild;

	if ((j->flags & PKG_FLAG_DRY_RUN) == PKG_FLAG_DRY_RUN)
		dry_run = true;

	if (!dry_run && pkgdb_obtain_lock(j->db) != EPKG_OK)
		return (EPKG_FATAL);


	switch (j->type) {
	case PKG_JOBS_AUTOREMOVE:
		ret =jobs_solve_autoremove(j);
		break;
	case PKG_JOBS_DEINSTALL:
		ret = jobs_solve_deinstall(j);
		break;
	case PKG_JOBS_UPGRADE:
		ret = jobs_solve_upgrade(j);
		break;
	case PKG_JOBS_INSTALL:
		ret = jobs_solve_install(j);
		break;
	case PKG_JOBS_FETCH:
		ret = jobs_solve_fetch(j);
		break;
	default:
		return (EPKG_FATAL);
	}

	if (ret == EPKG_OK) {
		if (pkg_config_string(PKG_CONFIG_CUDF_SOLVER, &solver) == EPKG_OK
				&& solver != NULL) {
			pchild = process_spawn_pipe(spipe, solver);
			if (pchild == -1)
				return (EPKG_FATAL);

			ret = pkg_jobs_cudf_emit_file(j, j->type, spipe[1]);
			fclose(spipe[1]);

			if (ret == EPKG_OK)
				ret = pkg_jobs_cudf_parse_output(j, spipe[0]);

			fclose(spipe[0]);
			waitpid(pchild, &pstatus, WNOHANG);
		}
		else {
			problem = pkg_solve_jobs_to_sat(j);
			if (problem != NULL) {
				if (pkg_config_string(PKG_CONFIG_SAT_SOLVER, &solver) == EPKG_OK
						&& solver != NULL) {
					pchild = process_spawn_pipe(spipe, solver);
					if (pchild == -1)
						return (EPKG_FATAL);

					ret = pkg_solve_dimacs_export(problem, spipe[1]);
					fclose(spipe[1]);

					if (ret == EPKG_OK) {
						ret = pkg_solve_parse_sat_output(spipe[0], problem, j);
					}

					fclose(spipe[0]);
					waitpid(pchild, &pstatus, WNOHANG);
				}
				else {
					if (!pkg_solve_sat_problem(problem)) {
						pkg_emit_error("cannot solve job using SAT solver");
						ret = EPKG_FATAL;
						j->solved = false;
					}
					else {
						ret = pkg_solve_sat_to_jobs(problem, j);
					}
				}
			}
			else {
				pkg_emit_error("cannot convert job to SAT problem");
				ret = EPKG_FATAL;
				j->solved = false;
			}
		}
	}

	/* Resort priorities */
	if (j->solved) {
		DL_SORT(j->jobs_add, jobs_sort_priority_inc);
		DL_SORT(j->jobs_delete, jobs_sort_priority_dec);
	}

	return (ret);
}

int
pkg_jobs_count(struct pkg_jobs *j)
{
	assert(j != NULL);

	return (j->count);
}

pkg_jobs_t
pkg_jobs_type(struct pkg_jobs *j)
{
	assert(j != NULL);

	return (j->type);
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
	struct pkg_solved *ps;
	struct pkg *pkg = NULL;
	struct pkg *newpkg = NULL;
	struct pkg *pkg_temp = NULL;
	struct pkgdb_it *it = NULL;
	struct pkg *pkg_queue = NULL;
	struct pkg_manifest_key *keys = NULL;
	char path[MAXPATHLEN];
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

	/* Delete conflicts initially */
	DL_FOREACH(j->jobs_delete, ps) {
		p = ps->pkg;
		retcode = pkg_delete(p, j->db, flags);

		if (retcode != EPKG_OK)
			return (retcode);
	}
	DL_FOREACH(j->jobs_add, ps) {
		p = ps->pkg;
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
			flags |= PKG_ADD_FORCE;
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
			pkg_emit_upgrade_finished(newpkg);
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
	struct pkg_solved *ps;
	const char *name;
	int retcode;
	int flags = 0;

	if ((j->flags & PKG_FLAG_DRY_RUN) == PKG_FLAG_DRY_RUN)
		return (EPKG_OK); /* Do nothing */

	if ((j->flags & PKG_FLAG_FORCE) == PKG_FLAG_FORCE)
		flags = PKG_DELETE_FORCE;

	if ((j->flags & PKG_FLAG_NOSCRIPT) == PKG_FLAG_NOSCRIPT)
		flags |= PKG_DELETE_NOSCRIPT;

	DL_FOREACH(j->jobs_delete, ps) {
		p = ps->pkg;
		pkg_get(p, PKG_NAME, &name);
		if ((strcmp(name, "pkg") == 0 ||
			 strcmp(name, "pkg-devel") == 0) && flags != PKG_DELETE_FORCE) {
			pkg_emit_error("Cannot delete pkg itself without force flag");
			continue;
		}
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
		rc = pkg_jobs_deinstall(j);
		if (rc == EPKG_OK)
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
	struct pkg_solved *ps;
	struct pkg *pkg = NULL;
	struct statfs fs;
	struct stat st;
	char path[MAXPATHLEN];
	int64_t dlsize = 0;
	const char *cachedir = NULL;
	const char *repopath = NULL;
	char cachedpath[MAXPATHLEN];
	int ret = EPKG_OK;
	struct pkg_manifest_key *keys = NULL;
	
	if (pkg_config_string(PKG_CONFIG_CACHEDIR, &cachedir) != EPKG_OK)
		return (EPKG_FATAL);

	/* check for available size to fetch */
	DL_FOREACH(j->jobs_add, ps) {
		p = ps->pkg;
		int64_t pkgsize;
		pkg_get(p, PKG_PKGSIZE, &pkgsize, PKG_REPOPATH, &repopath);
		snprintf(cachedpath, sizeof(cachedpath), "%s/%s", cachedir, repopath);
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
	DL_FOREACH(j->jobs_add, ps) {
		p = ps->pkg;
		if (pkg_repo_fetch(p) != EPKG_OK)
			return (EPKG_FATAL);
	}

	/* integrity checking */
	pkg_emit_integritycheck_begin();

	pkg_manifest_keys_new(&keys);
	DL_FOREACH(j->jobs_add, ps) {
		p = ps->pkg;
		const char *pkgrepopath;

		pkg_get(p, PKG_REPOPATH, &pkgrepopath);
		snprintf(path, sizeof(path), "%s/%s", cachedir,
		    pkgrepopath);
		if (pkg_open(&pkg, path, keys, 0) != EPKG_OK)
			return (EPKG_FATAL);

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
