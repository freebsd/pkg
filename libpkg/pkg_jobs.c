/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2013-2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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

static int find_remote_pkg(struct pkg_jobs *j, const char *pattern, match_t m,
		bool root, bool recursive, bool add_request);
static struct pkg *get_local_pkg(struct pkg_jobs *j, const char *origin, unsigned flag);
static struct pkg *get_remote_pkg(struct pkg_jobs *j, const char *origin, unsigned flag);
static int pkg_jobs_fetch(struct pkg_jobs *j);
static bool newer_than_local_pkg(struct pkg_jobs *j, struct pkg *rp, bool force);
static bool pkg_need_upgrade(struct pkg *rp, struct pkg *lp, bool recursive);
static bool new_pkg_version(struct pkg_jobs *j);
static int pkg_jobs_check_conflicts(struct pkg_jobs *j);

int
pkg_jobs_new(struct pkg_jobs **j, pkg_jobs_t t, struct pkgdb *db)
{
	assert(db != NULL);

	if ((*j = calloc(1, sizeof(struct pkg_jobs))) == NULL) {
		pkg_emit_errno("calloc", "pkg_jobs");
		return (EPKG_FATAL);
	}

	(*j)->db = db;
	(*j)->type = t;
	(*j)->solved = 0;
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

static void
pkg_jobs_pattern_free(struct job_pattern *jp)
{
	if (jp->pattern != NULL)
		free(jp->pattern);
	if (jp->path != NULL)
		free(jp->path);

	free(jp);
}

static void
pkg_jobs_provide_free(struct pkg_job_provide *pr)
{
	struct pkg_job_provide *cur, *tmp;

	DL_FOREACH_SAFE(pr, cur, tmp) {
		free (cur);
	}
}

void
pkg_jobs_free(struct pkg_jobs *j)
{
	struct pkg_job_request *req, *tmp;
	struct pkg_job_universe_item *un, *untmp, *cur, *curtmp;

	if (j == NULL)
		return;

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
		LL_FOREACH_SAFE(un, cur, curtmp) {
			pkg_free(cur->pkg);
			free(cur);
		}
	}
	HASH_FREE(j->seen, free);
	HASH_FREE(j->patterns, pkg_jobs_pattern_free);
	HASH_FREE(j->provides, pkg_jobs_provide_free);
	LL_FREE(j->jobs, free);

	free(j);
}

static bool
pkg_jobs_maybe_match_file(struct job_pattern *jp, const char *pattern)
{
	const char *dot_pos;
	char *pkg_path;

	assert(jp != NULL);
	assert(pattern != NULL);

	dot_pos = strrchr(pattern, '.');
	if (dot_pos != NULL) {
		/*
		 * Compare suffix with .txz or .tbz
		 */
		dot_pos ++;
		if (strcmp(dot_pos, "txz") == 0 ||
			strcmp(dot_pos, "tbz") == 0 ||
			strcmp(dot_pos, "tgz") == 0 ||
			strcmp(dot_pos, "tar") == 0) {
			if ((pkg_path = realpath(pattern, NULL)) != NULL) {
				/* Dot pos is one character after the dot */
				int len = dot_pos - pattern;

				jp->is_file = true;
				jp->path = pkg_path;
				jp->pattern = malloc(len);
				strlcpy(jp->pattern, pattern, len);

				return (true);
			}
		}
	}
	else if (strcmp(pattern, "-") == 0) {
		/*
		 * Read package from stdin
		 */
		jp->is_file = true;
		jp->path = strdup(pattern);
		jp->pattern = strdup(pattern);
	}

	return (false);
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
		jp = calloc(1, sizeof(struct job_pattern));
		if (!pkg_jobs_maybe_match_file(jp, argv[i])) {
			jp->pattern = strdup(argv[i]);
			jp->match = match;
		}
		HASH_ADD_KEYPTR(hh, j->patterns, jp->pattern, strlen(jp->pattern), jp);
	}

	if (argc == 0 && match == MATCH_ALL) {
		jp = calloc(1, sizeof(struct job_pattern));
		jp->pattern = NULL;
		jp->match = match;
		HASH_ADD_KEYPTR(hh, j->patterns, "all", 3, jp);
	}

	return (EPKG_OK);
}

bool
pkg_jobs_iter(struct pkg_jobs *jobs, void **iter,
				struct pkg **new, struct pkg **old,
				int *type)
{
	struct pkg_solved *s;
	assert(iter != NULL);
	if (jobs->jobs == NULL) {
		return (false);
	}
	if (*iter == NULL) {
		s = jobs->jobs;
	}
	else if (*iter == jobs->jobs) {
		return (false);
	}
	else {
		s = *iter;
	}
	*new = s->items[0]->pkg;
	*old = s->items[1] ? s->items[1]->pkg : NULL;
	*type = s->type;
	*iter = s->next ? s->next : jobs->jobs;
	return (true);
}

static void
pkg_jobs_add_req(struct pkg_jobs *j, const char *origin, struct pkg_job_universe_item *item,
		bool add)
{
	struct pkg_job_request *req, *test, **head;

	if (add)
		head = &j->request_add;
	else
		head = &j->request_delete;

	HASH_FIND(hh, *head, origin, strlen(origin), test);

	if (test != NULL)
		return;

	req = calloc(1, sizeof (struct pkg_job_request));
	if (req == NULL) {
		pkg_emit_errno("malloc", "struct pkg_job_request");
		return;
	}
	req->item = item;

	HASH_ADD_KEYPTR(hh, *head, origin, strlen(origin), req);
}

enum pkg_priority_update_type {
	PKG_PRIORITY_UPDATE_REQUEST = 0,
	PKG_PRIORITY_UPDATE_UNIVERSE,
	PKG_PRIORITY_UPDATE_CONFLICT,
	PKG_PRIORITY_UPDATE_DELETE
};

static void
pkg_jobs_update_universe_priority(struct pkg_jobs *j,
		struct pkg_job_universe_item *item, int priority,
		enum pkg_priority_update_type type)
{
	const char *origin, *digest;
	struct pkg_dep *d = NULL;
	struct pkg_conflict *c = NULL;
	struct pkg_job_universe_item *found, *cur, *it;
	const char *is_local;
	int maxpri;

	int (*deps_func)(const struct pkg *pkg, struct pkg_dep **d);
	int (*rdeps_func)(const struct pkg *pkg, struct pkg_dep **d);

	LL_FOREACH(item, it) {

		pkg_get(it->pkg, PKG_ORIGIN, &origin, PKG_DIGEST, &digest);
		if ((item->next != NULL || item->prev != NULL) &&
				it->pkg->type != PKG_INSTALLED &&
				(type == PKG_PRIORITY_UPDATE_CONFLICT ||
				 type == PKG_PRIORITY_UPDATE_DELETE)) {
			/*
			 * We do not update priority of a remote part of conflict, as we know
			 * that remote packages should not contain conflicts (they should be
			 * resolved in request prior to calling of this function)
			 */
			pkg_debug(4, "skip update priority for %s-%s", origin, digest);
			continue;
		}
		if (it->priority > priority)
			continue;

		is_local = it->pkg->type == PKG_INSTALLED ? "local" : "remote";
		pkg_debug(2, "universe: update %s priority of %s(%s): %d -> %d, reason: %d",
				is_local, origin, digest, it->priority, priority, type);
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
			HASH_FIND_STR(j->universe, pkg_dep_get(d, PKG_DEP_ORIGIN), found);
			if (found != NULL) {
				LL_FOREACH(found, cur) {
					if (cur->priority < priority + 1)
						pkg_jobs_update_universe_priority(j, cur,
								priority + 1, type);
				}
			}
		}

