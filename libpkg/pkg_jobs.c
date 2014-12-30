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

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <bsd_compat.h>

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/types.h>

#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <errno.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <ctype.h>

#ifdef HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif

#include "utarray.h"

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/pkg_jobs.h"

static int pkg_jobs_find_upgrade(struct pkg_jobs *j, const char *pattern, match_t m);
static int pkg_jobs_fetch(struct pkg_jobs *j);
static bool new_pkg_version(struct pkg_jobs *j);
static int pkg_jobs_check_conflicts(struct pkg_jobs *j);

#define IS_DELETE(j) ((j)->type == PKG_JOBS_DEINSTALL || (j)->type == PKG_JOBS_AUTOREMOVE)

int
pkg_jobs_new(struct pkg_jobs **j, pkg_jobs_t t, struct pkgdb *db)
{
	assert(db != NULL);

	if ((*j = calloc(1, sizeof(struct pkg_jobs))) == NULL) {
		pkg_emit_errno("calloc", "pkg_jobs");
		return (EPKG_FATAL);
	}

	(*j)->universe = pkg_jobs_universe_new(*j);

	if ((*j)->universe == NULL) {
		free(*j);
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
	if ((pkg_repo_find(ident)) == NULL) {
		pkg_emit_error("Unknown repository: %s", ident);
		return (EPKG_FATAL);
	}

	j->reponame = ident;

	return (EPKG_OK);
}

int
pkg_jobs_set_destdir(struct pkg_jobs *j, const char *dir)
{
	if (dir == NULL)
		return (EPKG_FATAL);

	j->destdir = dir;

	return (EPKG_OK);
}

const char*
pkg_jobs_destdir(struct pkg_jobs *j)
{
	return (j->destdir);
}

static void
pkg_jobs_pattern_free(struct job_pattern *jp)
{
	free(jp->pattern);
	free(jp->path);
	free(jp);
}

void
pkg_jobs_request_free(struct pkg_job_request *req)
{
	struct pkg_job_request_item *it, *tmp;

	if (req != NULL) {
		DL_FOREACH_SAFE(req->item, it, tmp) {
			free(it);
		}

		free(req);
	}
}

void
pkg_jobs_free(struct pkg_jobs *j)
{
	struct pkg_job_request *req, *tmp;

	if (j == NULL)
		return;

	HASH_ITER(hh, j->request_add, req, tmp) {
		HASH_DEL(j->request_add, req);
		pkg_jobs_request_free(req);
	}
	HASH_ITER(hh, j->request_delete, req, tmp) {
		HASH_DEL(j->request_delete, req);
		pkg_jobs_request_free(req);
	}

	pkg_jobs_universe_free(j->universe);
	LL_FREE(j->jobs, free);
	HASH_FREE(j->patterns, pkg_jobs_pattern_free);
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

				pkg_debug(1, "Jobs> Adding file: %s", pattern);
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

static struct pkg_job_request_item*
pkg_jobs_add_req_from_universe(struct pkg_job_request **head,
	struct pkg_job_universe_item *un, bool local, bool automatic)
{
	struct pkg_job_request *req;
	struct pkg_job_request_item *nit;
	struct pkg_job_universe_item *uit;
	bool new_req = false;

	assert(un != NULL);
	HASH_FIND_STR(*head, un->pkg->uid, req);

	if (req == NULL) {
		req = calloc(1, sizeof(*req));
		if (req == NULL) {
			pkg_emit_errno("malloc", "struct pkg_job_request");
			return (NULL);
		}
		new_req = true;
		req->automatic = automatic;
		pkg_debug(4, "add new uid %s to the request", un->pkg->uid);
	}
	else {
		if (req->item->unit == un) {
			/* We have exactly the same request, skip it */
			return (req->item);
		}
	}

	DL_FOREACH(un, uit) {
		if ((uit->pkg->type == PKG_INSTALLED && local) ||
				(uit->pkg->type != PKG_INSTALLED && !local)) {
			nit = calloc(1, sizeof(*nit));
			if (nit == NULL) {
				pkg_emit_errno("malloc", "struct pkg_job_request_item");
				return (NULL);
			}
			nit->pkg = uit->pkg;
			nit->unit = uit;
			DL_APPEND(req->item, nit);
		}
	}

	if (new_req) {
		if (req->item != NULL) {
			HASH_ADD_KEYPTR(hh, *head, un->pkg->uid, strlen(un->pkg->uid), req);
		}
		else {
			free(req);
			return (NULL);
		}
	}

	return (req->item);
}

static struct pkg_job_request_item*
pkg_jobs_add_req(struct pkg_jobs *j, struct pkg *pkg)
{
	struct pkg_job_request *req, **head;
	struct pkg_job_request_item *nit;
	struct pkg_job_universe_item *un;
	int rc;

	assert(pkg != NULL);

	if (!IS_DELETE(j)) {
		head = &j->request_add;
		assert(pkg->type != PKG_INSTALLED);
	}
	else {
		head = &j->request_delete;
		assert(pkg->type == PKG_INSTALLED);
	}
	rc = pkg_jobs_universe_add_pkg(j->universe, pkg, false, &un);
	if (rc == EPKG_END) {
		/*
		 * This means that we have a package in the universe with the same
		 * digest. In turn, that means that two upgrade candidates are equal,
		 * we thus won't do anything with this item, as it is definitely useless
		 */
		HASH_FIND_STR(*head, pkg->uid, req);
		if (req != NULL) {
			DL_FOREACH(req->item, nit) {
				if (nit->unit == un)
					return (nit);
			}
		}
		else {
			/*
			 * We need to add request chain from the universe chain
			 */
			return (pkg_jobs_add_req_from_universe(head, un, IS_DELETE(j), false));
		}

		return (NULL);
	}
	else if (rc == EPKG_FATAL) {
		/*
		 * Something bad has happened
		 */
		return (NULL);
	}

	if (pkg->locked) {
		pkg_emit_locked(pkg);
		return (NULL);
	}

	HASH_FIND_STR(*head, pkg->uid, req);

	nit = calloc(1, sizeof(*nit));
	if (nit == NULL) {
		pkg_emit_errno("malloc", "struct pkg_job_request_item");
		return (NULL);
	}
	nit->pkg = pkg;
	nit->unit = un;

	if (req == NULL) {
		/* Allocate new unique request item */
		req = calloc(1, sizeof(*req));
		if (req == NULL) {
			pkg_emit_errno("malloc", "struct pkg_job_request");
			return (NULL);
		}
		HASH_ADD_KEYPTR(hh, *head, pkg->uid, strlen(pkg->uid), req);
	}

	/* Append candidate to the list of candidates */
	DL_APPEND(req->item, nit);

	return (nit);
}

/*
 * Post-process add request and handle flags:
 * upgrade - search for upgrades for dependencies and add them to the request
 * force - all upgrades are forced
 * reverse - try to upgrade reverse deps as well
 */
static void
pkg_jobs_process_add_request(struct pkg_jobs *j, bool top)
{
	bool force = j->flags & PKG_FLAG_FORCE,
		 reverse = j->flags & PKG_FLAG_RECURSIVE,
		 upgrade = j->type == PKG_JOBS_UPGRADE;
	struct pkg_job_request *req, *tmp, *found;
	struct pkg_job_request_item *it;
	struct pkg_job_universe_item *un, *cur;
	struct pkg_dep *d;
	struct pkg *lp;
	int (*deps_func)(const struct pkg *pkg, struct pkg_dep **d);
	UT_array *to_process = NULL;

	if (upgrade || reverse) {
		utarray_new(to_process, &ut_ptr_icd);

		HASH_ITER(hh, j->request_add, req, tmp) {
			it = req->item;

			if (reverse)
				deps_func = pkg_rdeps;
			else
				deps_func = pkg_deps;

			d = NULL;
			/*
			 * Here we get deps of local packages only since we are pretty sure
			 * that they are completely expanded
			 */
			lp = pkg_jobs_universe_get_local(j->universe,
			    it->pkg->uid, 0);
			while (lp != NULL && deps_func(lp, &d) == EPKG_OK) {
				/*
				 * Do not add duplicated upgrade candidates
				 */
				HASH_FIND_STR(j->request_add, d->uid, found);
				if (found != NULL)
					continue;

				pkg_debug(4, "adding dependency %s to request", d->uid);
				lp = pkg_jobs_universe_get_local(j->universe,
					d->uid, 0);
				/*
				 * Here we need to check whether specific remote package
				 * is newer than a local one
				 */
				un = pkg_jobs_universe_get_upgrade_candidates(j->universe,
					d->uid, lp, force);
				if (un == NULL)
					continue;

				cur = un->prev;
				while (cur != un) {
					if (cur->pkg->type != PKG_INSTALLED) {
						utarray_push_back(to_process, &un);
						break;
					}
					cur = cur->prev;
				}
			}
		}
	}

	if (to_process) {
		/* Add all items to the request */
		struct pkg_job_universe_item **pun = NULL;

		if (to_process->n > 0) {
			while ((pun = (struct pkg_job_universe_item **)
							utarray_next(to_process, pun)) != NULL) {
				pkg_jobs_add_req_from_universe(&j->request_add, *pun, false, true);
			}
			/* Now recursively process all items checked */
			pkg_jobs_process_add_request(j, false);
		}
		utarray_free(to_process);
	}
}

/*
 * For delete request we merely check rdeps and force flag
 */
static int
pkg_jobs_process_delete_request(struct pkg_jobs *j)
{
	bool force = j->flags & PKG_FLAG_FORCE;
	struct pkg_job_request *req, *tmp, *found;
	struct pkg_dep *d = NULL;
	UT_array *to_process = NULL;
	struct pkg *lp;
	int rc = EPKG_OK;

	if (!force) {
		/*
		 * Need to add also all reverse deps here
		 */
		utarray_new(to_process, &ut_ptr_icd);
		HASH_ITER(hh, j->request_delete, req, tmp) {
			d = NULL;
			while (pkg_rdeps(req->item->pkg, &d) == EPKG_OK) {
				HASH_FIND_STR(j->request_delete, d->uid, found);
				if (found)
					continue;

				lp = pkg_jobs_universe_get_local(j->universe, d->uid, 0);
				if (lp) {
					if (lp->locked) {
						pkg_emit_error("%s is locked, "
						   "cannot delete %s", lp->name,
						   req->item->pkg->name);
						rc = EPKG_FATAL;
					}
					utarray_push_back(to_process, &lp);
				}
			}
		}
	}

	if (to_process) {
		/* Add all items to the request */
		struct pkg **ppkg = NULL;

		if (to_process->n > 0 && rc != EPKG_FATAL) {
			while ((ppkg = (struct pkg **)
							utarray_next(to_process, ppkg)) != NULL) {

				if (pkg_jobs_add_req(j, *ppkg) == NULL) {
					utarray_free(to_process);
					return (EPKG_FATAL);
				}
			}
			/* Now recursively process all items checked */
			rc = pkg_jobs_process_delete_request(j);
		}
		utarray_free(to_process);
	}

	return (rc);
}

static int
pkg_jobs_set_execute_priority(struct pkg_jobs *j, struct pkg_solved *solved)
{
	struct pkg_solved *ts;

	if (solved->type == PKG_SOLVED_UPGRADE
			&& solved->items[1]->pkg->conflicts != NULL) {
		/*
		 * We have an upgrade request that has some conflicting packages, therefore
		 * update priorities of local packages and try to update priorities of remote ones
		 */
		if (solved->items[0]->priority == 0)
			pkg_jobs_update_conflict_priority(j->universe, solved);

		if (solved->items[1]->priority > solved->items[0]->priority &&
				!solved->already_deleted) {
			/*
			 * Split conflicting upgrade request into delete -> upgrade request
			 */
			ts = calloc(1, sizeof(struct pkg_solved));
			if (ts == NULL) {
				pkg_emit_errno("calloc", "pkg_solved");
				return (EPKG_FATAL);
			}

			ts->type = PKG_SOLVED_UPGRADE_REMOVE;
			ts->items[0] = solved->items[1];
			solved->items[1] = NULL;
			solved->type = PKG_SOLVED_INSTALL;
			DL_APPEND(j->jobs, ts);
			j->count ++;
			solved->already_deleted = true;
			pkg_debug(2, "split upgrade request for %s",
			   ts->items[0]->pkg->uid);
			return (EPKG_CONFLICT);
		}
	}
	else if (solved->type == PKG_SOLVED_DELETE) {
		if (solved->items[0]->priority == 0)
			pkg_jobs_update_universe_priority(j->universe, solved->items[0],
					PKG_PRIORITY_UPDATE_DELETE);
	}
	else {
		if (solved->items[0]->priority == 0)
			pkg_jobs_update_universe_priority(j->universe, solved->items[0],
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
		if (pkg_jobs_set_execute_priority(j, req) == EPKG_CONFLICT)
			goto iter_again;
	}

	DL_SORT(j->jobs, pkg_jobs_sort_priority);
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
		unit = pkg_jobs_universe_find(j->universe, d->uid);
		if (unit != NULL) {
			if (!unit->pkg->automatic) {
				return (false);
			}
			npkg = unit->pkg;
		}
		else {
			npkg = pkg_jobs_universe_get_local(j->universe, d->uid,
					PKG_LOAD_BASIC|PKG_LOAD_RDEPS|PKG_LOAD_ANNOTATIONS);
			if (npkg == NULL)
				return (false);
			if (!npkg->automatic) {
				/*
				 * Safe to free, as d->uid is not in the universe
				 */
				pkg_free(npkg);
				return (false);
			}
			if (pkg_jobs_universe_process(j->universe, npkg) != EPKG_OK)
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
	const char *uid = "pkg";
	pkg_flags old_flags;
	bool ret = false;

	/* Disable -f for pkg self-check, and restore at end. */
	old_flags = j->flags;
	j->flags &= ~(PKG_FLAG_FORCE|PKG_FLAG_RECURSIVE);

	/* determine local pkgng */
	p = pkg_jobs_universe_get_local(j->universe, uid, 0);

	if (p == NULL) {
		uid = "pkg-devel";
		p = pkg_jobs_universe_get_local(j->universe, uid, 0);
	}

	/* you are using git version skip */
	if (p == NULL) {
		ret = false;
		goto end;
	}

	/* Use maximum priority for pkg */
	if (pkg_jobs_find_upgrade(j, uid, MATCH_EXACT) == EPKG_OK) {
		ret = true;
		goto end;
	}

end:
	j->flags = old_flags;

	return (ret);
}

static int
pkg_jobs_process_remote_pkg(struct pkg_jobs *j, struct pkg *rp,
	struct pkg_job_request_item **req)
{
	struct pkg_job_universe_item *nit;
	struct pkg_job_request_item *nrit = NULL;
	struct pkg *lp = NULL;

	if (rp->digest == NULL) {
		if (pkg_checksum_calculate(rp, j->db) != EPKG_OK) {
			return (EPKG_FATAL);
		}
	}
	if (j->type != PKG_JOBS_FETCH) {
		lp = pkg_jobs_universe_get_local(j->universe, rp->uid, 0);
		if (lp && lp->locked)
			return (EPKG_LOCKED);
	}

	nit = pkg_jobs_universe_get_upgrade_candidates(j->universe, rp->uid, lp,
		j->flags & PKG_FLAG_FORCE);

	if (nit != NULL) {
		nrit = pkg_jobs_add_req_from_universe(&j->request_add, nit, false, false);
		if (req != NULL)
			*req = nrit;
	}

	if (nrit == NULL && lp)
		return (EPKG_INSTALLED);

	return (nrit != NULL ? EPKG_OK : EPKG_FATAL);
}

static bool
pkg_jobs_has_replacement(struct pkg_jobs *j, const char *uid)
{
	struct pkg_job_replace *cur;

	LL_FOREACH(j->universe->uid_replaces, cur) {
		if (strcmp (cur->new_uid, uid) == 0) {
			return (true);
		}
	}

	return (false);
}

static int
pkg_jobs_try_remote_candidate(struct pkg_jobs *j, const char *pattern,
    const char *uid, match_t m)
{
	struct pkg *p = NULL;
	struct pkgdb_it *it;
	unsigned flags = PKG_LOAD_BASIC|PKG_LOAD_OPTIONS|PKG_LOAD_DEPS|
				PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|
				PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;
	int rc = EPKG_FATAL;
	struct sbuf *qmsg;
	struct pkg_job_universe_item *unit;

	if ((it = pkgdb_repo_query(j->db, pattern, m, j->reponame)) == NULL)
		return (EPKG_FATAL);

	qmsg = sbuf_new_auto();

	while (it != NULL && pkgdb_it_next(it, &p, flags) == EPKG_OK) {
		if (pkg_jobs_has_replacement(j, p->uid)) {
			pkg_debug(1, "replacement %s is already used", p->uid);
			continue;
		}

		sbuf_printf(qmsg, "%s has no direct installation candidates, change it to "
				"%s? [Y/n]: ", uid, p->uid);
		sbuf_finish(qmsg);
		if (pkg_emit_query_yesno(true, sbuf_data(qmsg))) {
			/* Change the origin of the local package */
			pkg_validate(p);
			unit = pkg_jobs_universe_find(j->universe, uid);
			if (unit != NULL)
				pkg_jobs_universe_change_uid(j->universe, unit, p->uid,
					strlen(p->uid), false);
			else
				assert(0);

			rc = EPKG_OK;
			pkg_jobs_process_remote_pkg(j, p, NULL);
			if (rc == EPKG_OK) {
				/* Avoid freeing */
				p = NULL;
			}
			break;
		}
		sbuf_reset(qmsg);
	}


	pkg_free(p);

	sbuf_free(qmsg);
	pkgdb_it_free(it);

	return (rc);
}

static int
pkg_jobs_guess_upgrade_candidate(struct pkg_jobs *j, const char *pattern)
{

	int rc = EPKG_FATAL;
	const char *pos, *opattern = pattern;
	char *cpy;
	size_t len, olen;

	/* First of all, try to search a package with the same name */
	pos = strchr(pattern, '/');
	if (pos != NULL && pos[1] != '\0') {
		if (pkg_jobs_try_remote_candidate(j, pos + 1, opattern, MATCH_EXACT)
						== EPKG_OK)
			return (EPKG_OK);

		pos ++;
		pattern = pos;
	}
	else {
		pos = pattern;
	}

	/* Figure, if we have any numbers at the end of the package */
	olen = strlen(pos);
	len = olen;
	while (len > 0) {
		if (isdigit(pos[len - 1]) || pos[len - 1] == '.')
			len --;
		else
			break;
	}

	if (olen != len) {
		/* Try exact pattern without numbers */
		cpy = malloc(len + 1);
		strlcpy(cpy, pos, len + 1);
		if (pkg_jobs_try_remote_candidate(j, cpy, opattern, MATCH_EXACT) != EPKG_OK) {
			free(cpy);
			cpy = sqlite3_mprintf(" WHERE name REGEXP ('^' || %.*Q || '[0-9.]*$')",
					len, pos);

			if (pkg_jobs_try_remote_candidate(j, cpy, opattern, MATCH_CONDITION)
					== EPKG_OK)
				rc = EPKG_OK;
			sqlite3_free(cpy);
		}
		else {
			free(cpy);
			rc = EPKG_OK;
		}
	}

	return (rc);
}

static int
pkg_jobs_find_upgrade(struct pkg_jobs *j, const char *pattern, match_t m)
{
	struct pkg *p = NULL;
	struct pkgdb_it *it;
	bool found = false;
	int rc = EPKG_FATAL;
	struct pkg_dep *rdep = NULL;
	unsigned flags = PKG_LOAD_BASIC|PKG_LOAD_OPTIONS|PKG_LOAD_DEPS|
			PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|
			PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;
	struct pkg_job_universe_item *unit = NULL;

	if ((it = pkgdb_repo_query(j->db, pattern, m, j->reponame)) == NULL)
		rc = EPKG_FATAL;

	while (it != NULL && pkgdb_it_next(it, &p, flags) == EPKG_OK) {
		rc = pkg_jobs_process_remote_pkg(j, p, NULL);
		if (rc == EPKG_FATAL)
			break;
		else if (rc == EPKG_OK)
			found = true;

		p = NULL;
	}

	pkgdb_it_free(it);

	if (!found && rc != EPKG_INSTALLED) {
		/*
		 * Here we need to ensure that this package has no
		 * reverse deps installed
		 */
		p = pkg_jobs_universe_get_local(j->universe, pattern,
			PKG_LOAD_BASIC|PKG_LOAD_RDEPS);
		if (p == NULL)
			return (EPKG_FATAL);

		while(pkg_rdeps(p, &rdep) == EPKG_OK) {
			struct pkg *rdep_package;

			rdep_package = pkg_jobs_universe_get_local(j->universe, rdep->uid,
					PKG_LOAD_BASIC);
			if (rdep_package != NULL)
				return (EPKG_END);
		}

		pkg_debug(2, "non-automatic package with pattern %s has not been found in "
				"remote repo", pattern);
		pkg_jobs_universe_add_pkg(j->universe, p, false, &unit);
		rc = pkg_jobs_guess_upgrade_candidate(j, pattern);
	}

	return (rc);
}

static int
pkg_jobs_check_local_pkg(struct pkg_jobs *j, struct job_pattern *jp)
{
	struct pkgdb_it *it;
	struct pkg *pkg = NULL;
	int rc = EPKG_OK;

	it = pkgdb_query(j->db, jp->pattern, jp->match);
	if (it != NULL) {
		if (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_ANNOTATIONS) != EPKG_OK)
			rc = EPKG_FATAL;
		else
			pkg_free(pkg);

		pkgdb_it_free(it);
	}
	else {
		rc = EPKG_FATAL;
	}

	return (rc);
}

static int
pkg_jobs_find_remote_pattern(struct pkg_jobs *j, struct job_pattern *jp)
{
	int rc = EPKG_OK;
	struct pkg *pkg = NULL;
	struct pkg_manifest_key *keys = NULL;
	struct pkg_job_request *req;
	struct job_pattern jfp;

	if (!jp->is_file) {
		if (j->type == PKG_JOBS_UPGRADE) {
			/*
			 * For upgrade patterns we must ensure that a local package is
			 * installed as well.
			 */
			if (pkg_jobs_check_local_pkg(j, jp) != EPKG_OK) {
				pkg_emit_error("%s is not installed, therefore upgrade is impossible",
						jp->pattern);
				return (EPKG_FATAL);
			}
		}
		rc = pkg_jobs_find_upgrade(j, jp->pattern, jp->match);
	}
	else {
		pkg_manifest_keys_new(&keys);
		if (pkg_open(&pkg, jp->path, keys, PKG_OPEN_MANIFEST_ONLY) != EPKG_OK) {
			rc = EPKG_FATAL;
		} else if (pkg_validate(pkg) == EPKG_OK) {
			if (j->type == PKG_JOBS_UPGRADE) {
				jfp.match = MATCH_EXACT;
				jfp.pattern = pkg->name;
				if (pkg_jobs_check_local_pkg (j, &jfp) != EPKG_OK) {
					pkg_emit_error("%s is not installed, therefore upgrade is impossible",
							jfp.pattern);
					pkg_manifest_keys_free(keys);
					return (EPKG_FATAL);
				}
			}
			pkg->type = PKG_FILE;
			pkg_jobs_add_req(j, pkg);

			HASH_FIND_STR(j->request_add, pkg->uid, req);
			if (req != NULL)
				req->item->jp = jp;
		}
		else {
			pkg_emit_error("cannot load %s: invalid format",
					jp->pattern);
			rc = EPKG_FATAL;
		}
		pkg_manifest_keys_free(keys);
	}

	return (rc);
}

bool
pkg_jobs_need_upgrade(struct pkg *rp, struct pkg *lp)
{
	int ret, ret1, ret2;
	struct pkg_option *lo = NULL, *ro = NULL;
	struct pkg_dep *ld = NULL, *rd = NULL;
	struct pkg_shlib *ls = NULL, *rs = NULL;
	struct pkg_conflict *lc = NULL, *rc = NULL;
	struct pkg_provide *lpr = NULL, *rpr = NULL;

	/* If no local package, then rp is obviously need to be added */
	if (lp == NULL)
		return true;

	/* Do not upgrade locked packages */
	if (lp->locked) {
		pkg_emit_locked(lp);
		return (false);
	}

	if (lp->digest != NULL && rp->digest != NULL &&
	    strcmp(lp->digest, rp->digest) == 0) {
		/* Remote and local packages has the same digest, hence they are the same */
		return (false);
	}
	/*
	 * XXX: for a remote package we also need to check whether options
	 * are compatible.
	 */
	ret = pkg_version_cmp(lp->version, rp->version);
	if (ret > 0)
		return (false);
	else if (ret < 0)
		return (true);

	/* Compare archs */
	if (strcmp (lp->arch, rp->arch) != 0) {
		free(rp->reason);
		rp->reason = strdup("ABI changed");
		return (true);
	}

	/* compare options */
	for (;;) {
		ret1 = pkg_options(rp, &ro);
		ret2 = pkg_options(lp, &lo);
		if (ret1 != ret2) {
			free(rp->reason);
			rp->reason = strdup("options changed");
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if (strcmp(lo->key, ro->key) != 0 ||
			    strcmp(lo->value, ro->value) != 0) {
				free(rp->reason);
				rp->reason = strdup("options changed");
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
			free(rp->reason);
			rp->reason = strdup("direct dependency changed");
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if ((strcmp(rd->name, ld->name) != 0) ||
			    (strcmp(rd->origin, ld->origin) != 0)) {
				free(rp->reason);
				rp->reason = strdup("direct dependency changed");
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
			free(rp->reason);
			rp->reason = strdup("direct conflict changed");
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if (strcmp(rc->uid, lc->uid) != 0) {
				free(rp->reason);
				rp->reason = strdup("direct conflict changed");
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
			free(rp->reason);
			rp->reason = strdup("provides changed");
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if (strcmp(rpr->provide, lpr->provide) != 0) {
				free(rp->reason);
				rp->reason = strdup("provides changed");
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
			free(rp->reason);
			rp->reason = strdup("needed shared library changed");
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if (strcmp(rs->name, ls->name) != 0) {
				free(rp->reason);
				rp->reason = strdup("needed shared library changed");
				pkg_debug(1, "shlib changed %s -> %s",
				    ls->name, rs->name);
				return (true);
			}
		}
		else
			break;
	}

	return (false);
}

static void
pkg_jobs_propagate_automatic(struct pkg_jobs *j)
{
	struct pkg_job_universe_item *unit, *utmp, *cur, *local;
	struct pkg_job_request *req;
	bool automatic;

	HASH_ITER(hh, j->universe->items, unit, utmp) {
		if (unit->next == NULL) {
			/*
			 * For packages that are alone in the installation list
			 * we search them in the corresponding request
			 */
			HASH_FIND_STR(j->request_add, unit->pkg->uid, req);
			if ((req == NULL || req->automatic) &&
			    unit->pkg->type != PKG_INSTALLED) {
				automatic = true;
				pkg_debug(2, "set automatic flag for %s", unit->pkg->uid);
				unit->pkg->automatic = automatic;
			}
			else {
				if (j->type == PKG_JOBS_INSTALL) {
					automatic = false;
					unit->pkg->automatic = false;
				}
			}
		}
		else {
			/*
			 * For packages that are in the conflict chain we need to inherit
			 * automatic flag from the local package
			 */
			local = NULL;
			automatic = false;
			LL_FOREACH(unit, cur) {
				if (cur->pkg->type == PKG_INSTALLED) {
					local = cur;
					automatic = local->pkg->automatic;
					break;
				}
			}
			if (local != NULL)
				LL_FOREACH(unit, cur)
					if (cur->pkg->type != PKG_INSTALLED)
						cur->pkg->automatic = automatic;
		}
	}
}

static struct pkg_job_request *
pkg_jobs_find_deinstall_request(struct pkg_job_universe_item *item,
		struct pkg_jobs *j, int rec_level)
{
	struct pkg_job_request *found;
	struct pkg_job_universe_item *dep_item;
	struct pkg_dep *d = NULL;
	struct pkg *pkg = item->pkg;

	if (rec_level > 128) {
		pkg_debug(2, "cannot find deinstall request after 128 iterations for %s,"
		    "circular dependency maybe", pkg->uid);
		return (NULL);
	}

	HASH_FIND_STR(j->request_delete, pkg->uid, found);
	if (found == NULL) {
		while (pkg_deps(pkg, &d) == EPKG_OK) {
			dep_item = pkg_jobs_universe_find(j->universe, d->uid);
			if (dep_item) {
				found = pkg_jobs_find_deinstall_request(dep_item, j, rec_level + 1);
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
	struct pkg_solved *sit;
	struct pkg_job_request *jreq;
	struct pkg *req_pkg, *pkg;

	LL_FOREACH(j->jobs, sit) {
		jreq = pkg_jobs_find_deinstall_request(sit->items[0], j, 0);
		if (jreq != NULL && jreq->item->unit != sit->items[0]) {
			req_pkg = jreq->item->pkg;
			pkg = sit->items[0]->pkg;
			/* Set the reason */
			free(pkg->reason);
			pkg_asprintf(&pkg->reason, "depends on %n-%v", req_pkg, req_pkg);
		}
	}
}

static int
jobs_solve_deinstall(struct pkg_jobs *j)
{
	struct job_pattern *jp, *jtmp;
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	HASH_ITER(hh, j->patterns, jp, jtmp) {
		if ((it = pkgdb_query(j->db, jp->pattern, jp->match)) == NULL)
			return (EPKG_FATAL);

		while (pkgdb_it_next(it, &pkg,
				PKG_LOAD_BASIC|PKG_LOAD_RDEPS|PKG_LOAD_DEPS|PKG_LOAD_ANNOTATIONS) == EPKG_OK) {
			if(pkg->locked) {
				pkg_emit_locked(pkg);
			}
			else {
				pkg_jobs_add_req(j, pkg);
			}
			pkg = NULL;
		}
		pkgdb_it_free(it);
	}

	j->solved = 1;

	return (pkg_jobs_process_delete_request(j));
}

static int
jobs_solve_autoremove(struct pkg_jobs *j)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;

	if ((it = pkgdb_query(j->db, " WHERE automatic=1 ", MATCH_CONDITION)) == NULL)
		return (EPKG_FATAL);

	while (pkgdb_it_next(it, &pkg,
			PKG_LOAD_BASIC|PKG_LOAD_RDEPS|PKG_LOAD_DEPS|PKG_LOAD_ANNOTATIONS)
			== EPKG_OK) {
		if(pkg->locked) {
			pkg_emit_locked(pkg);
		}
		else if (pkg_jobs_test_automatic(j, pkg)) {
			assert(pkg_jobs_add_req(j, pkg));
		}

		pkg = NULL;
	}
	pkgdb_it_free(it);

	j->solved = true;
	pkg_jobs_process_delete_request(j);

	return (EPKG_OK);
}

struct pkg_jobs_install_candidate {
	int64_t id;
	struct pkg_jobs_install_candidate *next;
};

static struct pkg_jobs_install_candidate *
pkg_jobs_new_candidate(struct pkg *pkg)
{
	struct pkg_jobs_install_candidate *n;

	n = malloc(sizeof(*n));
	if (n == NULL) {
		pkg_emit_errno("malloc", "pkg_jobs_install_candidate");
		return (NULL);
	}
	n->id = pkg->id;
	return (n);
}

static bool
pkg_jobs_check_remote_candidate(struct pkg_jobs *j, struct pkg *pkg)
{
	struct pkgdb_it *it;
	struct pkg *p = NULL;

	/* If we have no digest, we need to check this package */
	if (pkg->digest == NULL)
		return (true);

	it = pkgdb_repo_query(j->db, pkg->uid, MATCH_EXACT, j->reponame);
	if (it != NULL) {
		/*
		 * If we have the same package in a remote repo, it is not an
		 * installation candidate
		 */
		int npkg = 0;

		while (pkgdb_it_next(it, &p, PKG_LOAD_BASIC) == EPKG_OK) {
			/*
			 * Check package with the same uid and explore whether digest
			 * has been changed
			 */
			if (strcmp(p->digest, pkg->digest) != 0)
				npkg ++;

			pkg_free(p);
			p = NULL;
		}

		pkgdb_it_free(it);

		if (npkg == 0)
			return (false);
	}

	return (true);
}

static struct pkg_jobs_install_candidate *
pkg_jobs_find_install_candidates(struct pkg_jobs *j, size_t *count)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	struct pkg_jobs_install_candidate *candidates = NULL, *c;

	if ((it = pkgdb_query(j->db, NULL, MATCH_ALL)) == NULL)
		return (NULL);

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		if ((j->flags & PKG_FLAG_FORCE) ||
						pkg_jobs_check_remote_candidate(j, pkg)) {
			c = pkg_jobs_new_candidate(pkg);
			LL_PREPEND(candidates, c);
			(*count)++;
		}
		pkg_free(pkg);
		pkg = NULL;
	}
	pkgdb_it_free(it);

	return (candidates);
}

static int
jobs_solve_install_upgrade(struct pkg_jobs *j)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	char sqlbuf[256];
	size_t jcount = 0;
	struct job_pattern *jp, *jtmp;
	struct pkg_job_request *req, *rtmp;
	unsigned flags = PKG_LOAD_BASIC|PKG_LOAD_OPTIONS|PKG_LOAD_DEPS|
			PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;
	struct pkg_jobs_install_candidate *candidates, *c;

	/* Check for new pkg. Skip for 'upgrade -F'. */
	if ((j->flags & PKG_FLAG_SKIP_INSTALL) == 0 &&
	    (j->flags & PKG_FLAG_PKG_VERSION_TEST) == PKG_FLAG_PKG_VERSION_TEST)
		if (new_pkg_version(j)) {
			pkg_emit_newpkgversion();
			goto order;
		}

	if (j->patterns == NULL && j->type == PKG_JOBS_INSTALL) {
		pkg_emit_error("no patterns are specified for install job");
		return (EPKG_FATAL);
	}

	if (j->solved == 0) {
		if (j->patterns == NULL) {
			size_t elt_num = 0;

			candidates = pkg_jobs_find_install_candidates(j, &jcount);

			pkg_emit_progress_start("Checking for upgrades (%zd candidates)",
				jcount);

			LL_FOREACH(candidates, c) {
				pkg_emit_progress_tick(++elt_num, jcount);
				sqlite3_snprintf(sizeof(sqlbuf), sqlbuf, " WHERE id=%" PRId64,
						c->id);
				if ((it = pkgdb_query(j->db, sqlbuf, MATCH_CONDITION)) == NULL)
					return (EPKG_FATAL);

				pkg = NULL;
				while (pkgdb_it_next(it, &pkg, flags) == EPKG_OK) {
					/* Do not test we ignore what doesn't exists remotely */
					pkg_jobs_find_upgrade(j, pkg->uid, MATCH_EXACT);
				}
				pkg_free(pkg);
				pkgdb_it_free(it);
			}
			pkg_emit_progress_tick(jcount, jcount);
			LL_FREE(candidates, free);

			pkg_emit_progress_start("Processing candidates (%zd candidates)",
				jcount);
			elt_num = 0;
			HASH_ITER(hh, j->request_add, req, rtmp) {
				pkg_emit_progress_tick(++elt_num, jcount);
				pkg_jobs_universe_process(j->universe, req->item->pkg);
			}
			pkg_emit_progress_tick(jcount, jcount);
		}
		else {
			HASH_ITER(hh, j->patterns, jp, jtmp) {
				if (pkg_jobs_find_remote_pattern(j, jp) == EPKG_FATAL) {
					pkg_emit_error("No packages available to %s matching '%s' "
							"have been found in the "
							"repositories",
							(j->type == PKG_JOBS_UPGRADE) ? "upgrade" : "install",
							jp->pattern);
					return (EPKG_FATAL);
				}
			}
			/*
			 * Need to iterate request one more time to recurse depends
			 */
			HASH_ITER(hh, j->request_add, req, rtmp)
				pkg_jobs_universe_process(j->universe, req->item->pkg);
		}
	}
	else {
		/*
		 * If we have tried to solve request, then we just want to re-add all
		 * request packages to the universe to find out any potential conflicts
		 */
		HASH_ITER(hh, j->request_add, req, rtmp) {
			pkg_jobs_universe_process(j->universe, req->item->pkg);
		}
	}

	pkg_jobs_process_add_request(j, true);
	if (pkg_conflicts_request_resolve(j) != EPKG_OK) {
		pkg_emit_error("Cannot resolve conflicts in a request");
		return (EPKG_FATAL);
	}

	pkg_jobs_propagate_automatic(j);

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
	struct pkg_job_request *req, *rtmp;
	unsigned flag = PKG_LOAD_BASIC|PKG_LOAD_ANNOTATIONS;

	if ((j->flags & PKG_FLAG_WITH_DEPS) == PKG_FLAG_WITH_DEPS)
		flag |= PKG_LOAD_DEPS;

	if ((j->flags & PKG_FLAG_UPGRADES_FOR_INSTALLED) == PKG_FLAG_UPGRADES_FOR_INSTALLED) {
		if ((it = pkgdb_query(j->db, NULL, MATCH_ALL)) == NULL)
			return (EPKG_FATAL);

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
			if(pkg->locked) {
				pkg_emit_locked(pkg);
			}
			else {
				/* Do not test we ignore what doesn't exists remotely */
				pkg_jobs_find_upgrade(j, pkg->uid, MATCH_EXACT);
			}
			pkg = NULL;
		}
		pkgdb_it_free(it);
	} else {
		HASH_ITER(hh, j->patterns, jp, jtmp) {
			/* TODO: use repository priority here */
			if (pkg_jobs_find_upgrade(j, jp->pattern, jp->match) == EPKG_FATAL)
				pkg_emit_error("No packages matching '%s' have been found in the "
						"repositories", jp->pattern);
		}
		HASH_ITER(hh, j->request_add, req, rtmp)
			pkg_jobs_universe_process(j->universe, req->item->pkg);
	}

	j->solved ++;

	return (EPKG_OK);
}

static void
pkg_jobs_apply_replacements(struct pkg_jobs *j)
{
	struct pkg_job_replace *r;
	static const char sql[] = ""
		"UPDATE packages SET name=?1 "
		" WHERE name=?2;" ;
	sqlite3_stmt *stmt;
	int ret;

	pkg_debug(4, "jobs: running '%s'", sql);
	ret = sqlite3_prepare_v2(j->db->sqlite, sql, -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(j->db->sqlite, sql);
		return;
	}

	LL_FOREACH(j->universe->uid_replaces, r) {
		pkg_debug(4, "changing uid %s -> %s", r->old_uid, r->new_uid);
		sqlite3_bind_text(stmt, 1, r->new_uid, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 2, r->old_uid, -1, SQLITE_TRANSIENT);

		if (sqlite3_step(stmt) != SQLITE_DONE)
			ERROR_SQLITE(j->db->sqlite, sql);

		sqlite3_reset(stmt);
	}

	sqlite3_finalize(stmt);
}

int
pkg_jobs_solve(struct pkg_jobs *j)
{
	int ret, pstatus;
	struct pkg_solve_problem *problem;
	struct pkg_solved *job;
	const char *solver;
	FILE *spipe[2];
	pid_t pchild;

	pkgdb_begin_solver(j->db);

	switch (j->type) {
	case PKG_JOBS_AUTOREMOVE:
		ret =jobs_solve_autoremove(j);
		break;
	case PKG_JOBS_DEINSTALL:
		ret = jobs_solve_deinstall(j);
		break;
	case PKG_JOBS_UPGRADE:
	case PKG_JOBS_INSTALL:
		ret = jobs_solve_install_upgrade(j);
		break;
	case PKG_JOBS_FETCH:
		ret = jobs_solve_fetch(j);
		break;
	default:
		pkgdb_end_solver(j->db);
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
again:
			pkg_jobs_universe_process_upgrade_chains(j);
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
					ret = pkg_solve_sat_problem(problem);
					if (ret == EPKG_FATAL) {
						pkg_emit_error("cannot solve job using SAT solver");
						ret = EPKG_FATAL;
						pkg_solve_problem_free(problem);
						j->solved = 0;
					}
					else if (ret == EPKG_AGAIN) {
						pkg_solve_problem_free(problem);
						goto again;
					}
					else {
						ret = pkg_solve_sat_to_jobs(problem);
						pkg_solve_problem_free(problem);
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

	pkgdb_end_solver(j->db);

	if (ret != EPKG_OK)
		return (ret);

	pkg_jobs_apply_replacements(j);

	/* Check if we need to fetch and re-run the solver */
	DL_FOREACH(j->jobs, job) {
		struct pkg *p;

		p = job->items[0]->pkg;
		if (p->type != PKG_REMOTE)
			continue;

		if (pkgdb_ensure_loaded(j->db, p, PKG_LOAD_FILES|PKG_LOAD_DIRS)
				== EPKG_FATAL) {
			j->need_fetch = true;
			break;
		}
	}

	if (j->solved == 1 && !j->need_fetch && j->type != PKG_JOBS_FETCH) {
		int rc;
		bool has_conflicts = false;
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
				break;
			}
		} while (j->conflicts_registered > 0);
	}

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
		struct pkg_manifest_key *keys)
{
	struct pkg *new, *old;
	struct pkg_job_request *req;
	char path[MAXPATHLEN], *target;
	int flags = 0;
	int retcode = EPKG_FATAL;

	old = ps->items[1] ? ps->items[1]->pkg : NULL;
	new = ps->items[0]->pkg;

	HASH_FIND_STR(j->request_add, new->uid, req);
	if (req != NULL && req->item->jp != NULL && req->item->jp->is_file) {
		/*
		 * We have package as a file, set special repository name
		 */
		target = req->item->jp->path;
		free(new->reponame);
		new->reponame = strdup("local file");
	}
	else {
		pkg_snprintf(path, sizeof(path), "%R", new);
		if (*path != '/')
			pkg_repo_cached_name(new, path, sizeof(path));
		target = path;
	}

	if (old != NULL)
		new->old_version = strdup(old->version);

	if ((j->flags & PKG_FLAG_FORCE) == PKG_FLAG_FORCE)
		flags |= PKG_ADD_FORCE;
	if ((j->flags & PKG_FLAG_NOSCRIPT) == PKG_FLAG_NOSCRIPT)
		flags |= PKG_ADD_NOSCRIPT;
	if ((j->flags & PKG_FLAG_FORCE_MISSING) == PKG_FLAG_FORCE_MISSING)
		flags |= PKG_ADD_FORCE_MISSING;
	flags |= PKG_ADD_UPGRADE;
	if (new->automatic || (j->flags & PKG_FLAG_AUTOMATIC) == PKG_FLAG_AUTOMATIC)
		flags |= PKG_ADD_AUTOMATIC;

#if 0
	if (old != NULL && !ps->already_deleted) {
		if ((retcode = pkg_delete(old, j->db, PKG_DELETE_UPGRADE)) != EPKG_OK) {
			pkgdb_transaction_rollback_sqlite(j->db->sqlite, "upgrade");
			goto cleanup;
		}
	}
#endif
	if (old != NULL)
		retcode = pkg_add_upgrade(j->db, target, flags, keys, NULL, new, old);
	else
		retcode = pkg_add_from_remote(j->db, target, flags, keys, NULL, new);

	if (retcode != EPKG_OK) {
		pkgdb_transaction_rollback_sqlite(j->db->sqlite, "upgrade");
		return (retcode);
	}

	return (EPKG_OK);
}

static int
pkg_jobs_execute(struct pkg_jobs *j)
{
	struct pkg *p = NULL;
	struct pkg_solved *ps;
	struct pkg_manifest_key *keys = NULL;
	int flags = 0;
	int retcode = EPKG_FATAL;
	bool handle_rc = false;

	if (j->flags & PKG_FLAG_SKIP_INSTALL)
		return (EPKG_OK);

	if ((j->flags & PKG_FLAG_FORCE) == PKG_FLAG_FORCE)
		flags |= PKG_DELETE_FORCE;

	if ((j->flags & PKG_FLAG_NOSCRIPT) == PKG_FLAG_NOSCRIPT)
		flags |= PKG_DELETE_NOSCRIPT;

	handle_rc = pkg_object_bool(pkg_config_get("HANDLE_RC_SCRIPTS"));

	retcode = pkgdb_upgrade_lock(j->db, PKGDB_LOCK_ADVISORY,
			PKGDB_LOCK_EXCLUSIVE);
	if (retcode != EPKG_OK)
		return (retcode);

	p = NULL;
	pkg_manifest_keys_new(&keys);
	/* Install */
	if ((retcode = pkgdb_transaction_begin_sqlite(j->db->sqlite, "upgrade")) !=
	    EPKG_OK)
		return (retcode);

	pkg_jobs_set_priorities(j);

	DL_FOREACH(j->jobs, ps) {
		switch (ps->type) {
		case PKG_SOLVED_DELETE:
		case PKG_SOLVED_UPGRADE_REMOVE:
			p = ps->items[0]->pkg;
			if (ps->type == PKG_SOLVED_DELETE &&
			    (strcmp(p->name, "pkg") == 0 ||
			    strcmp(p->name, "pkg-devel") == 0) &&
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
					j, handle_rc, keys);
			if (retcode != EPKG_OK)
				goto cleanup;
			break;
		case PKG_SOLVED_UPGRADE:
			retcode = pkg_jobs_handle_install(ps,
					j, handle_rc, keys);
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
	pkgdb_transaction_commit_sqlite(j->db->sqlite, "upgrade");
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
		if (j->need_fetch) {
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

						return (EPKG_CONFLICT);
					}
				}
				else {
					/* Not the first run, conflicts are resolved already */
					pkg_plugins_hook_run(pre, j, j->db);
					rc = pkg_jobs_execute(j);
				}
			}
		}
		else {
			pkg_plugins_hook_run(pre, j, j->db);
			rc = pkg_jobs_execute(j);
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
	bool mirror = (j->flags & PKG_FLAG_FETCH_MIRROR) ? true : false;

	
	if (j->destdir == NULL || !mirror)
		cachedir = pkg_object_string(pkg_config_get("PKG_CACHEDIR"));
	else
		cachedir = j->destdir;

	/* check for available size to fetch */
	DL_FOREACH(j->jobs, ps) {
		if (ps->type != PKG_SOLVED_DELETE && ps->type != PKG_SOLVED_UPGRADE_REMOVE) {
			p = ps->items[0]->pkg;
			if (p->type != PKG_REMOTE)
				continue;

			if (mirror) {
				snprintf(cachedpath, sizeof(cachedpath),
				   "%s/%s", cachedir, p->repopath);
			}
			else
				pkg_repo_cached_name(p, cachedpath, sizeof(cachedpath));

			if (stat(cachedpath, &st) == -1)
				dlsize += p->pkgsize;
			else
				dlsize += p->pkgsize - st.st_size;
		}
	}

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

	if (dlsize > ((int64_t)fs.f_bsize * (int64_t)fs.f_bavail)) {
		int64_t fsize = (int64_t)fs.f_bsize * (int64_t)fs.f_bavail;
		char dlsz[8], fsz[8];

		humanize_number(dlsz, sizeof(dlsz), dlsize, "B", HN_AUTOSCALE, 0);
		humanize_number(fsz, sizeof(fsz), fsize, "B", HN_AUTOSCALE, 0);
		pkg_emit_error("Not enough space in %s, needed %s available %s",
		    cachedir, dlsz, fsz);
		return (EPKG_FATAL);
	}

	if ((j->flags & PKG_FLAG_DRY_RUN) == PKG_FLAG_DRY_RUN)
		return (EPKG_OK); /* don't download anything */

	/* Fetch */
	DL_FOREACH(j->jobs, ps) {
		if (ps->type != PKG_SOLVED_DELETE
						&& ps->type != PKG_SOLVED_UPGRADE_REMOVE) {
			p = ps->items[0]->pkg;
			if (p->type != PKG_REMOTE)
				continue;

			if (mirror) {
				if (pkg_repo_mirror_package(p, cachedir) != EPKG_OK)
					return (EPKG_FATAL);
			}
			else {
				if (pkg_repo_fetch_package(p) != EPKG_OK)
					return (EPKG_FATAL);
			}
		}
	}

	return (EPKG_OK);
}

static int
pkg_jobs_check_conflicts(struct pkg_jobs *j)
{
	struct pkg_solved *ps;
	struct pkg *p = NULL;
	int ret = EPKG_OK, res, added = 0;

	pkg_emit_integritycheck_begin();
	j->conflicts_registered = 0;

	DL_FOREACH(j->jobs, ps) {
		if (ps->type == PKG_SOLVED_DELETE || ps->type == PKG_SOLVED_UPGRADE_REMOVE) {
			continue;
		}
		else {
			p = ps->items[0]->pkg;

			if (p->type == PKG_REMOTE)
				pkgdb_ensure_loaded(j->db, p, PKG_LOAD_FILES|PKG_LOAD_DIRS);
		}
		if ((res = pkg_conflicts_append_chain(ps->items[0], j)) != EPKG_OK)
			ret = res;
		else
			added ++;
	}

	pkg_debug(1, "check integrity for %d items added", added);

	pkg_emit_integritycheck_finished(j->conflicts_registered);
	if (j->conflicts_registered > 0)
		ret = EPKG_CONFLICT;

	return (ret);
}
