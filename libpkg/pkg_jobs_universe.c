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

#include "utarray.h"

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/pkg_jobs.h"

#define IS_DELETE(j) ((j)->type == PKG_JOBS_DEINSTALL || (j)->type == PKG_JOBS_AUTOREMOVE)

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
				PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_ANNOTATIONS|
				PKG_LOAD_CONFLICTS;
		else
			flag = PKG_LOAD_BASIC|PKG_LOAD_RDEPS|PKG_LOAD_DEPS|PKG_LOAD_ANNOTATIONS;
	}

	HASH_FIND(hh, universe->items, uid, strlen(uid), unit);
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

	if ((it = pkgdb_query(universe->j->db, uid, MATCH_EXACT)) == NULL)
		return (NULL);

	if (pkgdb_it_next(it, &pkg, flag) != EPKG_OK)
		pkg = NULL;

	pkgdb_it_free(it);

	return (pkg);
}

struct pkg *
pkg_jobs_universe_get_remote(struct pkg_jobs_universe *universe,
	const char *uid, unsigned flag)
{
	struct pkg *pkg = NULL, *selected = NULL;
	struct pkgdb_it *it;
	struct pkg_job_universe_item *unit, *cur, *found;

	if (flag == 0) {
		flag = PKG_LOAD_BASIC|PKG_LOAD_DEPS|PKG_LOAD_OPTIONS|
				PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|
				PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;
	}

	HASH_FIND(hh, universe->items, uid, strlen(uid), unit);
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
			pkgdb_ensure_loaded(universe->j->db, unit->pkg, flag);
			return (unit->pkg);
		}
	}

	if ((it = pkgdb_repo_query(universe->j->db, uid, MATCH_EXACT,
		universe->j->reponame)) == NULL)
		return (NULL);

	while (pkgdb_it_next(it, &pkg, flag) == EPKG_OK) {

		if (selected == NULL) {
			selected = pkg;
			pkg = NULL;
		}
		else if (pkg_version_change_between(pkg, selected) == PKG_UPGRADE) {
			selected = pkg;
			pkg = NULL;
		}

	}
	if (pkg != NULL && pkg != selected)
		pkg_free(pkg);

	pkgdb_it_free(it);

	return (selected);
}

/**
 * Check whether a package is in the universe already or add it
 * @return item or NULL
 */