		d = NULL;
		maxpri = priority;
		while (rdeps_func(it->pkg, &d) == EPKG_OK) {
			HASH_FIND_STR(j->universe, pkg_dep_get(d, PKG_DEP_ORIGIN), found);
			if (found != NULL) {
				LL_FOREACH(found, cur) {
					if (cur->priority >= maxpri) {
						maxpri = cur->priority + 1;
					}
				}
			}
		}
		if (maxpri != priority) {
			pkg_jobs_update_universe_priority(j, it,
					maxpri, type);
			return;
		}
		if (it->pkg->type != PKG_INSTALLED) {
			while (pkg_conflicts(it->pkg, &c) == EPKG_OK) {
				HASH_FIND_STR(j->universe, pkg_conflict_origin(c), found);
				if (found != NULL) {
					LL_FOREACH(found, cur) {
						if (cur->pkg->type == PKG_INSTALLED) {
							/*
							 * Move delete requests to be done before installing
							 */
							if (cur->priority <= it->priority)
								pkg_jobs_update_universe_priority(j, cur,
									it->priority + 1, PKG_PRIORITY_UPDATE_CONFLICT);
						}
					}
				}
			}
		}
	}
}

static void
pkg_jobs_update_conflict_priority(struct pkg_jobs *j, struct pkg_solved *req)
{
	struct pkg_conflict *c = NULL;
	struct pkg *lp = req->items[1]->pkg;
	struct pkg_job_universe_item *found, *cur, *rit = NULL;

	while (pkg_conflicts(lp, &c) == EPKG_OK) {
		rit = NULL;
		HASH_FIND_STR(j->universe, pkg_conflict_origin(c), found);
		assert(found != NULL);

		LL_FOREACH(found, cur) {
			if (cur->pkg->type != PKG_INSTALLED) {
				rit = cur;
				break;
			}
		}

		assert(rit != NULL);
		if (rit->priority >= req->items[1]->priority) {
			pkg_jobs_update_universe_priority(j, req->items[1],
					rit->priority + 1, PKG_PRIORITY_UPDATE_CONFLICT);
			/*
			 * Update priorities for a remote part as well
			 */
			pkg_jobs_update_universe_priority(j, req->items[0],
					req->items[0]->priority, PKG_PRIORITY_UPDATE_REQUEST);
		}
	}
}

static int
pkg_jobs_set_request_priority(struct pkg_jobs *j, struct pkg_solved *req)
{
	struct pkg_solved *treq;
	const char *origin;

	if (req->type == PKG_SOLVED_UPGRADE
			&& req->items[1]->pkg->conflicts != NULL) {
		/*
		 * We have an upgrade request that has some conflicting packages, therefore
		 * update priorities of local packages and try to update priorities of remote ones
		 */
		if (req->items[0]->priority == 0)
			pkg_jobs_update_conflict_priority(j, req);

		if (req->items[1]->priority > req->items[0]->priority &&
				!req->already_deleted) {
			/*
			 * Split conflicting upgrade request into delete -> upgrade request
			 */
			treq = calloc(1, sizeof(struct pkg_solved));
			if (treq == NULL) {
				pkg_emit_errno("calloc", "pkg_solved");
				return (EPKG_FATAL);
			}

			treq->type = PKG_SOLVED_UPGRADE_REMOVE;
			treq->items[0] = req->items[1];
			DL_APPEND(j->jobs, treq);
			req->already_deleted = true;
			pkg_get(treq->items[0]->pkg, PKG_ORIGIN, &origin);
			pkg_debug(2, "split upgrade request for %s", origin);
			return (EPKG_CONFLICT);
		}
	}
	else if (req->type == PKG_SOLVED_DELETE) {
		if (req->items[0]->priority == 0)
			pkg_jobs_update_universe_priority(j, req->items[0], 0,
					PKG_PRIORITY_UPDATE_DELETE);
	}
	else {
		if (req->items[0]->priority == 0)
			pkg_jobs_update_universe_priority(j, req->items[0], 0,
					PKG_PRIORITY_UPDATE_REQUEST);
	}

	return (EPKG_OK);
}

static int
pkg_jobs_sort_priority(struct pkg_solved *r1, struct pkg_solved *r2)
{
	if (r1->items[0]->priority == r2->items[0]->priority) {
		if (r1->type == PKG_SOLVED_DELETE && r2->type != PKG_SOLVED_DELETE)
			return (-1);
		else if (r2->type == PKG_SOLVED_DELETE && r1->type != PKG_SOLVED_DELETE)
			return (1);

		return (0);
	}
	return (r2->items[0]->priority - r1->items[0]->priority);
}

static void
pkg_jobs_set_priorities(struct pkg_jobs *j)
{
	struct pkg_solved *req;

iter_again:
	LL_FOREACH(j->jobs, req) {
		req->items[0]->priority = 0;
		if (req->items[1] != NULL)
			req->items[1]->priority = 0;
	}
	LL_FOREACH(j->jobs, req) {
		if (pkg_jobs_set_request_priority(j, req) == EPKG_CONFLICT)
			goto iter_again;
	}

	DL_SORT(j->jobs, pkg_jobs_sort_priority);
}

#undef PRIORITY_CAN_UPDATE

/*
 * Calculate manifest for packages that lack it
 */
static int
pkg_jobs_digest_manifest(struct pkg *pkg)
{
	char *new_digest;
	int rc;
	struct sbuf *sb;

	/* We need to calculate digest of this package */
	sb = sbuf_new_auto();
	rc = pkg_emit_manifest_sbuf(pkg, sb, PKG_MANIFEST_EMIT_COMPACT, &new_digest);

	if (rc == EPKG_OK) {
		pkg_set(pkg, PKG_DIGEST, new_digest);
		free(new_digest);
	}

	sbuf_delete(sb);

	return (rc);
}

/**
 * Check whether a package is in the universe already or add it
 * @return item or NULL
 */
static int
pkg_jobs_handle_pkg_universe(struct pkg_jobs *j, struct pkg *pkg,
		struct pkg_job_universe_item **found)
{
	struct pkg_job_universe_item *item, *cur, *tmp = NULL;
	const char *origin, *digest, *version, *name;
	struct pkg_job_seen *seen;

	pkg_get(pkg, PKG_ORIGIN, &origin, PKG_DIGEST, &digest,
			PKG_VERSION, &version, PKG_NAME, &name);
	if (digest == NULL) {
		if (pkg_jobs_digest_manifest(pkg) != EPKG_OK) {
			*found = NULL;
			return (EPKG_FATAL);
		}
		pkg_get(pkg, PKG_DIGEST, &digest);
	}

	HASH_FIND_STR(j->seen, digest, seen);
	if (seen != NULL) {
		cur = seen->un;
		if (found != NULL)
			*found = seen->un;

		return (EPKG_END);
	}

	pkg_debug(2, "universe: add new %s pkg: %s, (%s-%s)",
				(pkg->type == PKG_INSTALLED ? "local" : "remote"), origin,
				name, version);

	item = calloc(1, sizeof (struct pkg_job_universe_item));
	if (item == NULL) {
		pkg_emit_errno("pkg_jobs_pkg_insert_universe", "calloc: struct pkg_job_universe_item");
		return (EPKG_FATAL);
	}

	item->pkg = pkg;

