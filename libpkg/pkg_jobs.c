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

#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <errno.h>
#include <libutil.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <ctype.h>

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
	if (jp->pattern != NULL)
		free(jp->pattern);
	if (jp->path != NULL)
		free(jp->path);

	free(jp);
}

void
pkg_jobs_free(struct pkg_jobs *j)
{
	struct pkg_job_request *req, *tmp;

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

void
pkg_jobs_add_req(struct pkg_jobs *j, const char *uid,
		struct pkg_job_universe_item *item)
{
	struct pkg_job_request *req, *test, **head;

	if (!IS_DELETE(j)) {
		head = &j->request_add;
		assert(item->pkg->type != PKG_INSTALLED);
	}
	else {
		head = &j->request_delete;
		assert(item->pkg->type == PKG_INSTALLED);
	}

	HASH_FIND_PTR(*head, &item, test);

	if (test != NULL)
		return;

	req = calloc(1, sizeof (struct pkg_job_request));
	if (req == NULL) {
		pkg_emit_errno("malloc", "struct pkg_job_request");
		return;
	}
	req->item = item;

	HASH_ADD_PTR(*head, item, req);
}

static int
pkg_jobs_set_request_priority(struct pkg_jobs *j, struct pkg_solved *req)
{
	struct pkg_solved *treq;
	const char *uid;

	if (req->type == PKG_SOLVED_UPGRADE
			&& req->items[1]->pkg->conflicts != NULL) {
		/*
		 * We have an upgrade request that has some conflicting packages, therefore
		 * update priorities of local packages and try to update priorities of remote ones
		 */
		if (req->items[0]->priority == 0)
			pkg_jobs_update_conflict_priority(j->universe, req);

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
			j->count ++;
			req->already_deleted = true;
			pkg_get(treq->items[0]->pkg, PKG_UNIQUEID, &uid);
			pkg_debug(2, "split upgrade request for %s", uid);
			return (EPKG_CONFLICT);
		}
	}
	else if (req->type == PKG_SOLVED_DELETE) {
		if (req->items[0]->priority == 0)
			pkg_jobs_update_universe_priority(j->universe, req->items[0],
					PKG_PRIORITY_UPDATE_DELETE);
	}
	else {
		if (req->items[0]->priority == 0)
			pkg_jobs_update_universe_priority(j->universe, req->items[0],
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
	bool automatic;

	while (pkg_rdeps(p, &d) == EPKG_OK && ret) {
		unit = pkg_jobs_universe_find(j->universe, d->uid);
		if (unit != NULL) {
			pkg_get(unit->pkg, PKG_AUTOMATIC, &automatic);
			if (!automatic) {
				return (false);
			}
			npkg = unit->pkg;
		}
		else {
			npkg = pkg_jobs_universe_get_local(j->universe, d->uid,
					PKG_LOAD_BASIC|PKG_LOAD_RDEPS|PKG_LOAD_ANNOTATIONS);
			pkg_get(npkg, PKG_AUTOMATIC, &automatic);
			if (npkg == NULL)
				return (false);
			if (!automatic) {
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
	const char *uid = "pkg~ports-mgmt/pkg";
	pkg_flags old_flags;
	bool ret = false;

	/* Disable -f for pkg self-check, and restore at end. */
	old_flags = j->flags;
	j->flags &= ~(PKG_FLAG_FORCE|PKG_FLAG_RECURSIVE);

	/* determine local pkgng */
	p = pkg_jobs_universe_get_local(j->universe, uid, 0);

	if (p == NULL) {
		uid = "pkg~ports-mgmt/pkg-devel";
		p = pkg_jobs_universe_get_local(j->universe, uid, 0);
	}

	if (p == NULL) {
		uid = "pkg-devel~ports-mgmt/pkg-devel";
		p = pkg_jobs_universe_get_local(j->universe, uid, 0);
	}

	/* you are using git version skip */
	if (p == NULL) {
		ret = false;
		goto end;
	}

	pkg_jobs_universe_process(j->universe, p);

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
		bool force, struct pkg_job_universe_item **unit)
{
	struct pkg_job_universe_item *jit = NULL, *rjit = NULL;
	struct pkg_job_seen *seen;
	int rc = EPKG_FATAL;
	bool automatic;
	const char *uid, *digest;
	struct pkg *lp = NULL;

	pkg_get(rp, PKG_UNIQUEID, &uid, PKG_DIGEST, &digest);

	if (digest == NULL) {
		if (pkg_checksum_calculate(rp, j->db) != EPKG_OK) {
			return (EPKG_FATAL);
		}
		pkg_get(rp, PKG_DIGEST, &digest);
	}
	seen = pkg_jobs_universe_seen(j->universe, digest);
	if (seen != NULL) {
		if (!force) {
			/* Remote package is the same as local */
			pkg_debug(3, "already seen package %s-%s(%c) in the universe, do not add it again",
					uid, digest,
					seen->un->pkg->type == PKG_INSTALLED ? 'l' : 'r');
			jit = seen->un;
			do {
				if (jit->pkg->type != PKG_INSTALLED)
					break;
				jit = jit->prev;
			} while (jit != seen->un);

			if (jit->pkg->type != PKG_INSTALLED)
				pkg_jobs_add_req(j, uid, jit);

			return (EPKG_INSTALLED);
		}
		else {
			/*
			 * In forced mode we need to check whether we seen any upgrade
			 * candidates
			 */
			jit = seen->un;
			while (jit != NULL && jit->pkg->type == PKG_INSTALLED)
				jit = jit->next;

			if (jit == NULL) {
				/*
				 * No other remote candidates with this digest
				 */
				pkg_debug(3, "force add of %s to the universe", uid);
				pkg_jobs_universe_add_pkg(j->universe, rp, true, &jit);
				pkg_get(seen->un->pkg, PKG_AUTOMATIC, &automatic);
				pkg_set(rp, PKG_AUTOMATIC, automatic);
				rc = pkg_jobs_universe_process_item(j->universe, rp, NULL);

				if (rc == EPKG_OK) {
					if (unit != NULL)
						*unit = jit;

					pkg_jobs_add_req(j, uid, jit);
				}
			}
			else {
				struct pkg_job_request *jreq;

				pkg_debug(3, "already have the same package to reinstall: %s",
					uid);
				if (pkg_jobs_need_upgrade(rp, jit->pkg)) {
					pkg_debug(3, "force add of %s to the universe", uid);
					pkg_jobs_universe_add_pkg(j->universe, rp, true, &jit);
					pkg_get(seen->un->pkg, PKG_AUTOMATIC, &automatic);
					pkg_set(rp, PKG_AUTOMATIC, automatic);
					rc = pkg_jobs_universe_process_item(j->universe, rp, NULL);
				}
				/* But we can still need to add it to the request */
				HASH_FIND_PTR(j->request_add, jit, jreq);
				if (jreq == NULL)
					pkg_jobs_add_req(j, uid, jit);
			}
			if (unit != NULL)
				*unit = jit;

			return (EPKG_OK);
		}
	}
	else {
		jit = pkg_jobs_universe_find(j->universe, uid);
		if (jit == NULL) {
			if (j->type != PKG_JOBS_FETCH) {
				lp = pkg_jobs_universe_get_local(j->universe, uid, 0);
				if (lp != NULL)
					pkg_jobs_universe_process_item(j->universe, lp, &jit);
			}
		}

		/* We have a more recent package */
		if (jit != NULL) {
			LL_FOREACH(jit, rjit) {
				lp = rjit->pkg;

				if (!pkg_jobs_need_upgrade(rp, lp)) {
					/*
					 * We can have package from another repo in the
					 * universe, but if it is older than this one we just
					 * do not add it.
					 */
					if (rjit->pkg->type == PKG_INSTALLED) {
						if (!force)
							return (EPKG_INSTALLED);
					}
					else {
						pkg_debug(3, "already added newer package %s to the universe, "
							"do not add it again", uid);
						/* But we still need to check request */
						pkg_jobs_add_req(j, uid, rjit);
						return (EPKG_OK);
					}
				}
				else {
					/*
					 * New package is an upgrade, no need to continue
					 */
					jit = NULL;
					break;
				}
			}
			if (rjit == NULL && force) {
				pkg_debug(3, "force add of %s to the universe", uid);
				pkg_jobs_universe_add_pkg(j->universe, rp, true, &jit);
				pkg_get(jit->pkg, PKG_AUTOMATIC, &automatic);
				pkg_set(rp, PKG_AUTOMATIC, automatic);
			}
		}

		/* Add a package to request chain and populate universe */
		if (jit == NULL)
			rc = pkg_jobs_universe_process_item(j->universe, rp, &jit);
		else
			rc = pkg_jobs_universe_process_item(j->universe, rp, NULL);

		if (rc == EPKG_OK) {
			if (unit != NULL)
				*unit = jit;

			pkg_jobs_add_req(j, uid, jit);
		}
	}

	return (rc);
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
	const char *fuid;
	struct pkg_job_universe_item *unit;

	if ((it = pkgdb_repo_query(j->db, pattern, m, j->reponame)) == NULL)
		return (EPKG_FATAL);

	qmsg = sbuf_new_auto();

	while (it != NULL && pkgdb_it_next(it, &p, flags) == EPKG_OK) {
		pkg_get(p, PKG_UNIQUEID, &fuid);
		sbuf_printf(qmsg, "%s has no direct installation candidates, change it to "
				"%s? [Y/n]: ", uid, fuid);
		sbuf_finish(qmsg);
		if (pkg_emit_query_yesno(true, sbuf_data(qmsg))) {
			/* Change the origin of the local package */
			pkg_validate(p);
			unit = pkg_jobs_universe_find(j->universe, uid);
			if (unit != NULL)
				pkg_jobs_universe_change_uid(j->universe, unit, fuid,
					strlen(fuid), false);

			rc = pkg_jobs_process_remote_pkg(j, p, false, NULL);
			if (rc == EPKG_OK) {
				/* Avoid freeing */
				p = NULL;
			}
			break;
		}
		sbuf_reset(qmsg);
	}


	if (p != NULL)
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
	bool force = false, found = false;
	int rc = EPKG_FATAL;
	struct pkg_dep *rdep = NULL;
	unsigned flags = PKG_LOAD_BASIC|PKG_LOAD_OPTIONS|PKG_LOAD_DEPS|
			PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_SHLIBS_PROVIDED|
			PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;

	if (j->flags & PKG_FLAG_FORCE)
		force = true;

	if ((it = pkgdb_repo_query(j->db, pattern, m, j->reponame)) == NULL)
		rc = EPKG_FATAL;

	while (it != NULL && pkgdb_it_next(it, &p, flags) == EPKG_OK) {
		rc = pkg_jobs_process_remote_pkg(j, p, force, NULL);
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
		p = pkg_jobs_universe_get_local(j->universe, pattern, PKG_LOAD_BASIC|PKG_LOAD_RDEPS);
		if (p == NULL)
			return (EPKG_FATAL);

		pkg_jobs_universe_process(j->universe, p);

		while(pkg_rdeps(p, &rdep) == EPKG_OK) {
			struct pkg *rdep_package;

			rdep_package = pkg_jobs_universe_get_local(j->universe, rdep->uid,
					PKG_LOAD_BASIC);
			if (rdep_package != NULL) {
				pkg_jobs_universe_process(j->universe, rdep_package);
				/* It is not a top level package */
				return (EPKG_END);
			}
		}

		pkg_debug(2, "non-automatic package with pattern %s has not been found in "
				"remote repo", pattern);
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
pkg_jobs_find_remote_pattern(struct pkg_jobs *j, struct job_pattern *jp,
		bool *got_local)
{
	int rc = EPKG_FATAL;
	struct pkg *pkg = NULL;
	struct pkg_manifest_key *keys = NULL;
	struct pkg_job_universe_item *unit;
	struct job_pattern jfp;
	const char *pkgname;

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
		*got_local = false;
	}
	else {
		pkg_manifest_keys_new(&keys);
		if (pkg_open(&pkg, jp->path, keys, PKG_OPEN_MANIFEST_ONLY) != EPKG_OK)
			rc = EPKG_FATAL;
		else if (pkg_validate(pkg) == EPKG_OK) {
			if (j->type == PKG_JOBS_UPGRADE) {
				pkg_get(pkg, PKG_NAME, &pkgname);
				jfp.match = MATCH_EXACT;
				jfp.pattern = __DECONST(char *, pkgname);
				if (pkg_jobs_check_local_pkg (j, &jfp) != EPKG_OK) {
					pkg_emit_error("%s is not installed, therefore upgrade is impossible",
							jfp.pattern);
					pkg_manifest_keys_free(keys);
					return (EPKG_FATAL);
				}
			}
			pkg->type = PKG_FILE;
			rc = pkg_jobs_process_remote_pkg(j, pkg, j->flags & PKG_FLAG_FORCE,
				&unit);

			if (rc == EPKG_OK)
				unit->jp = jp;

			*got_local = true;
		}
		else {
			pkg_emit_error("cannot load %s: invalid format",
					jfp.pattern);
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
	const char *lversion, *rversion, *larch, *rarch, *reponame, *origin;
	const char *ldigest, *rdigest;
	struct pkg_option *lo = NULL, *ro = NULL;
	struct pkg_dep *ld = NULL, *rd = NULL;
	struct pkg_shlib *ls = NULL, *rs = NULL;
	struct pkg_conflict *lc = NULL, *rc = NULL;
	struct pkg_provide *lpr = NULL, *rpr = NULL;
	const ucl_object_t *an, *obj;

	/* If no local package, then rp is obviously need to be added */
	if (lp == NULL)
		return true;

	/* Do not upgrade locked packages */
	if (pkg_is_locked(lp)) {
		pkg_emit_locked(lp);
		return (false);
	}

	pkg_get(lp, PKG_VERSION, &lversion, PKG_ARCH, &larch, PKG_ORIGIN, &origin,
			PKG_DIGEST, &ldigest);
	pkg_get(rp, PKG_VERSION, &rversion, PKG_ARCH, &rarch, PKG_DIGEST, &rdigest);

	if (ldigest != NULL && rdigest != NULL &&
			strcmp(ldigest, rdigest) == 0) {
		/* Remote and local packages has the same digest, hence they are the same */
		return (false);
	}
	/*
	 * XXX: for a remote package we also need to check whether options
	 * are compatible.
	 */
	ret = pkg_version_cmp(lversion, rversion);
	if (ret > 0)
		return (false);
	else if (ret < 0)
		return (true);

	/* Check reponame */
	pkg_get(rp, PKG_REPONAME, &reponame);
	pkg_get(lp, PKG_ANNOTATIONS, &obj);
	an = pkg_object_find(obj, "repository");
	if (an != NULL)  {
		if (strcmp(reponame, ucl_object_tostring(an)) != 0) {
			/*
			 * If we have packages from some different repo, then
			 * we should not try to detect options changed and so on,
			 * basically, we need to check a version only and suggest upgrade
			 */
			pkg_debug(2, "package %s was installed from repo %s, so we ignore "
					"the same version of %s in %s repository", origin,
					ucl_object_tostring(an), origin, reponame);
			return (false);
		}
	}
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
			if (strcmp(pkg_conflict_uniqueid(rc),
					pkg_conflict_uniqueid(lc)) != 0) {
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
				pkg_debug(1, "shlib changed %s->%s", pkg_shlib_name(ls), pkg_shlib_name(rs));
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
	const char *uid;
	bool automatic;

	HASH_ITER(hh, j->universe->items, unit, utmp) {
		if (unit->next == NULL) {
			/*
			 * For packages that are alone in the installation list
			 * we search them in the corresponding request
			 */
			pkg_get(unit->pkg, PKG_UNIQUEID, &uid);
			HASH_FIND_PTR(j->request_add, &unit, req);
			if (req == NULL && unit->pkg->type != PKG_INSTALLED) {
				automatic = 1;
				pkg_debug(2, "set automatic flag for %s", uid);
				pkg_set(unit->pkg, PKG_AUTOMATIC, automatic);
			}
			else {
				if (j->type == PKG_JOBS_INSTALL) {
					automatic = 0;
					pkg_set(unit->pkg, PKG_AUTOMATIC, automatic);
				}
			}
		}
		else {
			/*
			 * For packages that are in the conflict chain we need to inherit
			 * automatic flag from the local package
			 */
			local = NULL;
			automatic = 0;
			LL_FOREACH(unit, cur) {
				if (cur->pkg->type == PKG_INSTALLED) {
					local = cur;
					pkg_get(local->pkg, PKG_AUTOMATIC, &automatic);
					break;
				}
			}
			if (local != NULL)
				LL_FOREACH(unit, cur)
					if (cur->pkg->type != PKG_INSTALLED)
						pkg_set(cur->pkg, PKG_AUTOMATIC, automatic);
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
	const char *uid;
	struct pkg *pkg = item->pkg;

	pkg_get(pkg, PKG_UNIQUEID, &uid);
	if (rec_level > 128) {
		pkg_debug(2, "cannot find deinstall request after 128 iterations for %s,"
				"circular dependency maybe", uid);
		return (NULL);
	}

	HASH_FIND_PTR(j->request_delete, &item, found);
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
	struct sbuf *reason = sbuf_new_auto();
	struct pkg_solved *sit;
	struct pkg_job_request *jreq;
	struct pkg *req_pkg, *pkg;
	const char *name, *version;

	LL_FOREACH(j->jobs, sit) {
		jreq = pkg_jobs_find_deinstall_request(sit->items[0], j, 0);
		if (jreq != NULL && jreq->item != sit->items[0]) {
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
	char *uid;
	struct pkg_job_universe_item *unit;

	HASH_ITER(hh, j->patterns, jp, jtmp) {
		if ((it = pkgdb_query(j->db, jp->pattern, jp->match)) == NULL)
			return (EPKG_FATAL);

		while (pkgdb_it_next(it, &pkg,
				PKG_LOAD_BASIC|PKG_LOAD_RDEPS|PKG_LOAD_DEPS|PKG_LOAD_ANNOTATIONS) == EPKG_OK) {
			// Check if the pkg is locked
			pkg_get(pkg, PKG_UNIQUEID, &uid);
			pkg_jobs_universe_process_item(j->universe, pkg, &unit);
			if(pkg_is_locked(pkg)) {
				pkg_emit_locked(pkg);
			}
			else {
				pkg_get(pkg, PKG_UNIQUEID, &uid);
				pkg_jobs_add_req(j, uid, unit);
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
	char *uid;
	struct pkg_job_universe_item *unit;

	if ((it = pkgdb_query(j->db, " WHERE automatic=1 ", MATCH_CONDITION)) == NULL)
		return (EPKG_FATAL);

	while (pkgdb_it_next(it, &pkg,
			PKG_LOAD_BASIC|PKG_LOAD_RDEPS|PKG_LOAD_DEPS|PKG_LOAD_ANNOTATIONS)
			== EPKG_OK) {
		// Check if the pkg is locked
		pkg_get(pkg, PKG_UNIQUEID, &uid);
		unit = pkg_jobs_universe_find(j->universe, uid);
		if (unit == NULL) {
			pkg_jobs_universe_process_item(j->universe, pkg, &unit);
			if(pkg_is_locked(pkg)) {
				pkg_emit_locked(pkg);
			}
			else if (pkg_jobs_test_automatic(j, pkg)) {
				pkg_debug(2, "removing %s as it has no non-automatic reverse depends",
						uid);
				pkg_jobs_add_req(j, uid, unit);
			}
		}
		else {
			if(pkg_is_locked(unit->pkg)) {
				pkg_emit_locked(unit->pkg);
			}
			else if (pkg_jobs_test_automatic(j, unit->pkg)) {
				pkg_debug(2, "removing %s as it has no non-automatic reverse depends",
						uid);
				pkg_jobs_add_req(j, uid, unit);
			}

			pkg_free(pkg);
		}
		pkg = NULL;
	}
	pkgdb_it_free(it);

	j->solved = true;

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
	int64_t id;

	pkg_get(pkg, PKG_ROWID, &id);
	n = malloc(sizeof(*n));
	if (n == NULL) {
		pkg_emit_errno("malloc", "pkg_jobs_install_candidate");
		return (NULL);
	}
	n->id = id;
	return (n);
}

static bool
pkg_jobs_check_remote_candidate(struct pkg_jobs *j, struct pkg *pkg)
{
	const char *digest, *uid;
	struct pkgdb_it *it;
	struct pkg *p = NULL;

	pkg_get(pkg, PKG_DIGEST, &digest, PKG_UNIQUEID, &uid);
	/* If we have no digest, we need to check this package */
	if (digest == NULL || digest[0] == '\0')
		return (true);

	it = pkgdb_repo_query(j->db, uid, MATCH_EXACT, j->reponame);
	if (it != NULL) {
		/*
		 * If we have the same package in a remote repo, it is not an
		 * installation candidate
		 */
		int npkg = 0;

		while (pkgdb_it_next(it, &p, PKG_LOAD_BASIC) == EPKG_OK) {
			const char *rdigest;

			pkg_get(p, PKG_DIGEST, &rdigest);
			/*
			 * Check package with the same uid and explore whether digest
			 * has been changed
			 */
			if (strcmp(rdigest, digest) != 0)
				npkg ++;
		}

		pkg_free(p);
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
	}
	pkg_free(pkg);
	pkgdb_it_free(it);

	return (candidates);
}

static int
jobs_solve_install_upgrade(struct pkg_jobs *j)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	char *uid;
	char sqlbuf[256];
	bool got_local;
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
					/* TODO: use repository priority here */
					pkg_get(pkg, PKG_UNIQUEID, &uid);
					/* Do not test we ignore what doesn't exists remotely */
					pkg_jobs_find_upgrade(j, uid, MATCH_EXACT);
				}
				pkg_free(pkg);
				pkgdb_it_free(it);
			}
			pkg_emit_progress_tick(jcount, jcount);
			LL_FREE(candidates, free);
		}
		else {
			HASH_ITER(hh, j->patterns, jp, jtmp) {
				if (pkg_jobs_find_remote_pattern(j, jp, &got_local) == EPKG_FATAL) {
					pkg_emit_error("No packages available to %s matching '%s' "
							"have been found in the "
							"repositories",
							(j->type == PKG_JOBS_UPGRADE) ? "upgrade" : "install",
							jp->pattern);
					return (EPKG_FATAL);
				}
			}
			if (got_local) {
				/*
				 * Need to iterate request one more time to recurse depends
				 */
				HASH_ITER(hh, j->request_add, req, rtmp) {
					pkg_jobs_universe_process(j->universe, req->item->pkg);
				}
			}
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
	char *uid;
	unsigned flag = PKG_LOAD_BASIC|PKG_LOAD_ANNOTATIONS;

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
				pkg_get(pkg, PKG_UNIQUEID, &uid);
				/* Do not test we ignore what doesn't exists remotely */
				pkg_jobs_find_upgrade(j, uid, MATCH_EXACT);
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
	}

	j->solved ++;

	return (EPKG_OK);
}

static void
pkg_jobs_apply_replacements(struct pkg_jobs *j)
{
	struct pkg_job_replace *r;
	static const char sql[] = ""
		"UPDATE packages SET name=SPLIT_UID('name', ?1), "
		"origin=SPLIT_UID('origin', ?1) WHERE "
		"name=SPLIT_UID('name', ?2) AND "
		"origin=SPLIT_UID('origin', ?2);";
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

	if (j->solved == 1 && !j->need_fetch) {
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
	const char *pkguid, *oldversion = NULL;
	char path[MAXPATHLEN], *target;
	bool automatic, upgrade = false;
	int flags = 0;
	int retcode = EPKG_FATAL;

	old = ps->items[1] ? ps->items[1]->pkg : NULL;
	new = ps->items[0]->pkg;

	pkg_get(new, PKG_UNIQUEID, &pkguid, PKG_AUTOMATIC, &automatic);
	if (old != NULL) {
		pkg_get(old, PKG_VERSION, &oldversion);
		upgrade = true;
	}

	if (ps->items[0]->jp != NULL && ps->items[0]->jp->is_file) {
		/*
		 * We have package as a file, set special repository name
		 */
		target = ps->items[0]->jp->path;
		pkg_set(new, PKG_REPONAME, "local file");
	}
	else {
		pkg_snprintf(path, sizeof(path), "%R", new);
		if (*path != '/')
			pkg_repo_cached_name(new, path, sizeof(path));
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
	if (automatic || (j->flags & PKG_FLAG_AUTOMATIC) == PKG_FLAG_AUTOMATIC)
		flags |= PKG_ADD_AUTOMATIC;

#if 0
	if (old != NULL && !ps->already_deleted) {
		if ((retcode = pkg_delete(old, j->db, PKG_DELETE_UPGRADE)) != EPKG_OK) {
			pkgdb_transaction_rollback(j->db->sqlite, "upgrade");
			goto cleanup;
		}
	}
#endif
	if (upgrade)
		retcode = pkg_add_upgrade(j->db, target, flags, keys, NULL, new, old);
	else
		retcode = pkg_add_from_remote(j->db, target, flags, keys, NULL, new);

	if (retcode != EPKG_OK) {
		pkgdb_transaction_rollback(j->db->sqlite, "upgrade");
		goto cleanup;
	}

	if (upgrade)
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
	const char *name;
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
	pkgdb_transaction_begin(j->db->sqlite, "upgrade");

	pkg_jobs_set_priorities(j);

	DL_FOREACH(j->jobs, ps) {
		switch (ps->type) {
		case PKG_SOLVED_DELETE:
		case PKG_SOLVED_UPGRADE_REMOVE:
			p = ps->items[0]->pkg;
			pkg_get(p, PKG_NAME, &name);
			if (ps->type == PKG_SOLVED_DELETE &&
			    (strcmp(name, "pkg") == 0 ||
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
	const char *cachedir = NULL, *repopath;
	char cachedpath[MAXPATHLEN];
	bool mirror = (j->flags & PKG_FLAG_FETCH_MIRROR) ? true : false;

	
	if (j->destdir == NULL || !mirror)
		cachedir = pkg_object_string(pkg_config_get("PKG_CACHEDIR"));
	else
		cachedir = j->destdir;

	/* check for available size to fetch */
	DL_FOREACH(j->jobs, ps) {
		if (ps->type != PKG_SOLVED_DELETE && ps->type != PKG_SOLVED_UPGRADE_REMOVE) {
			int64_t pkgsize;

			p = ps->items[0]->pkg;
			if (p->type != PKG_REMOTE)
				continue;

			pkg_get(p, PKG_PKGSIZE, &pkgsize, PKG_REPOPATH, &repopath);
			if (mirror) {
				snprintf(cachedpath, sizeof(cachedpath), "%s/%s", cachedir,
					repopath);
			}
			else
				pkg_repo_cached_name(p, cachedpath, sizeof(cachedpath));

			if (stat(cachedpath, &st) == -1)
				dlsize += pkgsize;
			else
				dlsize += pkgsize - st.st_size;
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

	DL_FOREACH(j->jobs, ps) {
		if (ps->type == PKG_SOLVED_DELETE || ps->type == PKG_SOLVED_UPGRADE_REMOVE) {
			continue;
		}
		else {
			p = ps->items[0]->pkg;

			if (p->type == PKG_REMOTE)
				pkgdb_ensure_loaded(j->db, p, PKG_LOAD_FILES|PKG_LOAD_DIRS);
			else if (p->type != PKG_FILE)
				continue;
		}
		if ((res = pkg_conflicts_append_pkg(p, j)) != EPKG_OK)
			ret = res;
		else
			added ++;

	}

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