int
pkg_jobs_universe_add_pkg(struct pkg_jobs_universe *universe, struct pkg *pkg,
		bool force, struct pkg_job_universe_item **found)
{
	struct pkg_job_universe_item *item, *tmp = NULL;
	struct pkg_job_seen *seen;

	pkg_validate(pkg);
	if (pkg->digest == NULL) {
		pkg_debug(3, "no digest found for package %s (%s-%s)",
		    pkg->uid, pkg->name, pkg->version);
		if (pkg_checksum_calculate(pkg, universe->j->db) != EPKG_OK) {
			*found = NULL;
			return (EPKG_FATAL);
		}
	}

	HASH_FIND_STR(universe->seen, pkg->digest, seen);
	if (seen != NULL && !force) {
		if (found != NULL)
			*found = seen->un;

		return (EPKG_END);
	}

	pkg_debug(2, "universe: add new %s pkg: %s, (%s-%s:%s)",
	    (pkg->type == PKG_INSTALLED ? "local" : "remote"), pkg->uid,
	    pkg->name, pkg->version, pkg->digest);

	item = calloc(1, sizeof (struct pkg_job_universe_item));
	if (item == NULL) {
		pkg_emit_errno("pkg_jobs_pkg_insert_universe", "calloc: struct pkg_job_universe_item");
		return (EPKG_FATAL);
	}

	item->pkg = pkg;


	HASH_FIND_STR(universe->items, pkg->uid, tmp);
	if (tmp == NULL)
		HASH_ADD_KEYPTR(hh, universe->items, pkg->uid, strlen(pkg->uid), item);

	DL_APPEND(tmp, item);

	if (seen == NULL) {
		seen = calloc(1, sizeof(struct pkg_job_seen));
		if (seen == NULL) {
			pkg_emit_errno("pkg_jobs_universe_add_pkg", "calloc: struct pkg_job_seen)");
			return (EPKG_FATAL);
		}
		seen->digest = pkg->digest;
		seen->un = item;
		HASH_ADD_KEYPTR(hh, universe->seen, seen->digest, strlen(seen->digest),
			seen);
	}

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
	struct pkg_job_universe_item *unit;
	struct pkg *npkg, *rpkg;

	if (flags & DEPS_FLAG_REVERSE)
		deps_func = pkg_rdeps;
	else
		deps_func = pkg_deps;

	while (deps_func(pkg, &d) == EPKG_OK) {
		HASH_FIND_STR(universe->items, d->uid, unit);
		if (unit != NULL)
			continue;

		rpkg = NULL;
		npkg = NULL;
		if (!(flags & DEPS_FLAG_MIRROR))
			npkg = pkg_jobs_universe_get_local(universe, d->uid, 0);

		if (!(flags & DEPS_FLAG_FORCE_LOCAL)) {

			/* Check for remote dependencies */
			rpkg = pkg_jobs_universe_get_remote(universe, d->uid, 0);
			if (rpkg != NULL && !(flags & DEPS_FLAG_FORCE_UPGRADE)) {
				if (!pkg_jobs_need_upgrade(rpkg, npkg)) {
					/*
					 * We can do it safely here, as rpkg is definitely NOT in
					 * the universe
					 */
					pkg_free(rpkg);
					rpkg = NULL;
				}
			}
		}

		if (npkg == NULL && rpkg == NULL) {
			pkg_emit_error("%s has a missing dependency: %s",
				pkg->name, d->name);

			if (flags & DEPS_FLAG_FORCE_MISSING)
				continue;

			return (EPKG_FATAL);
		}

		if (npkg != NULL)
			if (pkg_jobs_universe_process_item(universe, npkg, &unit) != EPKG_OK)
				continue;

		if (rpkg != NULL) {
			if (npkg != NULL) {
				/* Save automatic flag */
				rpkg->automatic = npkg->automatic;
			}

			pkg_jobs_universe_process_item(universe, rpkg, NULL);
		}
	}

	return (EPKG_OK);
}

static int
pkg_jobs_universe_process_conflicts(struct pkg_jobs_universe *universe,
	struct pkg *pkg)
{
	struct pkg_conflict *c = NULL;
	struct pkg_job_universe_item *unit;
	struct pkg *npkg;

	while (pkg_conflicts(pkg, &c) == EPKG_OK) {
		HASH_FIND_STR(universe->items, c->uid, unit);
		if (unit != NULL)
			continue;

		/* Check local and remote conflicts */
		if (pkg->type == PKG_INSTALLED) {
			/* Installed packages can conflict with remote ones */
			npkg = pkg_jobs_universe_get_remote(universe, c->uid, 0);
			if (npkg == NULL)
				continue;

			pkg_jobs_universe_process_item(universe, npkg, NULL);
		}
		else {
			/* Remote packages can conflict with remote and local */
			npkg = pkg_jobs_universe_get_local(universe, c->uid, 0);
			if (npkg != NULL) {
				if (pkg_jobs_universe_process_item(universe, npkg, NULL) != EPKG_OK)
					continue;

				if (c->type != PKG_CONFLICT_REMOTE_LOCAL) {
					npkg = pkg_jobs_universe_get_remote(universe,
					    c->uid, 0);
					if (npkg == NULL)
						continue;

					pkg_jobs_universe_process_item(universe, npkg, NULL);
				}
			}
		}
	}

	return (EPKG_OK);
}


static int
pkg_jobs_universe_process_shlibs(struct pkg_jobs_universe *universe,
	struct pkg *pkg)
{
	struct pkg_shlib *shlib = NULL;
	struct pkg_job_universe_item *unit;
	struct pkg_job_provide *pr, *prhead;
	struct pkgdb_it *it;
	struct pkg *npkg, *rpkg;
	unsigned flags = PKG_LOAD_BASIC|PKG_LOAD_OPTIONS|PKG_LOAD_DEPS|
				PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|
				PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;