	HASH_FIND_STR(j->universe, origin, tmp);
	if (tmp == NULL) {
		HASH_ADD_KEYPTR(hh, j->universe, origin, strlen(origin), item);
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
	bool automatic = false;
	const char *origin, *digest;
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

	/* Go through all depends */
	while (pkg_deps(pkg, &d) == EPKG_OK) {
		/* XXX: this assumption can be applied only for the current plain dependencies */
		HASH_FIND_STR(j->universe, pkg_dep_get(d, PKG_DEP_ORIGIN), unit);
		if (unit != NULL) {
			continue;
		}

		rpkg = NULL;
		npkg = get_local_pkg(j, pkg_dep_get(d, PKG_DEP_ORIGIN), 0);
		if (npkg == NULL) {
			/*
			 * We have a package installed, but its dependencies are not,
			 * try to search a remote dependency
			 */
			pkg_get(pkg, PKG_ORIGIN, &origin);
			pkg_debug(1, "dependency %s of local package %s is not installed",
					pkg_dep_get(d, PKG_DEP_ORIGIN), origin);
			npkg = get_remote_pkg(j, pkg_dep_get(d, PKG_DEP_ORIGIN), 0);
			if (npkg == NULL) {
				/* Cannot continue */
				pkg_emit_error("Missing dependency matching '%s'", pkg_dep_get(d, PKG_DEP_ORIGIN));
				if ((j->flags & PKG_FLAG_FORCE_MISSING) == PKG_FLAG_FORCE_MISSING) {
					continue;
				}
				return (EPKG_FATAL);
			}
			/* Set automatic if no local package is found */
			pkg_set(npkg, PKG_AUTOMATIC, (int64_t)true);
		}
		else if ((j->type == PKG_JOBS_UPGRADE ||
				j->type == PKG_JOBS_INSTALL) &&
				npkg->type == PKG_INSTALLED) {
			/* For upgrade jobs we need to ensure that we do not have a newer version */
			rpkg = get_remote_pkg(j, pkg_dep_get(d, PKG_DEP_ORIGIN), 0);
			if (rpkg != NULL) {
				if (!pkg_need_upgrade(rpkg, npkg, false)) {
					pkg_free(rpkg);
					rpkg = NULL;
				}
			}
		}

		if (pkg_jobs_add_universe(j, npkg, recursive, false, NULL) != EPKG_OK)
			return (EPKG_FATAL);
		if (rpkg != NULL) {
			/* Save automatic flag */
			pkg_get(npkg, PKG_AUTOMATIC, &automatic);
			pkg_set(rpkg, PKG_AUTOMATIC, (int64_t)automatic);

			if (pkg_jobs_add_universe(j, rpkg, recursive, false, NULL) != EPKG_OK)
				return (EPKG_FATAL);
		}
	}

	/* Go through all rdeps */
	d = NULL;
	while (pkg_rdeps(pkg, &d) == EPKG_OK) {
		/* XXX: this assumption can be applied only for the current plain dependencies */
		HASH_FIND_STR(j->universe, pkg_dep_get(d, PKG_DEP_ORIGIN), unit);
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
				if ((j->flags & PKG_FLAG_FORCE_MISSING) == PKG_FLAG_FORCE_MISSING) {
					continue;
				}
				return (EPKG_FATAL);
			}
		}
		if (pkg_jobs_add_universe(j, npkg, recursive, false, NULL) != EPKG_OK)
			return (EPKG_FATAL);
	}

	/* Examine conflicts */
	while (pkg_conflicts(pkg, &c) == EPKG_OK) {
		/* XXX: this assumption can be applied only for the current plain dependencies */
		HASH_FIND_STR(j->universe, pkg_conflict_origin(c), unit);
		if (unit != NULL)
			continue;

		/* Check local and remote conflicts */
		if (pkg->type == PKG_INSTALLED) {
			/* Installed packages can conflict with remote ones */
			npkg = get_remote_pkg(j, pkg_conflict_origin(c), 0);
			if (npkg == NULL)
				continue;

			if (pkg_jobs_add_universe(j, npkg, recursive, false, NULL) != EPKG_OK)
				return (EPKG_FATAL);
		}
		else {
			/* Remote packages can conflict with remote and local */
			npkg = get_local_pkg(j, pkg_conflict_origin(c), 0);
			if (npkg == NULL)
				continue;

			if (pkg_jobs_add_universe(j, npkg, recursive, false, NULL) != EPKG_OK)
				return (EPKG_FATAL);

			if (c->type != PKG_CONFLICT_REMOTE_LOCAL) {
				npkg = get_remote_pkg(j, pkg_conflict_origin(c), 0);
				if (npkg == NULL)
					continue;

				if (pkg_jobs_add_universe(j, npkg, recursive, false, NULL) != EPKG_OK)
					return (EPKG_FATAL);
			}
		}
	}

	/* For remote packages we should also handle shlib deps */
	if (pkg->type != PKG_INSTALLED) {
		while (pkg_shlibs_required(pkg, &shlib) == EPKG_OK) {
			HASH_FIND_STR(j->provides, pkg_shlib_name(shlib), pr);
			if (pr != NULL)
				continue;

			/* Not found, search in the repos */
			it = pkgdb_find_shlib_provide(j->db, pkg_shlib_name(shlib), j->reponame);
			if (it != NULL) {
				rpkg = NULL;
				prhead = NULL;
				while (pkgdb_it_next(it, &rpkg, flags) == EPKG_OK) {
					pkg_get(rpkg, PKG_DIGEST, &digest, PKG_ORIGIN, &origin);
					/* Check for local packages */
					HASH_FIND_STR(j->universe, origin, unit);
					if (unit != NULL) {
						if (pkg_need_upgrade (rpkg, unit->pkg, false)) {
							/* Remote provide is newer, so we can add it */
							if (pkg_jobs_add_universe(j, rpkg, recursive, false,
																&unit) != EPKG_OK)
								return (EPKG_FATAL);
						}
					}
					else {
						/* Maybe local package has just been not added */
						npkg = get_local_pkg(j, origin, 0);
						if (npkg != NULL) {
							if (pkg_jobs_add_universe(j, npkg, recursive, false,
									&unit) != EPKG_OK)
								return (EPKG_FATAL);
							if (pkg_need_upgrade (rpkg, npkg, false)) {
								/* Remote provide is newer, so we can add it */
								if (pkg_jobs_add_universe(j, rpkg, recursive, false,
										&unit) != EPKG_OK)
									return (EPKG_FATAL);
							}
						}
					}
					/* Skip seen packages */
					if (unit == NULL) {
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
					pkg_get(pkg, PKG_ORIGIN, &origin);
					pkg_debug(1, "cannot find packages that provide %s required for %s",
							pkg_shlib_name(shlib), origin);
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

/**
 * Test whether package specified is automatic with all its rdeps
 * @param j
 * @param p
 * @return
 */
static bool
pkg_jobs_test_automatic(struct pkg_jobs *j, struct pkg *p)
{
	struct pkg_dep *d = NULL;
	struct pkg_job_universe_item *unit;
	struct pkg *npkg;
	bool ret = true;

	while (pkg_rdeps(p, &d) == EPKG_OK && ret) {
		HASH_FIND_STR(j->universe, pkg_dep_get(d, PKG_DEP_ORIGIN), unit);
		if (unit != NULL) {
			if (!unit->pkg->automatic) {
				return (false);
			}
			npkg = unit->pkg;
		}
		else {
			npkg = get_local_pkg(j, pkg_dep_get(d, PKG_DEP_ORIGIN),
					PKG_LOAD_BASIC|PKG_LOAD_RDEPS);
			if (npkg == NULL)
				return (false);
			if (!npkg->automatic) {
				pkg_free(npkg);
				return (false);
			}
			if (pkg_jobs_add_universe(j, npkg, false, false, NULL) != EPKG_OK)
				return (false);
		}

		ret = pkg_jobs_test_automatic(j, npkg);
	}

	return (ret);
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

	pkg_jobs_add_universe(j, p, true, false, NULL);

	/* Use maximum priority for pkg */
	if (find_remote_pkg(j, origin, MATCH_EXACT, false, true, true) == EPKG_OK) {
		ret = true;
		goto end;
	}

end:
	j->flags = old_flags;

	return (ret);
}

static int
pkg_jobs_process_remote_pkg(struct pkg_jobs *j, struct pkg *p,
		bool root, bool force, bool recursive,
		struct pkg_job_universe_item **unit,
		bool add_request)
{
	struct pkg *p1;
	struct pkg_job_universe_item *jit;
	struct pkg_job_seen *seen;
	struct pkg_job_request *jreq;
	int rc = EPKG_FATAL;
	const char *origin, *digest;

	p1 = NULL;
	pkg_get(p, PKG_ORIGIN, &origin, PKG_DIGEST, &digest);

	if (digest == NULL) {
		if (pkg_jobs_digest_manifest(p) != EPKG_OK) {
			return (EPKG_FATAL);
		}
		pkg_get(p, PKG_DIGEST, &digest);
	}
	HASH_FIND_STR(j->seen, digest, seen);
	if (seen != NULL) {
		/* We have already added exactly the same package to the universe */
		pkg_debug(3, "already seen package %s-%s in the universe, do not add it again",
				origin, digest);
		/* However, we may want to add it to the job request */
		HASH_FIND_STR(j->request_add, origin, jreq);
		if (jreq == NULL)
			pkg_jobs_add_req(j, origin, seen->un, true);
		return (EPKG_OK);
	}
	HASH_FIND_STR(j->universe, origin, jit);
	if (jit != NULL) {
		/* We have a more recent package */
		if (!force && !pkg_need_upgrade(p, jit->pkg, false)) {
			/*
			 * We can have package from another repo in the
			 * universe, but if it is older than this one we just
			 * do not add it.
			 */
			if (jit->pkg->type == PKG_INSTALLED) {
				if (root)
					pkg_emit_already_installed(p);
				return (EPKG_INSTALLED);
			}
			else {
				pkg_debug(3, "already added newer package %s to the universe, do not add it again",
								origin);
				return (EPKG_OK);
			}
		}
		/*
		 * XXX: what happens if we have multirepo enabled and we add
		 * two packages from different repos?
		 */
	}
	else {
		if (j->type != PKG_JOBS_FETCH) {
			if (!newer_than_local_pkg(j, p, force)) {
				if (root)
					pkg_emit_already_installed(p);
				return (EPKG_INSTALLED);
			}
		}
	}

	rc = EPKG_OK;
	p->direct = root;
	/* Add a package to request chain and populate universe */
	rc = pkg_jobs_add_universe(j, p, recursive, false, &jit);
	if (add_request)
		pkg_jobs_add_req(j, origin, jit, true);

	if (unit != NULL)
		*unit = jit;

	return (rc);
}

static int
find_remote_pkg(struct pkg_jobs *j, const char *pattern,
		match_t m, bool root, bool recursive, bool add_request)
{
	struct pkg *p = NULL;
	struct pkgdb_it *it;
	bool force = false;
	int rc = EPKG_FATAL;
	unsigned flags = PKG_LOAD_BASIC|PKG_LOAD_OPTIONS|PKG_LOAD_DEPS|
			PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|
			PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;

	if (root && (j->flags & PKG_FLAG_FORCE) == PKG_FLAG_FORCE)
		force = true;

	if (((j->flags & PKG_FLAG_FORCE) == PKG_FLAG_FORCE) &&
	    ((j->flags & PKG_FLAG_RECURSIVE) == PKG_FLAG_RECURSIVE))
	    force = true;

	if (j->type == PKG_JOBS_UPGRADE && (j->flags & PKG_FLAG_FORCE) == PKG_FLAG_FORCE)
		force = true;

	if ((it = pkgdb_rquery(j->db, pattern, m, j->reponame)) == NULL)
		return (rc);

	while (pkgdb_it_next(it, &p, flags) == EPKG_OK) {
		rc = pkg_jobs_process_remote_pkg(j, p, root, force, recursive,
				NULL, add_request);
		if (rc == EPKG_FATAL)
			break;
		p = NULL;
	}

	pkgdb_it_free(it);

	return (rc);
}

static int
pkg_jobs_find_remote_pattern(struct pkg_jobs *j, struct job_pattern *jp,
		bool *got_local)
{
	int rc = EPKG_FATAL;
	struct pkg *pkg = NULL;
	struct pkg_manifest_key *keys = NULL;
	struct pkg_job_universe_item *unit;

	if (!jp->is_file) {
		rc = find_remote_pkg(j, jp->pattern, jp->match, true, true, true);
	}
	else {
		pkg_manifest_keys_new(&keys);
		if (pkg_open(&pkg, jp->path, keys, PKG_OPEN_MANIFEST_ONLY) != EPKG_OK)
			rc = EPKG_FATAL;
		else {
			pkg->type = PKG_FILE;
			rc = pkg_jobs_process_remote_pkg(j, pkg, true,
					j->flags & PKG_FLAG_FORCE, false, &unit, true);
			unit->jp = jp;
			*got_local = true;
		}
		pkg_manifest_keys_free(keys);
	}

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
				PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|
				PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;
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
	const char *lversion, *rversion, *larch, *rarch;
	struct pkg_option *lo = NULL, *ro = NULL;
	struct pkg_dep *ld = NULL, *rd = NULL;
	struct pkg_shlib *ls = NULL, *rs = NULL;
	struct pkg_conflict *lc = NULL, *rc = NULL;
	struct pkg_provide *lpr = NULL, *rpr = NULL;

	/* Do not upgrade locked packages */
	if (pkg_is_locked(lp))
		return (false);

	pkg_get(lp, PKG_VERSION, &lversion, PKG_ARCH, &larch);
	pkg_get(rp, PKG_VERSION, &rversion, PKG_ARCH, &rarch);

	ret = pkg_version_cmp(lversion, rversion);
	if (ret > 0)
		return (false);
	else if (ret < 0)
		return (true);

	/* Compare archs */
	if (strcmp (larch, rarch) != 0) {
		pkg_set(rp, PKG_REASON, "ABI changed");
		return (true);
	}

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
	ucl_object_t *an;
	int64_t oldsize;
	struct pkg *lp;
	bool automatic;
	int ret;

	pkg_get(rp, PKG_ORIGIN, &origin,
	    PKG_REPONAME, &reponame);
	lp = get_local_pkg(j, origin, 0);

	/* obviously yes because local doesn't exists */
	if (lp == NULL)
		return (true);

	pkg_jobs_add_universe(j, lp, true, false, NULL);
	pkg_get(lp, PKG_AUTOMATIC, &automatic,
	    PKG_VERSION, &oldversion,
	    PKG_FLATSIZE, &oldsize);

	/* Add repo name to the annotation */
	an = pkg_annotation_lookup(lp, "repository");
	if (an != NULL)  {
		if (strcmp(pkg_repo_ident(pkg_repo_find_name(reponame)),
		    ucl_object_tostring(an)) != 0)  {
			return (false);
		} else {
			pkg_addannotation(rp, "repository", ucl_object_tostring(an));
		}
	}

	pkg_get(rp, PKG_VERSION, &newversion);
	pkg_set(rp, PKG_OLD_VERSION, oldversion,
	    PKG_OLD_FLATSIZE, oldsize,
	    PKG_AUTOMATIC, (int64_t)automatic);

	if (force)
		return (true);

	ret = pkg_need_upgrade(rp, lp, false);

	return (ret);
}

static int
pkg_conflicts_add_missing(struct pkg_jobs *j, const char *origin)
{
	struct pkg *npkg;


	npkg = get_local_pkg(j, origin, 0);
	if (npkg == NULL) {
		npkg = get_remote_pkg(j, origin, 0);
		pkg_debug(2, "conflicts: add missing remote origin %s(%d)", origin);
	}
	else {
		pkg_debug(2, "conflicts: add missing local origin %s(%d)", origin);
	}

	if (npkg == NULL) {
		pkg_emit_error("cannot register conflict with non-existing origin %s",
				origin);
		return (EPKG_FATAL);
	}

	return pkg_jobs_add_universe(j, npkg, true, false, NULL);
}


static void
pkg_conflicts_register_universe(struct pkg_jobs *j,
		struct pkg_job_universe_item *u1,
		struct pkg_job_universe_item *u2, bool local_only,
		enum pkg_conflict_type type)
{

	pkg_conflicts_register(u1->pkg, u2->pkg, type);
}

static void
pkg_conflicts_add_from_pkgdb_local(const char *o1, const char *o2, void *ud)
{
	struct pkg_jobs *j = (struct pkg_jobs *)ud;
	struct pkg_job_universe_item *u1, *u2, *cur1, *cur2;
	struct pkg_conflict *c;
	const char *dig1, *dig2;

	HASH_FIND_STR(j->universe, o1, u1);
	HASH_FIND_STR(j->universe, o2, u2);

	if (u1 == NULL && u2 == NULL) {
		pkg_emit_error("cannot register conflict with non-existing origins %s and %s",
				o1, o2);
		return;
	}
	else if (u1 == NULL) {
		if (pkg_conflicts_add_missing(j, o1) != EPKG_OK)
			return;
		HASH_FIND_STR(j->universe, o1, u1);
	}
	else if (u2 == NULL) {
		if (pkg_conflicts_add_missing(j, o2) != EPKG_OK)
			return;
		HASH_FIND_STR(j->universe, o2, u2);
	}
	else {
		/* Maybe we have registered this conflict already */
		HASH_FIND(hh, u1->pkg->conflicts, o2, strlen(o2), c);
		if (c != NULL)
			return;
	}

	/*
	 * Here we have some unit but we do not know, where is a conflict, e.g.
	 * if we have several units U1 and U2 with the same origin O that are in
	 * the conflict with some origin O' provided by U1' and U2'. So we can
	 * register the conflicts between all units in the chain.
	 */
	LL_FOREACH(u1, cur1) {
		LL_FOREACH(u2, cur2) {
			if (cur1->pkg->type == PKG_INSTALLED && cur2->pkg->type != PKG_INSTALLED) {
				pkg_get(cur1->pkg, PKG_DIGEST, &dig1);
				pkg_get(cur2->pkg, PKG_DIGEST, &dig2);
				pkg_conflicts_register_universe(j, cur1, cur2, true, PKG_CONFLICT_REMOTE_LOCAL);
				pkg_debug(2, "register conflict between local %s(%s) <-> remote %s(%s)",
						o1, dig1, o2, dig2);
				j->conflicts_registered ++;
			}
			else if (cur2->pkg->type == PKG_INSTALLED && cur1->pkg->type != PKG_INSTALLED) {
				pkg_get(cur1->pkg, PKG_DIGEST, &dig1);
				pkg_get(cur2->pkg, PKG_DIGEST, &dig2);
				pkg_conflicts_register_universe(j, cur1, cur2, true, PKG_CONFLICT_REMOTE_LOCAL);
				pkg_debug(2, "register conflict between local %s(%s) <-> remote %s(%s)",
						o2, dig2, o1, dig1);
				j->conflicts_registered ++;
			}
		}
	}
}

static void
pkg_conflicts_add_from_pkgdb_remote(const char *o1, const char *o2, void *ud)
{
	struct pkg_jobs *j = (struct pkg_jobs *)ud;
	struct pkg_job_universe_item *u1, *u2, *cur1, *cur2;
	struct pkg_conflict *c;
	const char *dig1, *dig2;

	HASH_FIND_STR(j->universe, o1, u1);
	HASH_FIND_STR(j->universe, o2, u2);

	/*
	 * In case of remote conflict we need to register it only between remote
	 * packets
	 */

	if (u1 == NULL || u2 == NULL) {
		pkg_emit_error("cannot register remote conflict with non-existing origins %s and %s",
				o1, o2);
		return;
	}
	else {
		/* Maybe we have registered this conflict already */
		HASH_FIND(hh, u1->pkg->conflicts, o2, strlen(o2), c);
		if (c != NULL)
			return;
	}

	LL_FOREACH(u1, cur1) {
		if (cur1->pkg->type != PKG_INSTALLED) {
			HASH_FIND(hh, cur1->pkg->conflicts, o2, strlen(o2), c);
			if (c == NULL) {
				LL_FOREACH(u2, cur2) {
					HASH_FIND(hh, cur2->pkg->conflicts, o1, strlen(o1), c);
					if (c == NULL && cur2->pkg->type != PKG_INSTALLED) {
						/* No need to update priorities */
						pkg_conflicts_register(cur1->pkg, cur2->pkg, PKG_CONFLICT_REMOTE_REMOTE);
						j->conflicts_registered ++;
						pkg_get(cur1->pkg, PKG_DIGEST, &dig1);
						pkg_get(cur2->pkg, PKG_DIGEST, &dig2);
						pkg_debug(2, "register conflict between remote %s(%s) <-> %s(%s)",
								o1, dig1, o2, dig2);
						break;
					}
				}
			}
		}
	}
}

int
pkg_conflicts_append_pkg(struct pkg *p, struct pkg_jobs *j)
{
	/* Now we can get conflicts only from pkgdb */
	return (pkgdb_integrity_append(j->db, p, pkg_conflicts_add_from_pkgdb_remote, j));
}

int
pkg_conflicts_integrity_check(struct pkg_jobs *j)
{
	return (pkgdb_integrity_check(j->db, pkg_conflicts_add_from_pkgdb_local, j));
}

static struct pkg_job_request *
pkg_jobs_find_deinstall_request(struct pkg_job_universe_item *item, struct pkg_jobs *j)
{
	struct pkg_job_request *found;
	struct pkg_job_universe_item *dep_item;
	struct pkg_dep *d = NULL;
	const char *origin;
	struct pkg *pkg = item->pkg;

	pkg_get(pkg, PKG_ORIGIN, &origin);

	HASH_FIND_STR(j->request_delete, origin, found);
	if (found == NULL) {
		while (pkg_deps(pkg, &d) == EPKG_OK) {
			HASH_FIND_STR(j->universe, pkg_dep_get(d, PKG_DEP_ORIGIN), dep_item);
			if (dep_item) {
				found = pkg_jobs_find_deinstall_request(dep_item, j);
				if (found)
					return (found);
			}
		}
	}
	else {
		return (found);
	}

	return (NULL);
}

static void
pkg_jobs_set_deinstall_reasons(struct pkg_jobs *j)
{
	struct sbuf *reason = sbuf_new_auto();
	struct pkg_solved *sit;
	struct pkg_job_request *jreq;
	struct pkg *req_pkg, *pkg;
	const char *name, *version;

	LL_FOREACH(j->jobs, sit) {
		jreq = pkg_jobs_find_deinstall_request(sit->items[0], j);
		if (jreq->item != sit->items[0]) {
			req_pkg = jreq->item->pkg;
			pkg = sit->items[0]->pkg;
			/* Set the reason */
			pkg_get(req_pkg, PKG_NAME, &name, PKG_VERSION, &version);
			sbuf_printf(reason, "depends on %s-%s", name, version);
			sbuf_finish(reason);

			pkg_set(pkg, PKG_REASON, sbuf_data(reason));
			sbuf_clear(reason);
		}
	}

	sbuf_delete(reason);
}

static int
jobs_solve_deinstall(struct pkg_jobs *j)
{
	struct job_pattern *jp, *jtmp;
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	char *origin;
	struct pkg_job_universe_item *unit;

	bool recursive = false;

	if ((j->flags & PKG_FLAG_RECURSIVE) == PKG_FLAG_RECURSIVE)
		recursive = true;

	HASH_ITER(hh, j->patterns, jp, jtmp) {
		if ((it = pkgdb_query(j->db, jp->pattern, jp->match)) == NULL)
			return (EPKG_FATAL);

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_RDEPS) == EPKG_OK) {
			// Check if the pkg is locked
			pkg_get(pkg, PKG_ORIGIN, &origin);
			pkg_jobs_add_universe(j, pkg, recursive, false, &unit);
			if(pkg_is_locked(pkg)) {
				pkg_emit_locked(pkg);
			}
			else {
				pkg_get(pkg, PKG_ORIGIN, &origin);
				pkg_jobs_add_req(j, origin, unit, false);
			}
			/* TODO: use repository priority here */

			pkg = NULL;
		}
		pkgdb_it_free(it);
	}

	j->solved = 1;

	return( EPKG_OK);
}

static int
jobs_solve_autoremove(struct pkg_jobs *j)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	char *origin;
	struct pkg_job_universe_item *unit;

	if ((it = pkgdb_query(j->db, " WHERE automatic=1 ", MATCH_CONDITION)) == NULL)
		return (EPKG_FATAL);

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_RDEPS) == EPKG_OK) {
		// Check if the pkg is locked
		pkg_get(pkg, PKG_ORIGIN, &origin);
		HASH_FIND_STR(j->universe, origin, unit);
		if (unit == NULL) {
			pkg_jobs_add_universe(j, pkg, false, false, &unit);
			if(pkg_is_locked(pkg)) {
				pkg_emit_locked(pkg);
			}
			else if (pkg_jobs_test_automatic(j, pkg)) {
				pkg_debug(2, "removing %s as it has no non-automatic reverse depends",
						origin);
				pkg_jobs_add_req(j, origin, unit, false);
			}
		}
		else {
			if(pkg_is_locked(unit->pkg)) {
				pkg_emit_locked(unit->pkg);
			}
			else if (pkg_jobs_test_automatic(j, unit->pkg)) {
				pkg_debug(2, "removing %s as it has no non-automatic reverse depends",
						origin);
				pkg_jobs_add_req(j, origin, unit, false);
			}

			pkg_free(pkg);
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
	bool automatic;
	unsigned flags = PKG_LOAD_BASIC|PKG_LOAD_OPTIONS|PKG_LOAD_DEPS|
			PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;

	if ((j->flags & PKG_FLAG_PKG_VERSION_TEST) == PKG_FLAG_PKG_VERSION_TEST)
		if (new_pkg_version(j)) {
			pkg_emit_newpkgversion();
			goto order;
		}

	if (j->solved == 0) {
		if ((it = pkgdb_query(j->db, NULL, MATCH_ALL)) == NULL)
			return (EPKG_FATAL);

		while (pkgdb_it_next(it, &pkg, flags) == EPKG_OK) {
			/* TODO: use repository priority here */
			pkg_jobs_add_universe(j, pkg, true, false, NULL);
			pkg_get(pkg, PKG_ORIGIN, &origin, PKG_AUTOMATIC, &automatic);
			/* Do not test we ignore what doesn't exists remotely */
			find_remote_pkg(j, origin, MATCH_EXACT, false, true, !automatic);
			pkg = NULL;
		}
		pkgdb_it_free(it);
	}
	else {
		/*
		 * If we have tried to solve request, then we just want to re-add all
		 * request packages to the universe to find out any potential conflicts
		 */
		struct pkg_job_request *req, *rtmp;
		HASH_ITER(hh, j->request_add, req, rtmp) {
			pkg_jobs_add_universe(j, req->item->pkg, true, false, NULL);
		}
	}

	if (pkg_conflicts_request_resolve(j) != EPKG_OK) {
		pkg_emit_error("Cannot resolve conflicts in a request");
		return (EPKG_FATAL);
	}

order:

	j->solved ++;

	return (EPKG_OK);
}

static int
jobs_solve_install(struct pkg_jobs *j)
{
	struct job_pattern *jp, *jtmp;
	struct pkg_job_request *req, *rtmp;
	bool got_local;

	if ((j->flags & PKG_FLAG_PKG_VERSION_TEST) == PKG_FLAG_PKG_VERSION_TEST) {
		if (new_pkg_version(j)) {
			pkg_emit_newpkgversion();
			goto order;
		}
	}

	if (j->solved == 0) {
		HASH_ITER(hh, j->patterns, jp, jtmp) {
			if (pkg_jobs_find_remote_pattern(j, jp, &got_local) == EPKG_FATAL) {
				pkg_emit_error("No packages matching '%s' have been found in the "
						"repositories", jp->pattern);
				return (EPKG_FATAL);
			}
		}
		if (got_local) {
			/*
			 * Need to iterate request one more time to recurse depends
			 */
			HASH_ITER(hh, j->request_add, req, rtmp) {
				pkg_jobs_add_universe(j, req->item->pkg, true, true, NULL);
			}
		}
	}
	else {
		/*
		 * If we have tried to solve request, then we just want to re-add all
		 * request packages to the universe to find out any potential conflicts
		 */
		HASH_ITER(hh, j->request_add, req, rtmp) {
			pkg_jobs_add_universe(j, req->item->pkg, true, false, NULL);
		}
	}

	if (pkg_conflicts_request_resolve(j) != EPKG_OK) {
		pkg_emit_error("Cannot resolve conflicts in a request");
		return (EPKG_FATAL);
	}

order:

	j->solved ++;

	return (EPKG_OK);
}

static int
jobs_solve_fetch(struct pkg_jobs *j)
{
	struct job_pattern *jp, *jtmp;
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
			if(pkg_is_locked(pkg)) {
				pkg_emit_locked(pkg);
			}
			else {
				pkg_get(pkg, PKG_ORIGIN, &origin);
				/* Do not test we ignore what doesn't exists remotely */
				find_remote_pkg(j, origin, MATCH_EXACT, false,
						j->flags & PKG_FLAG_RECURSIVE, true);
			}
			pkg = NULL;
		}
		pkgdb_it_free(it);
	} else {
		HASH_ITER(hh, j->patterns, jp, jtmp) {
			/* TODO: use repository priority here */
			if (find_remote_pkg(j, jp->pattern, jp->match, true,
					j->flags & PKG_FLAG_RECURSIVE, true) == EPKG_FATAL)
				pkg_emit_error("No packages matching '%s' have been found in the "
						"repositories", jp->pattern);
		}
	}

	j->solved ++;

	return (EPKG_OK);
}

