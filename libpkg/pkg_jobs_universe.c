/* Copyright (c) 2014, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <sys/param.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/pkg_jobs.h"
#include "tllist.h"

#define IS_DELETE(j) ((j)->type == PKG_JOBS_DEINSTALL || (j)->type == PKG_JOBS_AUTOREMOVE)

typedef tll(struct pkg *) pkg_chain_t;

struct pkg *
pkg_jobs_universe_get_local(struct pkg_jobs_universe *universe,
	const char *uid, unsigned flag)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	struct pkg_job_universe_item *unit, *cur, *found;

	if (flag == 0) {
		if (!IS_DELETE(universe->j))
			flag = PKG_LOAD_BASIC|PKG_LOAD_DEPS|PKG_LOAD_RDEPS|PKG_LOAD_OPTIONS|
				PKG_LOAD_REQUIRES|PKG_LOAD_PROVIDES|
				PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|PKG_LOAD_ANNOTATIONS|
				PKG_LOAD_CONFLICTS;
		else
			flag = PKG_LOAD_BASIC|PKG_LOAD_RDEPS|PKG_LOAD_DEPS|PKG_LOAD_ANNOTATIONS;
	}

	unit = pkghash_get_value(universe->items, uid);
	if (unit != NULL) {
		/* Search local in a universe chain */
		cur = unit;
		found = NULL;
		do {
			if (cur->pkg->type == PKG_INSTALLED) {
				found = cur;
				break;
			}
			cur = cur->prev;
		} while (cur != unit);

		if (found) {
			pkgdb_ensure_loaded(universe->j->db, unit->pkg, flag);
			return (unit->pkg);
		}
	}

	if ((it = pkgdb_query(universe->j->db, uid, MATCH_INTERNAL)) == NULL)
		return (NULL);

	if (pkgdb_it_next(it, &pkg, flag) != EPKG_OK)
		pkg = NULL;

	pkgdb_it_free(it);

	return (pkg);
}

static pkg_chain_t *
pkg_jobs_universe_get_remote(struct pkg_jobs_universe *universe,
	const char *uid, unsigned flag)
{
	struct pkg *pkg = NULL;
	pkg_chain_t *result = NULL;
	struct pkgdb_it *it;
	struct pkg_job_universe_item *unit, *cur, *found;

	if (flag == 0) {
		flag = PKG_LOAD_BASIC|PKG_LOAD_DEPS|PKG_LOAD_OPTIONS|
			PKG_LOAD_PROVIDES|PKG_LOAD_REQUIRES|
				PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|
				PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;
	}

	unit = pkghash_get_value(universe->items, uid);
	if (unit != NULL && unit->pkg->type != PKG_INSTALLED) {
		/* Search local in a universe chain */
		cur = unit;
		found = NULL;
		do {
			if (cur->pkg->type != PKG_INSTALLED) {
				found = cur;
				break;
			}
			cur = cur->prev;
		} while (cur != unit);

		if (found) {
			/* Assume processed */
			return (NULL);
		}
	}

	if ((it = pkgdb_repo_query(universe->j->db, uid, MATCH_INTERNAL,
		universe->j->reponame)) == NULL)
		return (NULL);

	while (pkgdb_it_next(it, &pkg, flag) == EPKG_OK) {
		if (result == NULL)
			result = xcalloc(1, sizeof(pkg_chain_t));
		tll_push_front(*result, pkg);
		pkg = NULL;
	}

	pkgdb_it_free(it);

	return (result);
}

/**
 * Check whether a package is in the universe already or add it
 * @return item or NULL
 */
int
pkg_jobs_universe_add_pkg(struct pkg_jobs_universe *universe, struct pkg *pkg,
		bool force __unused, struct pkg_job_universe_item **found)
{
	struct pkg_job_universe_item *item, *seen, *tmp = NULL;

	pkg_validate(pkg, universe->j->db);

	if (pkg->digest == NULL) {
		pkg_debug(3, "no digest found for package %s (%s-%s)",
		    pkg->uid, pkg->name, pkg->version);
		if (pkg_checksum_calculate(pkg, universe->j->db, false, true, false) != EPKG_OK) {
			*found = NULL;
			return (EPKG_FATAL);
		}
	}

	seen = pkghash_get_value(universe->seen, pkg->digest);
	if (seen) {
		bool same_package = false;

		DL_FOREACH(seen, tmp) {
			if (tmp->pkg == pkg || (tmp->pkg->type == pkg->type &&
			    strcmp(tmp->pkg->digest, pkg->digest) == 0)) {
				if (tmp->pkg->reponame != NULL) {
					if (strcmp(tmp->pkg->reponame, pkg->reponame) == 0) {
						same_package = true;
						break;
					}
				} else {
					same_package = true;
					break;
				}
			}
		}

		if (same_package) {
			if (found != NULL) {
				*found = seen;
			}

			return (EPKG_END);
		}
	}

	if (pkg_is_locked(pkg)) {
		return (EPKG_LOCKED);
	}

	pkg_debug(2, "universe: add new %s pkg: %s, (%s-%s:%s)",
	    (pkg->type == PKG_INSTALLED ? "local" : "remote"), pkg->uid,
	    pkg->name, pkg->version, pkg->digest);

	item = xcalloc(1, sizeof (struct pkg_job_universe_item));
	item->pkg = pkg;

	tmp = pkghash_get_value(universe->items, pkg->uid);
	if (tmp == NULL) {
		pkghash_safe_add(universe->items, pkg->uid, item, NULL);
		item->inhash = true;
	}

	DL_APPEND(tmp, item);

	if (seen == NULL)
		pkghash_safe_add(universe->seen, item->pkg->digest, item, NULL);

	universe->nitems++;

	if (found != NULL)
		*found = item;

	return (EPKG_OK);
}