	while (pkg_shlibs_required(pkg, &shlib) == EPKG_OK) {
		HASH_FIND_STR(universe->provides, shlib->name, pr);
		if (pr != NULL)
			continue;

		/* Not found, search in the repos */
		it = pkgdb_repo_shlib_provide(universe->j->db,
			shlib->name, universe->j->reponame);
		if (it != NULL) {
			rpkg = NULL;
			prhead = NULL;
			while (pkgdb_it_next(it, &rpkg, flags) == EPKG_OK) {
				/* Check for local packages */
				HASH_FIND_STR(universe->items, rpkg->uid, unit);
				if (unit != NULL) {
					if (pkg_jobs_need_upgrade (rpkg, unit->pkg)) {
						/* Remote provide is newer, so we can add it */
						if (pkg_jobs_universe_process_item(universe, rpkg,
							&unit) != EPKG_OK)
							continue;

						rpkg = NULL;
					}
				}
				else {
					/* Maybe local package has just been not added */
					npkg = pkg_jobs_universe_get_local(universe, rpkg->uid, 0);
					if (npkg != NULL) {
						if (pkg_jobs_universe_process_item(universe, npkg,
							&unit) != EPKG_OK)
							return (EPKG_FATAL);
						if (pkg_jobs_need_upgrade (rpkg, npkg)) {
							/* Remote provide is newer, so we can add it */
							if (pkg_jobs_universe_process_item(universe, rpkg,
								&unit) != EPKG_OK)
								continue;
						}
					}
				}

				/* Skip seen packages */
				if (unit == NULL) {
					struct pkg_job_seen *seen;

					if (rpkg->digest == NULL) {
						pkg_debug(3, "no digest found for package %s", rpkg->uid);
						if (pkg_checksum_calculate(pkg, universe->j->db) != EPKG_OK) {
							return (EPKG_FATAL);
						}
					}
					HASH_FIND_STR(universe->seen, rpkg->digest, seen);
					if (seen == NULL) {
						pkg_jobs_universe_process_item(universe, rpkg,
							&unit);

						/* Reset package to avoid freeing */
						rpkg = NULL;
					}
					else {
						unit = seen->un;
					}
				}

				pr = calloc (1, sizeof (*pr));
				if (pr == NULL) {
					pkg_emit_errno("pkg_jobs_add_universe", "calloc: "
						"struct pkg_job_provide");
					return (EPKG_FATAL);
				}

				pr->un = unit;
				pr->provide = shlib->name;

				if (prhead == NULL) {
					DL_APPEND(prhead, pr);
					HASH_ADD_KEYPTR(hh, universe->provides, pr->provide,
						strlen(pr->provide), prhead);
				}
				else {
					DL_APPEND(prhead, pr);
				}
			}
			pkgdb_it_free(it);
			if (prhead == NULL) {
				pkg_debug(1, "cannot find packages that provide %s required for %s",
				    shlib->name, pkg->name);
				/*
				 * XXX: this is not normal but it is very common for the existing
				 * repos, hence we just ignore this stale dependency
				 */
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

	job_flags = universe->j->flags;

	/*
	 * Add pkg itself. If package is already seen then we check the `processed`
	 * flag that means that we have already tried to check our universe
	 */
	rc = pkg_jobs_universe_add_pkg(universe, pkg, false, &found);
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
		/* Handle conflicts */
		rc = pkg_jobs_universe_process_conflicts(universe, pkg);
		if (rc != EPKG_OK)
			return (rc);
		/* For remote packages we should also handle shlib deps */
		if (pkg->type != PKG_INSTALLED) {
			rc = pkg_jobs_universe_process_shlibs(universe, pkg);
			if (rc != EPKG_OK)
				return (rc);
		}
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
		    is_local, it->pkg->uid, it->pkg->digest, it->priority, priority, type);
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
			HASH_FIND_STR(universe->items, d->uid, found);
			if (found != NULL) {
				LL_FOREACH(found, cur) {
					if (cur->priority < priority + 1)
						pkg_jobs_update_universe_item_priority(universe, cur,
								priority + 1, type);
				}
			}
		}

		d = NULL;
		maxpri = priority;
		while (rdeps_func(it->pkg, &d) == EPKG_OK) {
			HASH_FIND_STR(universe->items, d->uid, found);
			if (found != NULL) {
				LL_FOREACH(found, cur) {
					if (cur->priority >= maxpri) {
						maxpri = cur->priority + 1;
					}
				}
			}
		}
		if (maxpri != priority) {
			pkg_jobs_update_universe_item_priority(universe, it,
					maxpri, type);
			return;
		}
		if (it->pkg->type != PKG_INSTALLED) {
			while (pkg_conflicts(it->pkg, &c) == EPKG_OK) {
				HASH_FIND_STR(universe->items, c->uid, found);
				if (found != NULL) {
					LL_FOREACH(found, cur) {
						if (cur->pkg->type == PKG_INSTALLED) {
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
		HASH_FIND_STR(universe->items, c->uid, found);
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
	struct pkg_job_universe_item *un, *untmp, *cur, *curtmp;

	HASH_ITER(hh, universe->items, un, untmp) {
		HASH_DEL(universe->items, un);

		LL_FOREACH_SAFE(un, cur, curtmp) {
			pkg_free(cur->pkg);
			free(cur);
		}
	}
	HASH_FREE(universe->seen, free);
	HASH_FREE(universe->provides, pkg_jobs_universe_provide_free);
	LL_FREE(universe->uid_replaces, pkg_jobs_universe_replacement_free);
}


struct pkg_jobs_universe *
pkg_jobs_universe_new(struct pkg_jobs *j)
{
	struct pkg_jobs_universe *universe;

	universe = calloc(1, sizeof(struct pkg_jobs_universe));
	if (universe == NULL) {
		pkg_emit_errno("pkg_jobs_universe_new", "calloc");
		return (NULL);
	}

	universe->j = j;

	return (universe);
}

struct pkg_job_universe_item *
pkg_jobs_universe_find(struct pkg_jobs_universe *universe, const char *uid)
{
	struct pkg_job_universe_item *unit;

	HASH_FIND_STR(universe->items, uid, unit);

	return (unit);
}

struct pkg_job_seen *
pkg_jobs_universe_seen(struct pkg_jobs_universe *universe, const char *digest)
{
	struct pkg_job_seen *seen;

	HASH_FIND_STR(universe->seen, digest, seen);

	return (seen);
}

void
pkg_jobs_universe_change_uid(struct pkg_jobs_universe *universe,
	struct pkg_job_universe_item *unit,
	const char *new_uid, size_t uidlen, bool update_rdeps)
{
	struct pkg_dep *rd = NULL, *d = NULL;
	struct pkg_job_universe_item *found;

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
						d->uid = strdup(new_uid);
					}
				}
			}
		}
	}

	replacement = calloc(1, sizeof(*replacement));
	if (replacement != NULL) {
		replacement->old_uid = strdup(unit->pkg->uid);
		replacement->new_uid = strdup(new_uid);
		LL_PREPEND(universe->uid_replaces, replacement);
	}

	HASH_DELETE(hh, universe->items, unit);
	free(unit->pkg->uid);
	unit->pkg->uid = strdup(new_uid);

	HASH_FIND(hh, universe->items, new_uid, uidlen, found);
	if (found != NULL)
		DL_APPEND(found, unit);
	else
		HASH_ADD_KEYPTR(hh, universe->items, new_uid, uidlen, unit);

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
	struct pkg_job_universe_item *local)
{
	struct pkg_repo *local_repo = NULL, *repo;
	struct pkg_job_universe_item *cur, *res = NULL;