int
pkg_jobs_solve(struct pkg_jobs *j)
{
	int ret, pstatus;
	struct pkg_solve_problem *problem;
	const char *solver;
	FILE *spipe[2];
	pid_t pchild;

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
		if ((solver = pkg_object_string(pkg_config_get("CUDF_SOLVER"))) != NULL) {
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
				if ((solver = pkg_object_string(pkg_config_get("SAT_SOLVER"))) != NULL) {
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
						j->solved = 0;
					}
					else {
						ret = pkg_solve_sat_to_jobs(problem, j);
					}
				}
			}
			else {
				pkg_emit_error("cannot convert job to SAT problem");
				ret = EPKG_FATAL;
				j->solved = 0;
			}
		}
	}

	if (j->type == PKG_JOBS_DEINSTALL && j->solved)
		pkg_jobs_set_deinstall_reasons(j);

	return (ret);
}

int
pkg_jobs_count(struct pkg_jobs *j)
{
	assert(j != NULL);

	return (j->count);
}

int
pkg_jobs_total(struct pkg_jobs *j)
{
	assert(j != NULL);

	return (j->total);
}

pkg_jobs_t
pkg_jobs_type(struct pkg_jobs *j)
{
	assert(j != NULL);

	return (j->type);
}

static int
pkg_jobs_handle_install(struct pkg_solved *ps, struct pkg_jobs *j, bool handle_rc,
		const char *cachedir, struct pkg_manifest_key *keys)
{
	struct pkg *new, *old;
	const char *pkgorigin, *oldversion = NULL;
	ucl_object_t *an;
	char path[MAXPATHLEN], *target;
	bool automatic = false;
	int flags = 0;
	int retcode = EPKG_FATAL;

	old = ps->items[1] ? ps->items[1]->pkg : NULL;
	new = ps->items[0]->pkg;

	pkg_get(new, PKG_ORIGIN, &pkgorigin);
	if (old != NULL)
		pkg_get(old, PKG_VERSION, &oldversion, PKG_AUTOMATIC, &automatic);
	else if (!new->direct)
		automatic = true;

	an = pkg_annotation_lookup(new, "repository");

	if (ps->items[0]->jp != NULL && ps->items[0]->jp->is_file) {
		/*
		 * We have package as a file
		 */
		target = ps->items[0]->jp->path;
	}
	else {
		pkg_snprintf(path, sizeof(path), "%R", new);
		if (*path != '/')
			pkg_snprintf(path, sizeof(path), "%S/%n-%v-%z",
					cachedir, new, new, new);
		target = path;
	}

	if (oldversion != NULL) {
		pkg_set(new, PKG_OLD_VERSION, oldversion);
		pkg_emit_upgrade_begin(new, old);
	} else {
		pkg_emit_install_begin(new);
	}

	if ((j->flags & PKG_FLAG_FORCE) == PKG_FLAG_FORCE)
		flags |= PKG_ADD_FORCE;
	if ((j->flags & PKG_FLAG_NOSCRIPT) == PKG_FLAG_NOSCRIPT)
		flags |= PKG_ADD_NOSCRIPT;
	if ((j->flags & PKG_FLAG_FORCE_MISSING) == PKG_FLAG_FORCE_MISSING)
		flags |= PKG_ADD_FORCE_MISSING;
	flags |= PKG_ADD_UPGRADE;
	if (automatic)
		flags |= PKG_ADD_AUTOMATIC;

	if (old != NULL && !ps->already_deleted) {
		if ((retcode = pkg_delete(old, j->db, PKG_DELETE_UPGRADE)) != EPKG_OK) {
			pkgdb_transaction_rollback(j->db->sqlite, "upgrade");
			goto cleanup;
		}
	}
	if ((retcode = pkg_add(j->db, target, flags, keys)) != EPKG_OK) {
		pkgdb_transaction_rollback(j->db->sqlite, "upgrade");
		goto cleanup;
	}

	if (an != NULL) {
		pkgdb_add_annotation(j->db, new, "repository", ucl_object_tostring(an));
	}

	if (oldversion != NULL)
		pkg_emit_upgrade_finished(new, old);
	else
		pkg_emit_install_finished(new);

	retcode = EPKG_OK;

cleanup:

	return (retcode);
}