#define DEPS_FLAG_REVERSE 0x1 << 1
#define DEPS_FLAG_MIRROR 0x1 << 2
#define DEPS_FLAG_FORCE_LOCAL 0x1 << 3
#define DEPS_FLAG_FORCE_MISSING 0x1 << 4
#define DEPS_FLAG_FORCE_UPGRADE 0x1 << 5

static int
pkg_jobs_universe_process_deps(struct pkg_jobs_universe *universe,
	struct pkg *pkg, unsigned flags)
{
	struct pkg_dep *d = NULL;
	int (*deps_func)(const struct pkg *pkg, struct pkg_dep **d);
	int rc;
	struct pkg_job_universe_item *unit;
	struct pkg *npkg, *rpkg, *lpkg;
	pkg_chain_t *rpkgs = NULL;
	bool found = false;

	rpkg = NULL;

	if (flags & DEPS_FLAG_REVERSE) {
		pkg_debug(4, "Processing rdeps for %s (%s)", pkg->uid, pkg->type == PKG_INSTALLED ? "installed" : "remote");
		if (pkg->type != PKG_INSTALLED) {
			lpkg = pkg_jobs_universe_get_local(universe, pkg->uid, 0);
			if (lpkg != NULL && lpkg != pkg)
				return (pkg_jobs_universe_process_deps(universe, lpkg, flags));
		}
		deps_func = pkg_rdeps;
	}
	else {
		pkg_debug(4, "Processing deps for %s", pkg->uid);
		deps_func = pkg_deps;
	}

	while (deps_func(pkg, &d) == EPKG_OK) {
		pkg_debug(4, "Processing *deps for %s: %s", pkg->uid, d->uid);
		if (pkghash_get(universe->items, d->uid) != NULL)
			continue;

		rpkgs = NULL;
		npkg = NULL;
		if (!(flags & DEPS_FLAG_MIRROR)) {
			npkg = pkg_jobs_universe_get_local(universe, d->uid, 0);
		}

		if (!(flags & DEPS_FLAG_FORCE_LOCAL)) {

			/* Check for remote dependencies */
			rpkgs = pkg_jobs_universe_get_remote(universe, d->uid, 0);
		}

		if (npkg == NULL && rpkgs == NULL) {
			pkg_emit_error("%s has a missing dependency: %s",
				pkg->name, d->name);

			if (flags & DEPS_FLAG_FORCE_MISSING) {
				continue;
			}

			return (EPKG_FATAL);
		}

		if (npkg != NULL) {
			if (pkg_jobs_universe_process_item(universe, npkg, &unit) != EPKG_OK) {
				continue;
			}
		}

		if (rpkgs == NULL)
			continue;
		/*
		 * When processing deps, we should first try to select a dependency
		 * from the same repo.
		 * Otherwise, we would have ping-pong of dependencies instead of
		 * the situation when this behaviour is handled by
		 * CONSERVATIVE_UPGRADES.
		 *
		 * Important notes here:
		 * 1. We are looking for packages that are dependencies of a package
		 * `pkg`
		 * 2. Now if `pkg` belongs to repo `r` and `rpkg` belongs to repo
		 * `r` then we just select it.
		 * 3. If `rpkg` is not found in `r` we just scan all packages
		 */

		/*
		 * XXX: this is the proper place to expand flexible dependencies
		 */

		found = false;
		/* Iteration one */
		tll_foreach(*rpkgs, rit) {
			rpkg = rit->item;

			if (pkg->reponame && rpkg->reponame &&
					strcmp (pkg->reponame, rpkg->reponame) == 0) {
				found = true;
				break;
			}
		}

		/* Fallback if a dependency is not found in the same repo */
		if (!found) {
			tll_foreach(*rpkgs, rit) {
				rpkg = rit->item;

				if (npkg != NULL) {
					/* Set reason for upgrades */
					if (!pkg_jobs_need_upgrade(rpkg, npkg))
						continue;
					/* Save automatic flag */
					rpkg->automatic = npkg->automatic;
				}

				rc = pkg_jobs_universe_process_item(universe, rpkg, NULL);

				/* Special case if we cannot find any package */
				if (npkg == NULL && rc != EPKG_OK) {
					tll_free(*rpkgs);
					free(rpkgs);
					return (rc);
				}
			}
		}
		else {
			assert (rpkg != NULL);

			if (npkg != NULL) {
				/* Set reason for upgrades */
				if (!pkg_jobs_need_upgrade(rpkg, npkg))
					continue;
				/* Save automatic flag */
				rpkg->automatic = npkg->automatic;
			}

			rc = pkg_jobs_universe_process_item(universe, rpkg, NULL);
			if (npkg == NULL && rc != EPKG_OK) {
				tll_free(*rpkgs);
				free(rpkgs);
				return (rc);
			}
		}

		tll_free(*rpkgs);
		free(rpkgs);
	}

