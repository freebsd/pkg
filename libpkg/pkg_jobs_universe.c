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

#include <sys/param.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <libutil.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
	struct pkg_job_universe_item *unit;

	if (flag == 0) {
		if (!IS_DELETE(universe->j))
			flag = PKG_LOAD_BASIC|PKG_LOAD_DEPS|PKG_LOAD_RDEPS|PKG_LOAD_OPTIONS|
				PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_ANNOTATIONS|
				PKG_LOAD_CONFLICTS;
		else
			flag = PKG_LOAD_BASIC|PKG_LOAD_RDEPS|PKG_LOAD_DEPS|PKG_LOAD_ANNOTATIONS;
	}

	HASH_FIND(hh, universe->items, uid, strlen(uid), unit);
	if (unit != NULL && unit->pkg->type == PKG_INSTALLED) {
		pkgdb_ensure_loaded(universe->j->db, unit->pkg, flag);
		return (unit->pkg);
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
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	struct pkg_job_universe_item *unit;

	if (flag == 0) {
		flag = PKG_LOAD_BASIC|PKG_LOAD_DEPS|PKG_LOAD_OPTIONS|
				PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|
				PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;
	}

	HASH_FIND(hh, universe->items, uid, strlen(uid), unit);
	if (unit != NULL && unit->pkg->type != PKG_INSTALLED) {
		pkgdb_ensure_loaded(universe->j->db, unit->pkg, flag);
		return (unit->pkg);
	}

	if ((it = pkgdb_repo_query(universe->j->db, uid, MATCH_EXACT,
		universe->j->reponame)) == NULL)
		return (NULL);

	if (pkgdb_it_next(it, &pkg, flag) != EPKG_OK)
		pkg = NULL;

	pkgdb_it_free(it);

	return (pkg);
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
	const char *uid, *digest, *version, *name;
	struct pkg_job_seen *seen;

	pkg_get(pkg, PKG_UNIQUEID, &uid, PKG_DIGEST, &digest,
			PKG_VERSION, &version, PKG_NAME, &name);
	if (digest == NULL) {
		pkg_debug(3, "no digest found for package %s (%s-%s)", uid,
				name, version);
		if (pkg_checksum_calculate(pkg, universe->j->db) != EPKG_OK) {
			*found = NULL;
			return (EPKG_FATAL);
		}
		pkg_get(pkg, PKG_DIGEST, &digest);
	}

	HASH_FIND_STR(universe->seen, digest, seen);
	if (seen != NULL && !force) {
		if (found != NULL)
			*found = seen->un;

		return (EPKG_END);
	}

	pkg_debug(2, "universe: add new %s pkg: %s, (%s-%s:%s)",
				(pkg->type == PKG_INSTALLED ? "local" : "remote"), uid,
				name, version, digest);

	item = calloc(1, sizeof (struct pkg_job_universe_item));
	if (item == NULL) {
		pkg_emit_errno("pkg_jobs_pkg_insert_universe", "calloc: struct pkg_job_universe_item");
		return (EPKG_FATAL);
	}

	item->pkg = pkg;

	HASH_FIND_STR(universe->items, uid, tmp);
	if (tmp == NULL)
		HASH_ADD_KEYPTR(hh, universe->items, uid, strlen(uid), item);

	DL_APPEND(tmp, item);

	if (seen == NULL) {
		seen = calloc(1, sizeof(struct pkg_job_seen));
		if (seen == NULL) {
			pkg_emit_errno("pkg_jobs_universe_add_pkg", "calloc: struct pkg_job_seen)");
			return (EPKG_FATAL);
		}
		seen->digest = digest;
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
			const char *name;

			pkg_get(pkg, PKG_NAME, &name);
			pkg_emit_error("%s has a missing dependency: %s",
				name, pkg_dep_get(d, PKG_DEP_NAME));

			if (flags & DEPS_FLAG_FORCE_MISSING)
				continue;

			return (EPKG_FATAL);
		}

		if (npkg != NULL)
			if (pkg_jobs_universe_process_item(universe, npkg, &unit) != EPKG_OK)
				continue;

		/* Explicitly request for a dependency for mirroring */
		if (flags & DEPS_FLAG_MIRROR)
			pkg_jobs_add_req(universe->j, d->uid, unit);

		if (rpkg != NULL) {
			if (npkg != NULL) {
				/* Save automatic flag */
				bool automatic;

				pkg_get(npkg, PKG_AUTOMATIC, &automatic);
				pkg_set(rpkg, PKG_AUTOMATIC, automatic);
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
		HASH_FIND_STR(universe->items, pkg_conflict_uniqueid(c), unit);
		if (unit != NULL)
			continue;

		/* Check local and remote conflicts */
		if (pkg->type == PKG_INSTALLED) {
			/* Installed packages can conflict with remote ones */
			npkg = pkg_jobs_universe_get_remote(universe, pkg_conflict_uniqueid(c), 0);
			if (npkg == NULL)
				continue;

			pkg_jobs_universe_process_item(universe, npkg, NULL);
		}
		else {
			/* Remote packages can conflict with remote and local */
			npkg = pkg_jobs_universe_get_local(universe, pkg_conflict_uniqueid(c), 0);
			if (npkg != NULL) {
				if (pkg_jobs_universe_process_item(universe, npkg, NULL) != EPKG_OK)
					continue;

				if (c->type != PKG_CONFLICT_REMOTE_LOCAL) {
					npkg = pkg_jobs_universe_get_remote(universe,
						pkg_conflict_uniqueid(c), 0);
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
		HASH_FIND_STR(universe->provides, pkg_shlib_name(shlib), pr);
		if (pr != NULL)
			continue;

		/* Not found, search in the repos */
		it = pkgdb_repo_shlib_provide(universe->j->db,
			pkg_shlib_name(shlib), universe->j->reponame);
		if (it != NULL) {
			rpkg = NULL;
			prhead = NULL;
			while (pkgdb_it_next(it, &rpkg, flags) == EPKG_OK) {
				const char *digest, *uid;

				pkg_get(rpkg, PKG_DIGEST, &digest, PKG_UNIQUEID, &uid);
				/* Check for local packages */
				HASH_FIND_STR(universe->items, uid, unit);
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
					npkg = pkg_jobs_universe_get_local(universe, uid, 0);
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

					if (digest == NULL) {
						pkg_debug(3, "no digest found for package %s", uid);
						if (pkg_checksum_calculate(pkg, universe->j->db) != EPKG_OK) {
							return (EPKG_FATAL);
						}
						pkg_get(pkg, PKG_DIGEST, &digest);
					}
					HASH_FIND_STR(universe->seen, digest, seen);
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
				pr->provide = pkg_shlib_name(shlib);

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
				const char *name;

				pkg_get(pkg, PKG_NAME, &name);
				pkg_debug(1, "cannot find packages that provide %s required for %s",
					pkg_shlib_name(shlib), name);
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

	job_flags = universe->j->flags;

	/* Add pkg itself */
	rc = pkg_jobs_universe_add_pkg(universe, pkg, false, result);
	if (rc == EPKG_END)
		return (EPKG_OK);
	else if (rc != EPKG_OK)
		return (rc);

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
	case PKG_JOBS_DEINSTALL:
	case PKG_JOBS_AUTOREMOVE:
		/* For delete jobs we worry only about local reverse deps */
		flags |= DEPS_FLAG_REVERSE|DEPS_FLAG_FORCE_LOCAL;
		if (!(job_flags & PKG_FLAG_FORCE))
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
	const char *uid, *digest;
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
		pkg_get(item->pkg, PKG_UNIQUEID, &uid);
		pkg_debug(2, "approaching recursion limit at %d, while processing of"
					" package %s", priority, uid);
	}

	LL_FOREACH(item, it) {

		pkg_get(it->pkg, PKG_UNIQUEID, &uid, PKG_DIGEST, &digest);
		if ((item->next != NULL || item->prev != NULL) &&
				it->pkg->type != PKG_INSTALLED &&
				(type == PKG_PRIORITY_UPDATE_CONFLICT ||
				 type == PKG_PRIORITY_UPDATE_DELETE)) {
			/*
			 * We do not update priority of a remote part of conflict, as we know
			 * that remote packages should not contain conflicts (they should be
			 * resolved in request prior to calling of this function)
			 */
			pkg_debug(4, "skip update priority for %s-%s", uid, digest);
			continue;
		}
		if (it->priority > priority)
			continue;

		is_local = it->pkg->type == PKG_INSTALLED ? "local" : "remote";
		pkg_debug(2, "universe: update %s priority of %s(%s): %d -> %d, reason: %d",
				is_local, uid, digest, it->priority, priority, type);
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
				HASH_FIND_STR(universe->items, pkg_conflict_uniqueid(c), found);
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
		HASH_FIND_STR(universe->items, pkg_conflict_uniqueid(c), found);
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
	const char *old_uid;
	struct pkg_job_replace *replacement;

	pkg_get(unit->pkg, PKG_UNIQUEID, &old_uid);

	if (update_rdeps) {
		/* For all rdeps update deps accordingly */
		while (pkg_rdeps(unit->pkg, &rd) == EPKG_OK) {
			found = pkg_jobs_universe_find(universe, rd->uid);
			if (found == NULL) {
				lp = pkg_jobs_universe_get_local(universe, rd->uid, 0);
				pkg_jobs_universe_process_item(universe, lp, &found);
			}

			if (found != NULL) {
				while (pkg_deps(found->pkg, &d) == EPKG_OK) {
					if (strcmp(d->uid, old_uid) == 0) {
						free(d->uid);
						d->uid = strdup(new_uid);
					}
				}
			}
		}
	}

	replacement = calloc(1, sizeof(*replacement));
	if (replacement != NULL) {
		replacement->old_uid = strdup(old_uid);
		replacement->new_uid = strdup(new_uid);
		LL_PREPEND(universe->uid_replaces, replacement);
	}

	HASH_DELETE(hh, universe->items, unit);
	pkg_set(unit->pkg, PKG_UNIQUEID, new_uid);
	HASH_FIND(hh, universe->items, new_uid, uidlen, found);
	if (found != NULL)
		DL_APPEND(found, unit);
	else
		HASH_ADD_KEYPTR(hh, universe->items, new_uid, uidlen, unit);

}

void
pkg_jobs_universe_process_upgrade_chains(struct pkg_jobs *j)
{
	struct pkg_job_universe_item *unit, *tmp, *cur, *local;
	struct pkg_job_request *req;

	HASH_ITER(hh, j->universe->items, unit, tmp) {
		unsigned vercnt = 0;

		local = NULL;
		LL_FOREACH(unit, cur) {
			if (cur->pkg->type == PKG_INSTALLED)
				local = cur;
			vercnt ++;
		}

		if (vercnt > 1) {
			/*
			 * Here we have more than one upgrade candidate,
			 * if local == NULL, then we have two remote repos,
			 * if local != NULL, then we have unspecified upgrade path
			 */

			if ((local == NULL && vercnt > 1) || (vercnt > 2)) {
				/* Select the most recent or one of packages */
				struct pkg_job_universe_item *selected = NULL;
				LL_FOREACH(unit, cur) {
					if (cur->pkg->type == PKG_INSTALLED)
						continue;

					if (selected != NULL && pkg_version_change_between(cur->pkg,
						selected->pkg) == PKG_UPGRADE) {
						selected = cur;
					}
					else if (selected == NULL) {
						selected = cur;
					}
				}
				/* Now remove all requests but selected */
				assert(selected != NULL);
				LL_FOREACH(unit, cur) {
					if (cur != selected) {
						HASH_FIND_PTR(j->request_add, &cur, req);
						if (req != NULL)
							HASH_DEL(j->request_add, req);
					}
				}
			}
		}
	}
}