static int
pkg_jobs_execute(struct pkg_jobs *j)
{
	struct pkg *p = NULL;
	struct pkg_solved *ps;
	struct pkg_manifest_key *keys = NULL;
	const char *cachedir = NULL, *name;
	int flags = 0;
	int retcode = EPKG_FATAL;
	bool handle_rc = false;

	if (j->flags & PKG_FLAG_SKIP_INSTALL)
		return (EPKG_OK);

	if ((j->flags & PKG_FLAG_FORCE) == PKG_FLAG_FORCE)
		flags |= PKG_DELETE_FORCE;

	if ((j->flags & PKG_FLAG_NOSCRIPT) == PKG_FLAG_NOSCRIPT)
		flags |= PKG_DELETE_NOSCRIPT;

	cachedir = pkg_object_string(pkg_config_get("PKG_CACHEDIR"));
	handle_rc = pkg_object_bool(pkg_config_get("HANDLE_RC_SCRIPTS"));

	/* XXX: get rid of hardcoded values */
	retcode = pkgdb_upgrade_lock(j->db, PKGDB_LOCK_ADVISORY,
			PKGDB_LOCK_EXCLUSIVE, 0.5, 20);
	if (retcode != EPKG_OK)
		return (retcode);

	p = NULL;
	pkg_manifest_keys_new(&keys);
	/* Install */
	pkgdb_transaction_begin(j->db->sqlite, "upgrade");

	pkg_jobs_set_priorities(j);

	DL_FOREACH(j->jobs, ps) {
		switch (ps->type) {
		case PKG_SOLVED_DELETE:
		case PKG_SOLVED_UPGRADE_REMOVE:
			p = ps->items[0]->pkg;
			pkg_get(p, PKG_NAME, &name);
			if ((strcmp(name, "pkg") == 0 ||
			    strcmp(name, "pkg-devel") == 0) &&
			    (flags & PKG_DELETE_FORCE) == 0) {
				pkg_emit_error("Cannot delete pkg itself without force flag");
				continue;
			}
			/*
			 * Assume that in upgrade we can remove packages with rdeps as
			 * in further they will be upgraded correctly.
			 */
			if (j->type == PKG_JOBS_UPGRADE)
				retcode = pkg_delete(p, j->db, flags | PKG_DELETE_CONFLICT);
			else
				retcode = pkg_delete(p, j->db, flags);
			if (retcode != EPKG_OK)
				goto cleanup;
			break;
		case PKG_SOLVED_INSTALL:
			retcode = pkg_jobs_handle_install(ps,
					j, handle_rc, cachedir, keys);
			if (retcode != EPKG_OK)
				goto cleanup;
			break;
		case PKG_SOLVED_UPGRADE:
			retcode = pkg_jobs_handle_install(ps,
					j, handle_rc, cachedir, keys);
			if (retcode != EPKG_OK)
				goto cleanup;
			break;
		case PKG_SOLVED_FETCH:
			retcode = EPKG_FATAL;
			pkg_emit_error("internal error: bad job type");
			goto cleanup;
			break;
		}

	}

cleanup:
	pkgdb_transaction_commit(j->db->sqlite, "upgrade");
	pkgdb_release_lock(j->db, PKGDB_LOCK_EXCLUSIVE);
	pkg_manifest_keys_free(keys);

	return (retcode);
}