	return (EPKG_OK);
}

static int
pkg_jobs_universe_handle_provide(struct pkg_jobs_universe *universe,
    struct pkgdb_it *it, const char *name, bool is_shlib, struct pkg *parent __unused)
{
	struct pkg_job_universe_item *unit;
	struct pkg_job_provide *pr, *prhead;
	struct pkg *npkg, *rpkg;
	int rc;
	unsigned flags = PKG_LOAD_BASIC|PKG_LOAD_OPTIONS|PKG_LOAD_DEPS|
				PKG_LOAD_REQUIRES|PKG_LOAD_PROVIDES|
				PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|
				PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;

	rpkg = NULL;

	prhead = pkghash_get_value(universe->provides, name);
	while (pkgdb_it_next(it, &rpkg, flags) == EPKG_OK) {
		/* Check for local packages */
		if ((unit = pkghash_get_value(universe->items, rpkg->uid)) != NULL) {
			/* Remote provide is newer, so we can add it */
			if (pkg_jobs_universe_process_item(universe, rpkg,
			    &unit) != EPKG_OK) {
				continue;
			}

			rpkg = NULL;
		}
		else {
			/* Maybe local package has just been not added */
			npkg = pkg_jobs_universe_get_local(universe, rpkg->uid, 0);
			if (npkg != NULL) {
				if (pkg_jobs_universe_process_item(universe, npkg,
						&unit) != EPKG_OK) {
					return (EPKG_FATAL);
				}
				if (pkg_jobs_universe_process_item(universe, rpkg,
						&unit) != EPKG_OK) {
					continue;
				}
				if (unit != NULL)
					rpkg = NULL;
			}
		}

		/* Skip seen packages */
		if (unit == NULL) {
			if (rpkg->digest == NULL) {
				pkg_debug(3, "no digest found for package %s", rpkg->uid);
				if (pkg_checksum_calculate(rpkg,
				    universe->j->db, false, true, false) != EPKG_OK) {
					return (EPKG_FATAL);
				}
			}
			rc = pkg_jobs_universe_process_item(universe, rpkg,
					&unit);

			if (rc != EPKG_OK) {
				return (rc);
			}

			/* Reset package to avoid freeing */
			rpkg = NULL;
		}

		pr = xcalloc (1, sizeof (*pr));
		pr->un = unit;
		pr->provide = name;
		pr->is_shlib = is_shlib;

		if (prhead == NULL) {
			DL_APPEND(prhead, pr);
			pkghash_safe_add(universe->provides, pr->provide,
			    prhead, NULL);
			pkg_debug (4, "universe: add new provide %s-%s(%s) for require %s",
					pr->un->pkg->name, pr->un->pkg->version,
					pr->un->pkg->type == PKG_INSTALLED ? "l" : "r",
					pr->provide);
		} else {
			DL_APPEND(prhead, pr);
			pkg_debug (4, "universe: append provide %s-%s(%s) for require %s",
					pr->un->pkg->name, pr->un->pkg->version,
					pr->un->pkg->type == PKG_INSTALLED ? "l" : "r",
					pr->provide);
		}
	}

	return (EPKG_OK);
}

static int
pkg_jobs_universe_process_shlibs(struct pkg_jobs_universe *universe,
	struct pkg *pkg)
{
	struct pkgdb_it *it;
	int rc;

