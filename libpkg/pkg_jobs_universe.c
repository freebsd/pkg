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

/**
 * Check whether a package is in the universe already or add it
 * @return item or NULL
 */
static int
pkg_jobs_handle_pkg_universe(struct pkg_jobs *j, struct pkg *pkg,
		struct pkg_job_universe_item **found)
{
	struct pkg_job_universe_item *item, *tmp = NULL;
	const char *uid, *digest, *version, *name;
	struct pkg_job_seen *seen;

	pkg_get(pkg, PKG_UNIQUEID, &uid, PKG_DIGEST, &digest,
			PKG_VERSION, &version, PKG_NAME, &name);
	if (digest == NULL) {
		pkg_debug(3, "no digest found for package %s (%s-%s)", uid,
				name, version);
		if (pkg_checksum_calculate(pkg, j->db) != EPKG_OK) {
			*found = NULL;
			return (EPKG_FATAL);
		}
		pkg_get(pkg, PKG_DIGEST, &digest);
	}

	HASH_FIND_STR(j->seen, digest, seen);
	if (seen != NULL) {
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

	HASH_FIND_STR(j->universe, uid, tmp);
	if (tmp == NULL) {
		HASH_ADD_KEYPTR(hh, j->universe, uid, strlen(uid), item);
	}

	DL_APPEND(tmp, item);

	seen = calloc(1, sizeof(struct pkg_job_seen));
	seen->digest = digest;
	seen->un = item;
	HASH_ADD_KEYPTR(hh, j->seen, seen->digest, strlen(seen->digest), seen);

	j->total++;

	if (found != NULL)
		*found = item;

	return (EPKG_OK);
}

static int
pkg_jobs_add_universe(struct pkg_jobs *j, struct pkg *pkg,
		bool recursive, bool deps_only, struct pkg_job_universe_item **result)
{
	struct pkg_dep *d = NULL;
	struct pkg_conflict *c = NULL;
	struct pkg *npkg, *rpkg;
	struct pkg_job_universe_item *unit;
	struct pkg_shlib *shlib = NULL;
	struct pkgdb_it *it;
	struct pkg_job_provide *pr, *prhead;
	struct pkg_job_seen *seen;
	int ret;
	bool automatic = false, mirror = false;
	const char *uid, *name, *digest;
	unsigned flags = PKG_LOAD_BASIC|PKG_LOAD_OPTIONS|PKG_LOAD_DEPS|
			PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|
			PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;

	if (!deps_only) {
		/* Add the requested package itself */
		ret = pkg_jobs_handle_pkg_universe(j, pkg, result);

		if (ret == EPKG_END)
			return (EPKG_OK);
		else if (ret == EPKG_OK && !recursive)
			return (EPKG_OK);
		else if (ret != EPKG_OK)
			return (EPKG_FATAL);
	}

	mirror = (j->flags & PKG_FLAG_FETCH_MIRROR) ? true : false;

	/* Go through all depends */
	while (pkg_deps(pkg, &d) == EPKG_OK) {
		/* XXX: this assumption can be applied only for the current plain dependencies */
		HASH_FIND_STR(j->universe, d->uid, unit);
		if (unit != NULL)
			continue;

		rpkg = NULL;
		npkg = NULL;
		if (!mirror)
			npkg = pkg_jobs_get_local_pkg(j, d->uid, 0);

		if (npkg == NULL && !IS_DELETE(j)) {
			/*
			 * We have a package installed, but its dependencies are not,
			 * try to search a remote dependency
			 */
			if (!mirror) {
				pkg_get(pkg, PKG_NAME, &name);
				pkg_debug(1, "dependency %s of local package %s is not installed",
					pkg_dep_get(d, PKG_DEP_NAME), name);
			}
			npkg = pkg_jobs_get_remote_pkg(j, d->uid, 0);
			if (npkg == NULL) {
				/* Cannot continue */
				pkg_emit_error("%s has a missing dependency: %s",
						name, pkg_dep_get(d, PKG_DEP_NAME));

				if ((j->flags & PKG_FLAG_FORCE_MISSING) == PKG_FLAG_FORCE_MISSING)
					continue;

				return (EPKG_FATAL);
			}
		}
		else if (npkg == NULL) {
			/* For delete jobs we don't care about uninstalled dependencies */
			continue;
		}
		else if (!IS_DELETE(j) && npkg->type == PKG_INSTALLED) {
			/* For upgrade jobs we need to ensure that we do not have a newer version */
			rpkg = pkg_jobs_get_remote_pkg(j, d->uid, 0);
			if (rpkg != NULL) {
				if (!pkg_need_upgrade(rpkg, npkg, false)) {
					/*
					 * We can do it safely here, as rpkg is definitely NOT in
					 * the universe
					 */
					pkg_free(rpkg);
					rpkg = NULL;
				}
			}
		}

		if (pkg_jobs_add_universe(j, npkg, recursive, false, &unit) != EPKG_OK)
			continue;

		if (mirror)
			pkg_jobs_add_req(j, d->uid, unit);

		if (rpkg != NULL) {
			/* Save automatic flag */
			pkg_get(npkg, PKG_AUTOMATIC, &automatic);
			pkg_set(rpkg, PKG_AUTOMATIC, automatic);

			if (pkg_jobs_add_universe(j, rpkg, recursive, false, NULL) != EPKG_OK)
				continue;
		}
	}

	/*
	 * XXX: handle shlibs somehow
	 */
	if (mirror)
		return (EPKG_OK);

	/* Go through all rdeps */
	d = NULL;
	while (pkg_rdeps(pkg, &d) == EPKG_OK) {
		/* XXX: this assumption can be applied only for the current plain dependencies */
		HASH_FIND_STR(j->universe, d->uid, unit);
		if (unit != NULL)
			continue;

		npkg = pkg_jobs_get_local_pkg(j, d->uid, 0);
		if (npkg != NULL) {
			/* Do not bother about remote deps */
			if (pkg_jobs_add_universe(j, npkg, recursive, false, NULL) != EPKG_OK)
				continue;
		}
	}

	if (!IS_DELETE(j)) {
		/* Examine conflicts */
		while (pkg_conflicts(pkg, &c) == EPKG_OK) {
			/* XXX: this assumption can be applied only for the current plain dependencies */
			HASH_FIND_STR(j->universe, pkg_conflict_uniqueid(c), unit);
			if (unit != NULL)
				continue;

			/* Check local and remote conflicts */
			if (pkg->type == PKG_INSTALLED) {
				/* Installed packages can conflict with remote ones */
				npkg = pkg_jobs_get_remote_pkg(j, pkg_conflict_uniqueid(c), 0);
				if (npkg == NULL)
					continue;

				if (pkg_jobs_add_universe(j, npkg, recursive, false, NULL) != EPKG_OK)
					continue;
			}
			else {
				/* Remote packages can conflict with remote and local */
				npkg = pkg_jobs_get_local_pkg(j, pkg_conflict_uniqueid(c), 0);
				if (npkg == NULL)
					continue;

				if (pkg_jobs_add_universe(j, npkg, recursive, false, NULL) != EPKG_OK)
					continue;

				if (c->type != PKG_CONFLICT_REMOTE_LOCAL) {
					npkg = pkg_jobs_get_remote_pkg(j, pkg_conflict_uniqueid(c), 0);
					if (npkg == NULL)
						continue;

					if (pkg_jobs_add_universe(j, npkg, recursive, false, NULL)
							!= EPKG_OK)
						continue;
				}
			}
		}
	}

	/* For remote packages we should also handle shlib deps */
	if (pkg->type != PKG_INSTALLED && !IS_DELETE(j)) {
		while (pkg_shlibs_required(pkg, &shlib) == EPKG_OK) {
			HASH_FIND_STR(j->provides, pkg_shlib_name(shlib), pr);
			if (pr != NULL)
				continue;

			/* Not found, search in the repos */
			it = pkgdb_repo_shlib_provide(j->db, pkg_shlib_name(shlib), j->reponame);
			if (it != NULL) {
				rpkg = NULL;
				prhead = NULL;
				while (pkgdb_it_next(it, &rpkg, flags) == EPKG_OK) {
					pkg_get(rpkg, PKG_DIGEST, &digest, PKG_UNIQUEID, &uid);
					/* Check for local packages */
					HASH_FIND_STR(j->universe, uid, unit);
					if (unit != NULL) {
						if (pkg_need_upgrade (rpkg, unit->pkg, false)) {
							/* Remote provide is newer, so we can add it */
							if (pkg_jobs_add_universe(j, rpkg, recursive, false,
																&unit) != EPKG_OK)
								continue;

							rpkg = NULL;
						}
					}
					else {
						/* Maybe local package has just been not added */
						npkg = pkg_jobs_get_local_pkg(j, uid, 0);
						if (npkg != NULL) {
							if (pkg_jobs_add_universe(j, npkg, recursive, false,
									&unit) != EPKG_OK)
								return (EPKG_FATAL);
							if (pkg_need_upgrade (rpkg, npkg, false)) {
								/* Remote provide is newer, so we can add it */
								if (pkg_jobs_add_universe(j, rpkg, recursive, false,
										&unit) != EPKG_OK)
									continue;
							}
						}
					}
					/* Skip seen packages */
					if (unit == NULL) {
						if (digest == NULL) {
							pkg_debug(3, "no digest found for package %s", uid);
							if (pkg_checksum_calculate(pkg, j->db) != EPKG_OK) {
								return (EPKG_FATAL);
							}
							pkg_get(pkg, PKG_DIGEST, &digest);
						}
						HASH_FIND_STR(j->seen, digest, seen);
						if (seen == NULL) {
							pkg_jobs_add_universe(j, rpkg, recursive, false,
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
						HASH_ADD_KEYPTR(hh, j->provides, pr->provide,
								strlen(pr->provide), prhead);
					}
					else {
						DL_APPEND(prhead, pr);
					}
				}
				pkgdb_it_free(it);
				if (prhead == NULL) {
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
	}

	return (EPKG_OK);
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
								pkg_jobs_update_universe_item_priority(j, cur,
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