int
pkg_jobs_apply(struct pkg_jobs *j)
{
	int rc;
	pkg_plugin_hook_t pre, post;
	bool has_conflicts = false;

	if (!j->solved) {
		pkg_emit_error("The jobs hasn't been solved");
		return (EPKG_FATAL);
	}

	if (j->type == PKG_JOBS_INSTALL) {
		pre = PKG_PLUGIN_HOOK_PRE_INSTALL;
		post = PKG_PLUGIN_HOOK_POST_INSTALL;
	}
	else if (j->type == PKG_JOBS_UPGRADE) {
		pre = PKG_PLUGIN_HOOK_PRE_UPGRADE;
		post = PKG_PLUGIN_HOOK_POST_UPGRADE;
	}
	else if (j->type == PKG_JOBS_AUTOREMOVE){
		pre = PKG_PLUGIN_HOOK_PRE_AUTOREMOVE;
		post = PKG_PLUGIN_HOOK_POST_AUTOREMOVE;
	}
	else {
		pre = PKG_PLUGIN_HOOK_PRE_DEINSTALL;
		post = PKG_PLUGIN_HOOK_POST_DEINSTALL;
	}

	switch (j->type) {
	case PKG_JOBS_INSTALL:
	case PKG_JOBS_UPGRADE:
	case PKG_JOBS_DEINSTALL:
	case PKG_JOBS_AUTOREMOVE:
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PRE_FETCH, j, j->db);
		rc = pkg_jobs_fetch(j);
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_POST_FETCH, j, j->db);
		if (rc == EPKG_OK) {
			/* Check local conflicts in the first run */
			if (j->solved == 1) {
				do {
					j->conflicts_registered = 0;
					rc = pkg_jobs_check_conflicts(j);
					if (rc == EPKG_CONFLICT) {
						/* Cleanup results */
						LL_FREE(j->jobs, free);
						j->jobs = NULL;
						j->count = 0;
						has_conflicts = true;
						rc = pkg_jobs_solve(j);
					}
					else if (rc == EPKG_OK && !has_conflicts) {
						pkg_plugins_hook_run(pre, j, j->db);
						rc = pkg_jobs_execute(j);
						break;
					}
				} while (j->conflicts_registered > 0);

				if (has_conflicts) {
					if (j->conflicts_registered == 0)
						pkg_jobs_set_priorities(j);

					pkg_emit_notice("The conflicts with the existing packages have been found.\n"
							"We need to run one more solver iteration to resolve them");
					return (EPKG_CONFLICT);
				}
			}
			else {
				/* Not the first run, conflicts are resolved already */
				pkg_plugins_hook_run(pre, j, j->db);
				rc = pkg_jobs_execute(j);
			}
		}
		pkg_plugins_hook_run(post, j, j->db);
		break;
	case PKG_JOBS_FETCH:
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PRE_FETCH, j, j->db);
		rc = pkg_jobs_fetch(j);
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_POST_FETCH, j, j->db);
		break;
	default:
		rc = EPKG_FATAL;
		pkg_emit_error("bad jobs argument");
		break;
	}

	return (rc);
}