	tll_foreach(pkg->shlibs_required, s) {
		if (pkghash_get(universe->provides, s->item) != NULL)
			continue;

		/* Check for local provides */
		it = pkgdb_query_shlib_provide(universe->j->db, s->item);
		if (it != NULL) {
			rc = pkg_jobs_universe_handle_provide(universe, it,
			    s->item, true, pkg);
			pkgdb_it_free(it);

			if (rc != EPKG_OK) {
				pkg_debug(1, "cannot find local packages that provide library %s "
						"required for %s",
						s->item, pkg->name);
			}
		}
		/* Not found, search in the repos */
		it = pkgdb_repo_shlib_provide(universe->j->db,
			s->item, universe->j->reponame);

		if (it != NULL) {
			rc = pkg_jobs_universe_handle_provide(universe, it, s->item, true, pkg);
			pkgdb_it_free(it);

			if (rc != EPKG_OK) {
				pkg_debug(1, "cannot find remote packages that provide library %s "
						"required for %s",
				    s->item, pkg->name);
			}
		}
	}

	return (EPKG_OK);
}

static int
pkg_jobs_universe_process_provides_requires(struct pkg_jobs_universe *universe,
	struct pkg *pkg)
{
	struct pkgdb_it *it;
	int rc;

	tll_foreach(pkg->requires, r) {
		if (pkghash_get(universe->provides, r->item) != NULL)
			continue;

		/* Check for local provides */
		it = pkgdb_query_provide(universe->j->db, r->item);
		if (it != NULL) {
			rc = pkg_jobs_universe_handle_provide(universe, it, r->item, false, pkg);
			pkgdb_it_free(it);

			if (rc != EPKG_OK) {
				pkg_debug(1, "cannot find local packages that provide %s "
						"required for %s",
						r->item, pkg->name);
			}
		}

		/* Not found, search in the repos */
		it = pkgdb_repo_provide(universe->j->db,
			r->item, universe->j->reponame);

		if (it != NULL) {
			rc = pkg_jobs_universe_handle_provide(universe, it, r->item, false, pkg);
			pkgdb_it_free(it);

			if (rc != EPKG_OK) {
				pkg_debug(1, "cannot find remote packages that provide %s "
						"required for %s",
				    r->item, pkg->name);
				return (rc);
			}
		}
	}

	return (EPKG_OK);
}

int
pkg_jobs_universe_process_item(struct pkg_jobs_universe *universe, struct pkg *pkg,
		struct pkg_job_universe_item **result)
{
	unsigned flags = 0, job_flags;
	int rc = EPKG_OK;
	pkg_jobs_t type = universe->j->type;
	struct pkg_job_universe_item *found;

	pkg_debug(4, "Processing item %s\n", pkg->uid);

	job_flags = universe->j->flags;

	/*
	 * Add pkg itself. If package is already seen then we check the `processed`
	 * flag that means that we have already tried to check our universe
	 */
	rc = pkg_jobs_universe_add_pkg(universe, pkg, false, &found);
	if (rc == EPKG_CONFLICT)
		return (rc);

	if (result)
		*result = found;

	if (rc == EPKG_END) {
		if (found->processed)
			return (EPKG_OK);
	}
	else if (rc != EPKG_OK) {
		return (rc);
	}

	found->processed = true;

	/* Convert jobs flags to dependency logical flags */
	if (job_flags & PKG_FLAG_FORCE_MISSING)
		flags |= DEPS_FLAG_FORCE_MISSING;

	switch(type) {
	case PKG_JOBS_FETCH:
		if (job_flags & PKG_FLAG_RECURSIVE) {
			flags |= DEPS_FLAG_MIRROR;
			/* For fetch jobs we worry about depends only */
			rc = pkg_jobs_universe_process_deps(universe, pkg, flags);
		}
		break;
	case PKG_JOBS_INSTALL:
	case PKG_JOBS_UPGRADE:
		/* Handle depends */
		rc = pkg_jobs_universe_process_deps(universe, pkg, flags);
		if (rc != EPKG_OK)
			return (rc);
		/* Handle reverse depends */
		rc = pkg_jobs_universe_process_deps(universe, pkg,
			flags|DEPS_FLAG_REVERSE);
		if (rc != EPKG_OK)
				return (rc);
		/* Provides/requires */
		rc = pkg_jobs_universe_process_shlibs(universe, pkg);
		if (rc != EPKG_OK)
			return (rc);
		rc = pkg_jobs_universe_process_provides_requires(universe, pkg);
		if (rc != EPKG_OK)
			return (rc);
		break;
	case PKG_JOBS_AUTOREMOVE:
		/* XXX */
		break;
	case PKG_JOBS_DEINSTALL:
		/* For delete jobs we worry only about local reverse deps */
		flags |= DEPS_FLAG_REVERSE|DEPS_FLAG_FORCE_LOCAL;
		if (job_flags & PKG_FLAG_RECURSIVE)
			rc = pkg_jobs_universe_process_deps(universe, pkg, flags);
		break;
	}