	if (local->pkg->reponame) {
		local_repo = pkg_repo_find(local->pkg->reponame);
	}
	else {
		const char *lrepo = pkg_kv_get(&local->pkg->annotations, "repository");
		if (lrepo) {
			local_repo = pkg_repo_find(lrepo);
		}
	}

	if (local_repo == NULL) {
		/* Return any package */
		LL_FOREACH(chain, cur) {
			if (cur->pkg->type == PKG_INSTALLED)
				continue;
			else
				return (cur);
		}
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

static struct pkg_job_universe_item *
pkg_jobs_universe_select_candidate(struct pkg_job_universe_item *chain,
	struct pkg_job_universe_item *local, bool conservative)
{
	struct pkg_job_universe_item *res;

	if (local == NULL) {
		/* New package selection */
		if (conservative) {
			/* Priority -> version */
			res = pkg_jobs_universe_select_max_prio(chain);
			if (res == NULL) {
				res = pkg_jobs_universe_select_max_ver(chain);
			}
		}
		else {
			/* Version -> priority */
			res = pkg_jobs_universe_select_max_ver(chain);
			if (res == NULL) {
				res = pkg_jobs_universe_select_max_prio(chain);
			}
		}
	}
	else {
		if (conservative) {
			/* same -> prio -> version */
			res = pkg_jobs_universe_select_same_repo(chain, local);
			if (res == NULL) {
				res = pkg_jobs_universe_select_max_prio(chain);
			}
			if (res == NULL) {
				res = pkg_jobs_universe_select_max_ver(chain);
			}
		}
		else {
			/* version -> prio -> same */
			res = pkg_jobs_universe_select_max_ver(chain);
			if (res == NULL) {
				res = pkg_jobs_universe_select_max_prio(chain);
			}
			if (res == NULL) {
				res = pkg_jobs_universe_select_same_repo(chain, local);
			}
		}
	}

	/* Fallback to any */
	return (res != NULL ? res : chain);
}

void
pkg_jobs_universe_process_upgrade_chains(struct pkg_jobs *j)
{
	struct pkg_job_universe_item *unit, *tmp, *cur, *local;
	struct pkg_job_request *req;
	struct pkg_job_request_item *rit, *rtmp;
	bool conservative = false;

	conservative = pkg_object_bool(pkg_config_get("CONSERVATIVE_UPGRADE"));

	HASH_ITER(hh, j->universe->items, unit, tmp) {
		unsigned vercnt = 0;

		HASH_FIND_STR(j->request_add, unit->pkg->uid, req);
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
			HASH_DEL(j->request_add, req);
			pkg_jobs_request_free(req);
		}
		else if (vercnt > 1) {
			/*
			 * Here we have more than one upgrade candidate,
			 * if local == NULL, then we have two remote repos,
			 * if local != NULL, then we have unspecified upgrade path
			 */

			if ((local == NULL && vercnt > 1) || (vercnt > 2)) {
				/* Select the most recent or one of packages */
				struct pkg_job_universe_item *selected;

				selected = pkg_jobs_universe_select_candidate(unit, local,
					conservative);
				/*
				 * Now remove all requests but selected from the requested
				 * candidates
				 */
				assert(selected != NULL);
				HASH_DEL(j->request_add, req);

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
					if (cur != selected) {
						DL_FOREACH_SAFE(req->item, rit, rtmp) {
							if (rit->unit == cur) {
								DL_DELETE(req->item, rit);
								free(rit);
							}
						}
					}
				}
				HASH_ADD_KEYPTR(hh, j->request_add, selected->pkg->uid,
					strlen (selected->pkg->uid), req);
			}
		}
	}
}


struct pkg_job_universe_item*
pkg_jobs_universe_get_upgrade_candidates(struct pkg_jobs_universe *universe,
	const char *uid, struct pkg *lp, bool force)
{
	struct pkg *pkg = NULL, *selected = lp;
	struct pkgdb_it *it;
	struct pkg_job_universe_item *unit, *ucur;
	int flag = PKG_LOAD_BASIC|PKG_LOAD_DEPS|PKG_LOAD_OPTIONS|
					PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|
					PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;
	UT_array *candidates;
	struct pkg **p = NULL;

	HASH_FIND(hh, universe->items, uid, strlen(uid), unit);
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

	if ((it = pkgdb_repo_query(universe->j->db, uid, MATCH_EXACT,
		universe->j->reponame)) == NULL)
		return (NULL);

	utarray_new(candidates, &ut_ptr_icd);
	while (pkgdb_it_next(it, &pkg, flag) == EPKG_OK) {

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
		utarray_push_back(candidates, &pkg);
		pkg = NULL;
	}

	pkgdb_it_free(it);

	if (lp != NULL) {
		/* Add local package to the universe as well */
		pkg_jobs_universe_add_pkg(universe, lp, false, NULL);
	}
	if (selected != lp) {
		/* We need to add the whole chain of upgrade candidates */
		while ((p = (struct pkg **)utarray_next(candidates, p)) != NULL) {
			pkg_jobs_universe_add_pkg(universe, *p, true, NULL);
		}
	}
	else {
		while ((p = (struct pkg **)utarray_next(candidates, p)) != NULL) {
			pkg_free(*p);
		}

		utarray_free(candidates);

		return (NULL);
	}

	HASH_FIND(hh, universe->items, uid, strlen(uid), unit);
	utarray_free(candidates);

	return (unit);
}