#define PKG_JOBS_FETCH_CALCULATE(list) do {										\
	DL_FOREACH((list), ps) {														\
		if (ps->type != PKG_SOLVED_DELETE && ps->type != PKG_SOLVED_UPGRADE_REMOVE) {\
			p = ps->items[0]->pkg;													\
			if (p->type != PKG_REMOTE)												\
				continue;															\
			int64_t pkgsize;														\
			pkg_get(p, PKG_PKGSIZE, &pkgsize);				\
			pkg_snprintf(cachedpath, sizeof(cachedpath), "%S/%n-%v-%z", \
				cachedir, p, p, p);													\
			if (stat(cachedpath, &st) == -1)										\
				dlsize += pkgsize;													\
			else																	\
				dlsize += pkgsize - st.st_size;										\
		}																			\
	}																				\
} while(0)

#define PKG_JOBS_DO_FETCH(list) do {												\
	DL_FOREACH((list), ps) {														\
		if (ps->type != PKG_SOLVED_DELETE && ps->type != PKG_SOLVED_UPGRADE_REMOVE) {\
			p = ps->items[0]->pkg;													\
			if (p->type != PKG_REMOTE)												\
				continue;															\
			if (pkg_repo_fetch(p) != EPKG_OK)										\
				return (EPKG_FATAL);												\
		}																			\
	}																				\
} while(0)