	return (rc);
}

int
pkg_jobs_universe_process(struct pkg_jobs_universe *universe,
	struct pkg *pkg)
{
	return (pkg_jobs_universe_process_item(universe, pkg, NULL));
}

#define RECURSION_LIMIT 1024

static void
pkg_jobs_update_universe_item_priority(struct pkg_jobs_universe *universe,
		struct pkg_job_universe_item *item, int priority,
		enum pkg_priority_update_type type)
{
	struct pkg_dep *d = NULL;
	struct pkg_conflict *c = NULL;
	struct pkg_job_universe_item *found, *cur, *it;
	const char *is_local;
	int maxpri;

	int (*deps_func)(const struct pkg *pkg, struct pkg_dep **d);
	int (*rdeps_func)(const struct pkg *pkg, struct pkg_dep **d);

	if (priority > RECURSION_LIMIT) {
		pkg_debug(1, "recursion limit has been reached, something is bad"
					" with dependencies/conflicts graph");
		return;
	}
	else if (priority + 10 > RECURSION_LIMIT) {
		pkg_debug(2, "approaching recursion limit at %d, while processing of"
		    " package %s", priority, item->pkg->uid);
	}

	LL_FOREACH(item, it) {
		if ((item->next != NULL || item->prev != NULL) &&
		    it->pkg->type != PKG_INSTALLED &&
		    (type == PKG_PRIORITY_UPDATE_CONFLICT ||
		     type == PKG_PRIORITY_UPDATE_DELETE)) {
			/*
			 * We do not update priority of a remote part of conflict, as we know
			 * that remote packages should not contain conflicts (they should be
			 * resolved in request prior to calling of this function)
			 */
			pkg_debug(4, "skip update priority for %s-%s",
			    it->pkg->uid, it->pkg->digest);
			continue;
		}
		if (it->priority > priority)
			continue;

		is_local = it->pkg->type == PKG_INSTALLED ? "local" : "remote";
		pkg_debug(2, "universe: update %s priority of %s(%s): %d -> %d, reason: %d",
		    is_local, it->pkg->uid, it->pkg->version, it->priority, priority, type);
		it->priority = priority;

		if (type == PKG_PRIORITY_UPDATE_DELETE) {
			/*
			 * For delete requests we inverse deps and rdeps logic
			 */
			deps_func = pkg_rdeps;
			rdeps_func = pkg_deps;
		}
		else {
			deps_func = pkg_deps;
			rdeps_func = pkg_rdeps;
		}

		while (deps_func(it->pkg, &d) == EPKG_OK) {
			found = pkghash_get_value(universe->items, d->uid);
			if (found == NULL)
				continue;
			LL_FOREACH(found, cur) {
				if (cur->priority < priority + 1)
					pkg_jobs_update_universe_item_priority(universe, cur,
					    priority + 1, type);
			}
		}

		d = NULL;
		maxpri = priority;
		while (rdeps_func(it->pkg, &d) == EPKG_OK) {
			found = pkghash_get_value(universe->items, d->uid);
			if (found == NULL)
				continue;
			LL_FOREACH(found, cur) {
				if (cur->priority >= maxpri) {
					maxpri = cur->priority + 1;
				}
			}
		}
		if (maxpri != priority) {
			pkg_jobs_update_universe_item_priority(universe, it,
					maxpri, type);
			return;
		}
		if (it->pkg->type == PKG_INSTALLED)
			continue;

		while (pkg_conflicts(it->pkg, &c) == EPKG_OK) {
			found = pkghash_get_value(universe->items, c->uid);
			LL_FOREACH(found, cur) {
				if (cur->pkg->type != PKG_INSTALLED)
					continue;
				/*
				 * Move delete requests to be done before installing
				 */
				if (cur->priority <= it->priority)
					pkg_jobs_update_universe_item_priority(universe, cur,
					    it->priority + 1, PKG_PRIORITY_UPDATE_CONFLICT);
			}
		}
	}
}

void
pkg_jobs_update_conflict_priority(struct pkg_jobs_universe *universe,
	struct pkg_solved *req)
{
	struct pkg_conflict *c = NULL;
	struct pkg *lp = req->items[1]->pkg;
	struct pkg_job_universe_item *found, *cur, *rit = NULL;