static int
pkg_jobs_fetch(struct pkg_jobs *j)
{
	struct pkg *p = NULL;
	struct pkg_solved *ps;
	struct statfs fs;
	struct stat st;
	int64_t dlsize = 0;
	const char *cachedir = NULL;
	char cachedpath[MAXPATHLEN];
	
	cachedir = pkg_object_string(pkg_config_get("PKG_CACHEDIR"));

	/* check for available size to fetch */
	PKG_JOBS_FETCH_CALCULATE(j->jobs);

	if (dlsize == 0)
		return (EPKG_OK);

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
	PKG_JOBS_DO_FETCH(j->jobs);

	return (EPKG_OK);
}

#undef PKG_JOBS_FETCH_CALCULATE
#undef PKG_JOBS_DO_FETCH

static int
pkg_jobs_check_conflicts(struct pkg_jobs *j)
{
	struct pkg_solved *ps;
	struct pkg_manifest_key *keys = NULL;
	struct pkg *pkg = NULL, *p = NULL;
	const char *cachedir = NULL;
	char path[MAXPATHLEN];
	int ret = EPKG_OK, res, added = 0;

	cachedir = pkg_object_string(pkg_config_get("PKG_CACHEDIR"));

	pkg_emit_integritycheck_begin();

	pkg_manifest_keys_new(&keys);
	DL_FOREACH(j->jobs, ps) {
		if (ps->type == PKG_SOLVED_DELETE || ps->type == PKG_SOLVED_UPGRADE_REMOVE) {
			continue;
		}
		else {
			p = ps->items[0]->pkg;
			if (p->type == PKG_REMOTE) {
				pkg_snprintf(path, sizeof(path), "%R", p);
				if (*path != '/')
					pkg_snprintf(path, sizeof(path), "%S/%n-%v-%z",
							cachedir, p, p, p);
				if (pkg_open(&pkg, path, keys, 0) != EPKG_OK)
					return (EPKG_FATAL);
				p = pkg;
			}
			else if (p->type != PKG_FILE) {
				pkg_emit_error("invalid package type in solved jobs (internal error)");
				return (EPKG_FATAL);
			}
		}
		if ((res = pkg_conflicts_append_pkg(p, j)) != EPKG_OK)
			ret = res;
		else
			added ++;

	}
	pkg_manifest_keys_free(keys);
	pkg_free(pkg);

	if (added > 0) {
		pkg_debug(1, "check integrity for %d items added", added);
		if ((res = pkg_conflicts_integrity_check(j)) != EPKG_OK) {
			pkg_emit_integritycheck_finished(j->conflicts_registered);
			return (res);
		}
	}

	pkg_emit_integritycheck_finished(j->conflicts_registered);

	return (ret);
}