	while (pkg_conflicts(lp, &c) == EPKG_OK) {
		rit = NULL;
		found = pkghash_get_value(universe->items, c->uid);
		assert(found != NULL);

		LL_FOREACH(found, cur) {
			if (cur->pkg->type != PKG_INSTALLED) {
				rit = cur;
				break;
			}
		}

		assert(rit != NULL);
		if (rit->priority >= req->items[1]->priority) {
			pkg_jobs_update_universe_item_priority(universe, req->items[1],
					rit->priority + 1, PKG_PRIORITY_UPDATE_CONFLICT);
			/*
			 * Update priorities for a remote part as well
			 */
			pkg_jobs_update_universe_item_priority(universe, req->items[0],
					req->items[0]->priority, PKG_PRIORITY_UPDATE_REQUEST);
		}
	}
}


void
pkg_jobs_update_universe_priority(struct pkg_jobs_universe *universe,
	struct pkg_job_universe_item *it, enum pkg_priority_update_type type)
{
	pkg_jobs_update_universe_item_priority(universe, it, 0, type);
}

static void
pkg_jobs_universe_provide_free(struct pkg_job_provide *pr)
{
	struct pkg_job_provide *cur, *tmp;

	DL_FOREACH_SAFE(pr, cur, tmp) {
		free (cur);
	}
}

static void
pkg_jobs_universe_replacement_free(struct pkg_job_replace *r)
{
	free(r->new_uid);
	free(r->old_uid);
	free(r);
}

void
pkg_jobs_universe_free(struct pkg_jobs_universe *universe)
{
	struct pkg_job_universe_item *cur, *curtmp;
	pkghash_it it;

	it = pkghash_iterator(universe->items);
	while (pkghash_next(&it)) {
		LL_FOREACH_SAFE(it.value, cur, curtmp) {
			pkg_free(cur->pkg);
			free(cur);
		}
	}
	pkghash_destroy(universe->items);
	universe->items = NULL;
	pkghash_destroy(universe->seen);
	universe->seen = NULL;
	it = pkghash_iterator(universe->provides);
	while (pkghash_next(&it))
		pkg_jobs_universe_provide_free(it.value);
	pkghash_destroy(universe->provides);
	LL_FREE(universe->uid_replaces, pkg_jobs_universe_replacement_free);
}

struct pkg_jobs_universe *
pkg_jobs_universe_new(struct pkg_jobs *j)
{
	struct pkg_jobs_universe *universe;

	universe = xcalloc(1, sizeof(struct pkg_jobs_universe));
	universe->j = j;

	return (universe);
}

struct pkg_job_universe_item *
pkg_jobs_universe_find(struct pkg_jobs_universe *universe, const char *uid)
{
	return (pkghash_get_value(universe->items, uid));
}

void
pkg_jobs_universe_change_uid(struct pkg_jobs_universe *universe,
	struct pkg_job_universe_item *unit,
	const char *new_uid, bool update_rdeps)
{
	struct pkg_dep *rd = NULL, *d = NULL;
	struct pkg_job_universe_item *found, *tmp;

	struct pkg *lp;
	struct pkg_job_replace *replacement;

	if (update_rdeps) {
		/* For all rdeps update deps accordingly */
		while (pkg_rdeps(unit->pkg, &rd) == EPKG_OK) {
			found = pkg_jobs_universe_find(universe, rd->uid);
			if (found == NULL) {
				lp = pkg_jobs_universe_get_local(universe, rd->uid, 0);
				/* XXX */
				assert(lp != NULL);
				pkg_jobs_universe_process_item(universe, lp, &found);
			}

			if (found != NULL) {
				while (pkg_deps(found->pkg, &d) == EPKG_OK) {
					if (strcmp(d->uid, unit->pkg->uid) == 0) {
						free(d->uid);
						d->uid = xstrdup(new_uid);
					}
				}
			}
		}
	}

	replacement = xcalloc(1, sizeof(*replacement));
	replacement->old_uid = xstrdup(unit->pkg->uid);
	replacement->new_uid = xstrdup(new_uid);
	LL_PREPEND(universe->uid_replaces, replacement);

	tmp = pkghash_delete(universe->items, unit->pkg->uid);
	if (tmp != NULL)
		tmp->inhash = false;
	free(unit->pkg->uid);
	unit->pkg->uid = xstrdup(new_uid);

	found = pkghash_get_value(universe->items, new_uid);
	if (found != NULL)
		DL_APPEND(found, unit);
	else
		pkghash_safe_add(universe->items, new_uid, unit, NULL);

}

static struct pkg_job_universe_item *
pkg_jobs_universe_select_max_ver(struct pkg_job_universe_item *chain)
{
	struct pkg_job_universe_item *cur, *res = NULL;
	bool found = false;
	int r;

	LL_FOREACH(chain, cur) {
		if (cur->pkg->type == PKG_INSTALLED)
			continue;

		if (res != NULL) {
			r = pkg_version_change_between(cur->pkg, res->pkg);
			if (r == PKG_UPGRADE) {
				res = cur;
				found = true;
			}
			else if (r != PKG_REINSTALL) {
				/*
				 * Actually the selected package is newer than some other
				 * packages in the chain
				 */
				found = true;
			}
		}
		else {
			res = cur;
		}
	}

	return (found ? res : NULL);
}

static struct pkg_job_universe_item *
pkg_jobs_universe_select_max_prio(struct pkg_job_universe_item *chain)
{
	struct pkg_repo *repo;
	unsigned int max_pri = 0;
	struct pkg_job_universe_item *cur, *res = NULL;

	LL_FOREACH(chain, cur) {
		if (cur->pkg->type == PKG_INSTALLED)
			continue;

		if (cur->pkg->reponame) {
			repo = pkg_repo_find(cur->pkg->reponame);
			if (repo && repo->priority > max_pri) {
				res = cur;
				max_pri = repo->priority;
			}
		}
	}

	return (res);
}

static struct pkg_job_universe_item *
pkg_jobs_universe_select_same_repo(struct pkg_job_universe_item *chain,
	struct pkg_job_universe_item *local, const char *assumed_reponame)
{
	struct pkg_repo *local_repo = NULL, *repo;
	struct pkg_job_universe_item *cur, *res = NULL;

	if (!local) {

		if (assumed_reponame) {
			local_repo = pkg_repo_find(assumed_reponame);
		}
	}
	else {
		if (local->pkg->reponame) {
			local_repo = pkg_repo_find(local->pkg->reponame);
		}
		else {
			const char *lrepo = pkg_kv_get(&local->pkg->annotations, "repository");
			if (lrepo) {
				local_repo = pkg_repo_find(lrepo);
			}
		}
	}

	if (local_repo == NULL) {
		return (NULL);
	}
	else {
		LL_FOREACH(chain, cur) {
			if (cur->pkg->type == PKG_INSTALLED)
				continue;

			if (cur->pkg->reponame) {
				repo = pkg_repo_find(cur->pkg->reponame);
				if (repo == local_repo) {
					res = cur;
					break;
				}
			}
		}
	}

	return (res);
}

struct pkg_job_universe_item *
pkg_jobs_universe_select_candidate(struct pkg_job_universe_item *chain,
    struct pkg_job_universe_item *local, bool conservative,
    const char *reponame, bool pinning)
{
	struct pkg_job_universe_item *res = NULL;

	if (local == NULL) {
		/* New package selection */
		if (conservative) {
			/* Check same repo */
			if (reponame && pinning) {
				res =  pkg_jobs_universe_select_same_repo(chain, NULL, reponame);
			}

			if (res == NULL) {
				/* Priority -> version */
				res = pkg_jobs_universe_select_max_prio(chain);
				if (res == NULL) {
					res = pkg_jobs_universe_select_max_ver(chain);
				}
			}
		}
		else {
			if (reponame && pinning) {
				res =  pkg_jobs_universe_select_same_repo(chain, NULL, reponame);
			}

			if (res == NULL) {
				/* Version -> priority */
				res = pkg_jobs_universe_select_max_ver(chain);
				if (res == NULL) {
					res = pkg_jobs_universe_select_max_prio(chain);
				}
			}
		}
	}
	else {
		if (conservative) {
			/* same -> prio -> version */
			if (pinning)
				res = pkg_jobs_universe_select_same_repo(chain, local, reponame);
			if (res == NULL) {
				res = pkg_jobs_universe_select_max_prio(chain);
			}
			if (res == NULL) {
				res = pkg_jobs_universe_select_max_ver(chain);
			}
		}
		else {
			/* same -> version -> prio */
			if (pinning)
				res = pkg_jobs_universe_select_same_repo(chain, local, reponame);
			if (res == NULL) {
				res = pkg_jobs_universe_select_max_ver(chain);
			}
			if (res == NULL) {
				res = pkg_jobs_universe_select_max_prio(chain);
			}
		}
	}

	/* Fallback to any */
	return (res != NULL ? res : chain);
}

void
pkg_jobs_universe_process_upgrade_chains(struct pkg_jobs *j)
{
	struct pkg_job_universe_item *unit, *cur, *local;
	struct pkg_job_request *req;
	struct pkg_job_request_item *rit, *rtmp;
	pkghash_it it;

	it = pkghash_iterator(j->universe->items);
	while (pkghash_next(&it)) {
		unsigned vercnt = 0;
		unit = (struct pkg_job_universe_item *)it.value;

		req = pkghash_get_value(j->request_add, unit->pkg->uid);
		if (req == NULL) {
			/* Not obviously requested */
			continue;
		}

		local = NULL;
		LL_FOREACH(unit, cur) {
			if (cur->pkg->type == PKG_INSTALLED)
				local = cur;
			vercnt ++;
		}

		if (local != NULL && local->pkg->locked) {
			pkg_debug(1, "removing %s from the request as it is locked",
				cur->pkg->uid);
			pkghash_del(j->request_add, req->item->pkg->uid);
			pkg_jobs_request_free(req);
			continue;
		}

		if (vercnt <= 1)
			continue;

		/*
		 * Here we have more than one upgrade candidate,
		 * if local == NULL, then we have two remote repos,
		 * if local != NULL, then we have unspecified upgrade path
		 */

		if ((local == NULL && vercnt > 1) || (vercnt > 2)) {
			/* Select the most recent or one of packages */
			struct pkg_job_universe_item *selected;

			selected = pkg_jobs_universe_select_candidate(unit, local,
				j->conservative, NULL, j->pinning);
			/*
			 * Now remove all requests but selected from the requested
			 * candidates
			 */
			assert(selected != NULL);
			pkghash_del(j->request_add, req->item->pkg->uid);

			/*
			 * We also check if the selected package has different digest,
			 * and if it has the same digest we proceed only if we have a
			 * forced job
			 */
			if (local != NULL && strcmp(local->pkg->digest,
				selected->pkg->digest) == 0 &&
				(j->flags & PKG_FLAG_FORCE) == 0) {
				pkg_debug (1, "removing %s from the request as it is the "
								"same as local", selected->pkg->uid);
				continue;
			}

			LL_FOREACH(unit, cur) {
				if (cur == selected)
					continue;

				DL_FOREACH_SAFE(req->item, rit, rtmp) {
					if (rit->unit == cur) {
						DL_DELETE(req->item, rit);
						free(rit);
					}
				}
			}
			if (req->item == NULL) {
				rit = xcalloc(1, sizeof(*rit));
				rit->pkg = selected->pkg;
				rit->unit = selected;
				DL_APPEND(req->item, rit);
			}
			pkghash_safe_add(j->request_add, selected->pkg->uid, req, NULL);
		}
	}
}

struct pkg_job_universe_item*
pkg_jobs_universe_get_upgrade_candidates(struct pkg_jobs_universe *universe,
	const char *uid, struct pkg *lp, bool force, const char *version)
{
	struct pkg *pkg = NULL, *selected = lp;
	struct pkgdb_it *it;
	struct pkg_job_universe_item *unit, *ucur;
	int flag = PKG_LOAD_BASIC|PKG_LOAD_DEPS|PKG_LOAD_OPTIONS|
					PKG_LOAD_REQUIRES|PKG_LOAD_PROVIDES|
					PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|
					PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;
	tll(struct pkg *) candidates = tll_init();

	unit = pkghash_get_value(universe->items, uid);
	if (unit != NULL) {
		/*
		 * If a unit has been found, we have already found the potential
		 * upgrade chain for it
		 */
		if (force) {
			/*
			 * We also need to ensure that a chain contains remote packages
			 * in case of forced upgrade
			 */
			DL_FOREACH(unit, ucur) {
				if (ucur->pkg->type != PKG_INSTALLED) {
					return (unit);
				}
			}
		}
		else {
			return (unit);
		}
	}

	if ((it = pkgdb_repo_query(universe->j->db, uid, MATCH_INTERNAL,
		universe->j->reponame)) == NULL)
		return (NULL);

	while (pkgdb_it_next(it, &pkg, flag) == EPKG_OK) {

		if (version != NULL && strcmp(pkg->version, version) != 0)
			continue;

		if (force) {
			/* Just add everything */
			selected = pkg;
		}
		else {
			if (selected == lp &&
					(lp == NULL || pkg_jobs_need_upgrade(pkg, lp)))
				selected = pkg;
			else if (pkg_version_change_between(pkg, selected) == PKG_UPGRADE)
				selected = pkg;
		}
		tll_push_front(candidates, pkg);
		pkg = NULL;
	}

	pkgdb_it_free(it);

	if (lp != NULL) {
		/* Add local package to the universe as well */
		pkg_jobs_universe_add_pkg(universe, lp, false, NULL);
	}
	if (selected != lp) {
		/* We need to add the whole chain of upgrade candidates */
		tll_foreach(candidates, cit) {
			pkg_jobs_universe_add_pkg(universe, cit->item, force, NULL);
		}
	}
	else {
		tll_free_and_free(candidates, pkg_free);
		return (NULL);
	}

	unit = pkghash_get_value(universe->items, uid);
	tll_free(candidates);

	return (unit);
}
