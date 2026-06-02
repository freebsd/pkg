/*-
 * Copyright (c) 2011-2026 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2013-2016 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2023 Serenity Cyber Security, LLC
 *                    Author: Gleb Popov <arrowd@FreeBSD.org>
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

#define dbg(x, ...) pkg_dbg(PKG_DBG_JOBS, x, __VA_ARGS__)

#include <bsd_compat.h>

#include <sys/param.h>
#include <sys/mount.h>
#if __has_include(<sys/sysctl.h>)
#include <sys/sysctl.h>
#endif
#include <sys/types.h>
#include <sys/statvfs.h>

#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <errno.h>
#if __has_include(<libutil.h>)
#include <libutil.h>
#endif
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <ctype.h>

#include "pkg.h"
#include <xstring.h>
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/pkg_jobs.h"

extern struct pkg_ctx ctx;

static int _pkg_is_installed(struct pkg_jobs *j, const char *pattern,
    match_t match);
static int pkg_jobs_find_upgrade(struct pkg_jobs *j, const char *pattern, match_t m);
static int pkg_jobs_fetch(struct pkg_jobs *j);
static bool new_pkg_version(struct pkg_jobs *j);
static int pkg_jobs_check_conflicts(struct pkg_jobs *j);
typedef vec_t(int64_t) candidates_t;

int
pkg_jobs_new(struct pkg_jobs **j, pkg_jobs_t t, struct pkgdb *db)
{
	assert(db != NULL);

	*j = xcalloc(1, sizeof(struct pkg_jobs));
	(*j)->universe = pkg_jobs_universe_new(*j);
	(*j)->db = db;
	(*j)->type = t;
	(*j)->solved = false;
	(*j)->pinning = true;
	(*j)->flags = PKG_FLAG_NONE;
	pkg_deferred_rc_init(&(*j)->rc);
	(*j)->conservative = pkg_object_bool(pkg_config_get("CONSERVATIVE_UPGRADE"));
	(*j)->triggers.dfd = -1;

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
	c_charv_t idents = vec_init();
	if (ident != NULL)
		vec_push(&idents, ident);
	return (pkg_jobs_set_repositories(j, &idents));
}

int
pkg_jobs_set_repositories(struct pkg_jobs *j, c_charv_t *idents)
{
	int ret = EPKG_OK;
	if (idents == NULL)
		return (EPKG_OK);
	for (size_t i = 0; i < idents->len; i++) {
		if ((pkg_repo_find(idents->d[i])) == NULL) {
			pkg_emit_error("Unknown repository: %s", idents->d[i]);
			ret = EPKG_FATAL;
		}
	}
	if (ret == EPKG_FATAL)
		return (ret);

	j->reponames = idents;

	return (ret);
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
}

void
pkg_jobs_request_free(struct pkg_job_request *req)
{
	if (req != NULL) {
		vec_free(&req->items);
		free(req);
	}
}

void
pkg_jobs_free(struct pkg_jobs *j)
{
	pkghash_it it;

	if (j == NULL)
		return;

	it = pkghash_iterator(j->request_add);
	while (pkghash_next(&it))
		pkg_jobs_request_free(it.value);
	pkghash_destroy(j->request_add);
	j->request_add = NULL;

	it = pkghash_iterator(j->request_delete);
	while (pkghash_next(&it))
		pkg_jobs_request_free(it.value);
	pkghash_destroy(j->request_delete);
	j->request_delete = NULL;

	pkg_jobs_universe_free(j->universe);
	vec_free_and_free(&j->jobs, free);
	vec_foreach(j->patterns, _i)
		pkg_jobs_pattern_free(&j->patterns.d[_i]);
	vec_free(&j->patterns);
	if (j->triggers.cleanup != NULL) {
		vec_autofree(j->triggers.cleanup);
		free(j->triggers.cleanup);
	}
	if (j->triggers.dfd != -1)
		close(j->triggers.dfd);
	if (j->triggers.schema != NULL)
		ucl_object_unref(j->triggers.schema);
	pkg_deferred_rc_free(&j->rc);
	pkghash_destroy(j->orphaned);
	pkghash_destroy(j->notorphaned);
	vec_autofree(&j->system_shlibs);
	pkg_conflicts_free(j);
	vec_free_and_free(&j->lockedpkgs, pkg_free);
	free(j);
}

static bool
pkg_jobs_maybe_match_url(struct job_pattern *jp, const char *pattern)
{
	char path[MAXPATHLEN];
	const char *name;

	if (strncmp(pattern, "http://", 7) != 0 &&
	    strncmp(pattern, "https://", 8) != 0 &&
	    strncmp(pattern, "file://", 7) != 0)
		return (false);

	if (!str_ends_with(pattern, ".pkg"))
		return (false);

	name = strrchr(pattern, '/');
	if (name == NULL)
		name = pattern;
	else
		name++;

	snprintf(path, sizeof(path), "%s/%s.XXXXX",
	    getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", name);

	if (pkg_fetch_file(NULL, pattern, path, 0, 0, 0) != EPKG_OK) {
		pkg_emit_error("Failed to fetch package from '%s'", pattern);
		return (false);
	}

	dbg(2, "Fetched URL to file: %s", path);
	jp->flags |= PKG_PATTERN_FLAG_FILE;
	jp->path = xstrdup(path);
	/* Use just the filename (without .pkg extension) as the pattern */
	size_t len = strlen(name) - strlen(".pkg") + 1;
	jp->pattern = xmalloc(len);
	strlcpy(jp->pattern, name, len);

	return (true);
}

static bool
pkg_jobs_maybe_match_file(struct job_pattern *jp, const char *pattern)
{
	const char *dot_pos;
	char *pkg_path;

	assert(jp != NULL);
	assert(pattern != NULL);

	if (pkg_jobs_maybe_match_url(jp, pattern))
		return (true);

	dot_pos = strrchr(pattern, '.');
	if (dot_pos != NULL) {
		/*
		 * Compare suffix with .txz or .tbz
		 */
		dot_pos ++;
		if (STREQ(dot_pos, "pkg") ||
		    STREQ(dot_pos, "tzst") ||
		    STREQ(dot_pos, "txz") ||
		    STREQ(dot_pos, "tbz") ||
		    STREQ(dot_pos, "tgz") ||
		    STREQ(dot_pos, "tar")) {
			if ((pkg_path = realpath(pattern, NULL)) != NULL) {
				/* Dot pos is one character after the dot */
				int len = dot_pos - pattern;

				dbg(2, "Adding file: %s", pattern);
				jp->flags |= PKG_PATTERN_FLAG_FILE;
				jp->path = pkg_path;
				jp->pattern = xmalloc(len);
				strlcpy(jp->pattern, pattern, len);

				return (true);
			}
		}
	}
	else if (STREQ(pattern, "-")) {
		/*
		 * Read package from stdin
		 */
		jp->flags = PKG_PATTERN_FLAG_FILE;
		jp->path = xstrdup(pattern);
		jp->pattern = xstrdup(pattern);
	}

	return (false);
}

int
pkg_jobs_add(struct pkg_jobs *j, match_t match, char **argv, int argc)
{
	int i = 0;

	if (j->solved) {
		pkg_emit_error("The job has already been solved. "
		    "Unable to append new elements");
		return (EPKG_FATAL);
	}

	for (i = 0; i < argc; i++) {
		struct job_pattern jp = { 0 };
		if (j->type == PKG_JOBS_DEINSTALL ||
		    !pkg_jobs_maybe_match_file(&jp, argv[i])) {
			jp.pattern = xstrdup(argv[i]);
			jp.match = match;
		}
		vec_push(&j->patterns, jp);
	}

	if (argc == 0 && match == MATCH_ALL) {
		struct job_pattern jp = { .match = match };
		vec_push(&j->patterns, jp);
	}

	return (EPKG_OK);
}

bool
pkg_jobs_iter(struct pkg_jobs *j, void **iter,
				struct pkg **new, struct pkg **old,
				int *type)
{
	struct pkg_solved *s;
	struct {
		pkg_solved_list *list;
		size_t pos;
	} *t;
	t = *iter;
	if (*iter == NULL) {
		t = xcalloc(1, sizeof(*t));
		*iter = t;
	} else if (t->pos >= t->list->len) {
			free(t);
			return (false);
	}

	if (j->jobs.len == 0) {
		free(t);
		*iter = NULL;
		return (false);
	}
	if (t->list == NULL) {
		t->list = &j->jobs;
		t->pos = 0;
	}
	s = t->list->d[t->pos++];
	*new = s->items[0]->pkg;
	*old = s->items[1] ? s->items[1]->pkg : NULL;
	*type = s->type;
	return (true);
}

static struct pkg_job_request_item*
pkg_jobs_add_req_from_universe(pkghash **head, universe_itemv_t *uv,
    bool local, bool automatic)
{
	struct pkg_job_request *req;
	bool new_req = false;

	assert(uv != NULL && uv->len > 0);
	req = pkghash_get_value(*head, uv->d[0]->pkg->uid);

	if (req == NULL) {
		req = xcalloc(1, sizeof(*req));
		new_req = true;
		req->automatic = automatic;
		dbg(4, "add new uid %s to the request", uv->d[0]->pkg->uid);
	}
	else {
		/* Request already exists for this uid, skip adding duplicates */
		return (&req->items.d[0]);
	}

	vec_foreach(*uv, _i) {
		struct pkg_job_universe_item *uit = uv->d[_i];
		if ((uit->pkg->type == PKG_INSTALLED && local) ||
				(uit->pkg->type != PKG_INSTALLED && !local)) {
			vec_push(&req->items, ((struct pkg_job_request_item){
			    .pkg = uit->pkg, .unit = uit }));
		}
	}

	if (new_req) {
		if (req->items.len > 0) {
			pkghash_safe_add(*head, uv->d[0]->pkg->uid, req, NULL);
		}
		else {
			free(req);
			return (NULL);
		}
	}

	return (&req->items.d[0]);
}

static struct pkg_job_request_item*
pkg_jobs_add_req(struct pkg_jobs *j, struct pkg *pkg)
{
	pkghash **head;
	struct pkg_job_request *req;
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

	dbg(4, "add package %s-%s to the request", pkg->name,
			pkg->version);
	rc = pkg_jobs_universe_add_pkg(j->universe, pkg, &un);

	if (rc == EPKG_END) {
		/*
		 * This means that we have a package in the universe with the same
		 * digest. In turn, that means that two upgrade candidates are equal,
		 * we thus won't do anything with this item, as it is definitely useless
		 */
		req = pkghash_get_value(*head, pkg->uid);
		if (req != NULL) {
			vec_foreach(req->items, _ri) {
				if (req->items.d[_ri].unit == un)
					return (&req->items.d[_ri]);
			}
		}
		else {
			/*
			 * We need to add request chain from the universe chain
			 */
			universe_itemv_t *uv = pkg_jobs_universe_find(j->universe, pkg->uid);
			if (uv != NULL)
				return (pkg_jobs_add_req_from_universe(head, uv, IS_DELETE(j), false));
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

	req = pkghash_get_value(*head, pkg->uid);

	if (req == NULL) {
		/* Allocate new unique request item */
		req = xcalloc(1, sizeof(*req));
		pkghash_safe_add(*head, pkg->uid, req, NULL);
	}

	/* Append candidate to the list of candidates */
	vec_push(&req->items, ((struct pkg_job_request_item){
	    .pkg = pkg, .unit = un }));

	return (&req->items.d[req->items.len - 1]);
}

static bool
append_to_del_request(struct pkg_jobs *j, pkgs_t *to_process, const char *uid, const char *reqname)
{
	struct pkg *p;

	p = pkg_jobs_universe_get_local(j->universe, uid, 0);
	if (p == NULL)
		return (true);
	if (p->locked) {
		pkg_emit_error("%s is locked cannot delete %s", p->name,
		   reqname);
		return (false);
	}
	vec_push(to_process, p);
	return (true);
}

static bool
delete_process_provides(struct pkg_jobs *j, struct pkg *lp, const char *provide,
    struct pkgdb_it *(*provideq)(struct pkgdb *db, const char *req),
    struct pkgdb_it *(*requireq)(struct pkgdb *db, const char *req),
    pkgs_t *to_process)
{
	struct pkgdb_it *lit, *rit;
	struct pkg *pkg;
	struct pkg_job_request *req;
	bool ret = true;

	/* check for pkgbase shlibs and provides */
	if (charv_search(&j->system_shlibs, provide) != NULL)
		return (ret);
	/* if something else to provide the same thing we can safely delete */
	lit = provideq(j->db, provide);
	if (lit == NULL)
		return (ret);
	pkg = NULL;
	while (pkgdb_it_next(lit, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		/* skip myself */
		if (STREQ(pkg->uid, lp->uid)) {
			pkg_free(pkg);
			pkg = NULL;
			continue;
		}
		req = pkghash_get_value(j->request_delete, pkg->uid);
		/*
		 * skip already processed provides
		 * if N packages providing the same "provide"
		 * are in the request delete they needs to be
		 * counted as to be removed and then if no
		 * packages are left providing the provide are
		 * left after the removal of those packages
		 * cascade.
		 */
		if (req != NULL && req->processed) {
			pkg_free(pkg);
			pkg = NULL;
			continue;
		}

		pkgdb_it_free(lit);
		pkg_free(pkg);
		return (ret);
	}
	pkgdb_it_free(lit);
	rit = requireq(j->db, provide);
	if (rit == NULL)
		return (ret);

	pkg = NULL;
	while (pkgdb_it_next(rit, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		if (!append_to_del_request(j, to_process,
		    pkg->uid, lp->name))
			ret = false;
		pkg_free(pkg);
		pkg = NULL;
	}
	pkgdb_it_free(rit);
	return (ret);
}

/*
 * For delete request we merely check rdeps and force flag
 */
static int
pkg_jobs_process_delete_request(struct pkg_jobs *j)
{
	bool force = j->flags & PKG_FLAG_FORCE;
	struct pkg_job_request *req;
	struct pkg_dep *d = NULL;
	struct pkg *lp;
	int rc = EPKG_OK;
	pkgs_t to_process = vec_init();
	pkghash_it it;

	if (j->type == PKG_JOBS_DEINSTALL && force &&
	    (j->flags & PKG_FLAG_RECURSIVE) == 0)
		return (EPKG_OK);

	/*
	 * Need to add also all reverse deps here
	 */
	it = pkghash_iterator(j->request_delete);
	while (pkghash_next(&it)) {
		req = it.value;
		if (req->processed)
			continue;
		req->processed = true;
		lp = req->items.d[0].pkg;
		d = NULL;
		while (pkg_rdeps(lp, &d) == EPKG_OK) {
			if (!append_to_del_request(j, &to_process, d->uid,
			    lp->name))
				rc = EPKG_FATAL;
		}

		vec_foreach(lp->provides, i) {
			if (!delete_process_provides(j, lp, lp->provides.d[i],
			    pkgdb_query_provide, pkgdb_query_require,
			    &to_process))
				rc = EPKG_FATAL;
		}

		vec_foreach(lp->shlibs_provided, i) {
			if (!delete_process_provides(j, lp, lp->shlibs_provided.d[i],
			    pkgdb_query_shlib_provide,
			    pkgdb_query_shlib_require, &to_process))
				rc = EPKG_FATAL;
		}
	}

	if (rc == EPKG_FATAL)
		return (rc);

	vec_foreach(to_process, pit) {
		lp = to_process.d[pit];
		if (pkg_jobs_add_req(j, lp) == NULL) {
			vec_free(&to_process);
			return (EPKG_FATAL);
		}
	}

	if (vec_len(&to_process) > 0)
		rc = pkg_jobs_process_delete_request(j);
	vec_free(&to_process);

	return (rc);
}

static bool pkg_jobs_test_automatic(struct pkg_jobs *j, struct pkg *p);

static bool
_is_orphaned(struct pkg_jobs *j, const char *uid)
{
	universe_itemv_t *uv;
	struct pkg *npkg;

	if (pkghash_get(j->notorphaned, uid) != NULL)
		return (false);
	uv = pkg_jobs_universe_find(j->universe, uid);
	if (uv != NULL) {
		if (!uv->d[0]->pkg->automatic || uv->d[0]->pkg->vital)
			return (false);
		npkg = uv->d[0]->pkg;
	} else {
		npkg = pkg_jobs_universe_get_local(j->universe, uid,
		    PKG_LOAD_BASIC|PKG_LOAD_RDEPS|PKG_LOAD_ANNOTATIONS|
		    PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_REQUIRES);
		if (npkg == NULL)
			return (false);
		if (!npkg->automatic || npkg->vital) {
			pkg_free(npkg);
			return (false);
		}
		if (pkg_jobs_universe_process(j->universe, npkg) != EPKG_OK)
			return (false);
	}

	if (!pkg_jobs_test_automatic(j, npkg))
		return (false);

	return (true);
}

static bool
is_orphaned(struct pkg_jobs *j, const char *uid)
{
	if (pkghash_get(j->orphaned, uid) != NULL)
		return (true);
	if (pkghash_get(j->notorphaned, uid) != NULL)
		return (false);
	/*
	 * Optimistically mark as orphaned before evaluating to break
	 * infinite recursion when packages form dependency cycles
	 * (e.g. A depends on B explicitly while B requires a shlib
	 * provided by A). If _is_orphaned() returns false, we correct
	 * the entry below.
	 */
	pkghash_safe_add(j->orphaned, uid, NULL, NULL);
	if (_is_orphaned(j, uid))
		return (true);
	pkghash_del(j->orphaned, uid);
	pkghash_safe_add(j->notorphaned, uid, NULL, NULL);
	return (false);
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
	struct pkg *npkg = NULL;
	struct pkgdb_it *it;

	while (pkg_rdeps(p, &d) == EPKG_OK) {
		if (!is_orphaned(j, d->uid))
			return (false);
	}

	vec_foreach(p->provides, i) {
		it = pkgdb_query_require(j->db, p->provides.d[i]);
		if (it == NULL)
			continue;
		npkg = NULL;
		while (pkgdb_it_next(it, &npkg, PKG_LOAD_BASIC) == EPKG_OK) {
			if (!is_orphaned(j, npkg->uid)) {
				pkgdb_it_free(it);
				pkg_free(npkg);
				return (false);
			}
		}
		pkgdb_it_free(it);
	}

	vec_foreach(p->shlibs_provided, i) {
		it = pkgdb_query_shlib_require(j->db, p->shlibs_provided.d[i]);
		if (it == NULL)
			continue;
		npkg = NULL;
		while (pkgdb_it_next(it, &npkg, PKG_LOAD_BASIC) == EPKG_OK) {
			if (!is_orphaned(j, npkg->uid)) {
				pkgdb_it_free(it);
				pkg_free(npkg);
				return (false);
			}
		}
		pkgdb_it_free(it);
	}
	pkg_free(npkg);

	return (true);
}



static bool
new_pkg_version(struct pkg_jobs *j)
{
	struct pkg *p;
	const char *uid = "pkg";
	pkg_flags old_flags;
	bool ret = false;
	universe_itemv_t *uv;

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
	if (pkg_jobs_find_upgrade(j, uid, MATCH_INTERNAL) == EPKG_OK) {
		/*
		 * Now we can have *potential* upgrades, but we can have a situation,
		 * when our upgrade candidate comes from another repo
		 */
		uv = pkg_jobs_universe_find(j->universe, uid);

		if (uv) {
			vec_foreach(*uv, _i) {
				struct pkg_job_universe_item *cit = uv->d[_i];
				if (pkg_version_change_between (cit->pkg, p) == PKG_UPGRADE) {
					/* We really have newer version which is not installed */
					ret = true;
					break;
				}
			}
		}
	}

end:
	j->flags = old_flags;
	pkg_free(p);

	return (ret);
}

static int
pkg_jobs_process_remote_pkg(struct pkg_jobs *j, struct pkg *rp,
	struct pkg_job_request_item **req, int with_version)
{
	universe_itemv_t *uv;
	struct pkg_job_request_item *nrit = NULL;
	struct pkg *lp = NULL;
	struct pkg_dep *rdep = NULL;

	if (rp->digest == NULL) {
		if (pkg_checksum_calculate(rp, j->db, false, true, false) != EPKG_OK) {
			return (EPKG_FATAL);
		}
	}
	if (j->type != PKG_JOBS_FETCH) {
		lp = pkg_jobs_universe_get_local(j->universe, rp->uid, 0);
		if (lp && lp->locked) {
			pkg_emit_locked(lp);
			if (!pkg_in_universe(j->universe, lp))
				pkg_free(lp);
			return (EPKG_LOCKED);
		}
	}

	uv = pkg_jobs_universe_get_upgrade_candidates(j->universe, rp->uid, lp,
		j->flags & PKG_FLAG_FORCE,
		with_version != 0 ? rp->version : NULL);

	if (uv != NULL) {
		nrit = pkg_jobs_add_req_from_universe(&j->request_add, uv, false, false);

		if (req != NULL)
			*req = nrit;

		if (j->flags & PKG_FLAG_UPGRADE_VULNERABLE) {
			/* Set the proper reason */
			vec_foreach(*uv, _i) {
				struct pkg_job_universe_item *cur = uv->d[_i];
				if (cur->pkg->type != PKG_INSTALLED) {
					free(cur->pkg->reason);
					xasprintf(&cur->pkg->reason, "vulnerability found");
				}
			}
			/* Also process all rdeps recursively */
			if (nrit != NULL) {
				struct pkg *rdep_lp;
				while (pkg_rdeps(nrit->pkg, &rdep) == EPKG_OK) {
					rdep_lp = pkg_jobs_universe_get_local(j->universe, rdep->uid, 0);

					if (rdep_lp) {
						(void)pkg_jobs_process_remote_pkg(j, rdep_lp, NULL, 0);
						if (!pkg_in_universe(j->universe, rdep_lp))
							pkg_free(rdep_lp);
					}
				}
			}
		}
	}

	if (nrit == NULL && lp) {
		if (!pkg_in_universe(j->universe, lp))
			pkg_free(lp);
		return (EPKG_INSTALLED);
	}

	if (lp != NULL && !pkg_in_universe(j->universe, lp))
		pkg_free(lp);

	return (nrit != NULL ? EPKG_OK : EPKG_FATAL);
}

static int
pkg_jobs_find_upgrade(struct pkg_jobs *j, const char *pattern, match_t m)
{
	struct pkg *p = NULL;
	struct pkgdb_it *it;
	bool checklocal, found = false;
	int rc = EPKG_FATAL;
	int with_version;
	struct pkg_dep *rdep = NULL;
	unsigned flags = PKG_LOAD_COMMON;
	struct pkg_job_universe_item *unit = NULL;

	if ((it = pkgdb_repo_query2(j->db, pattern, m, j->reponames)) == NULL)
		return (rc);

	/*
	 * MATCH_EXACT is handled at a higher level, so that we can complain if a
	 * specific upgrade was requested without the package being locally installed.
	 *
	 * MATCH_ALL is a non-issue, because we will not get that from pkg-upgrade
	 * anyways.

	 * Pattern matches are the main target, as the above query may grab packages
	 * that are not installed that we can ignore.
	 */
	checklocal = j->type == PKG_JOBS_UPGRADE && m != MATCH_EXACT && m != MATCH_ALL;
	while (it != NULL && pkgdb_it_next(it, &p, flags) == EPKG_OK) {
		if (checklocal &&
		    _pkg_is_installed(j, p->name, MATCH_INTERNAL) != EPKG_OK)
			continue;
		if (pattern != NULL && *pattern != '@') {
			with_version = strcmp(p->name, pattern);
		} else {
			with_version = 0;
		}
		rc = pkg_jobs_process_remote_pkg(j, p, NULL, with_version);
		if (rc == EPKG_FATAL) {
			break;
		} else if (rc == EPKG_OK)
			found = true;
	}

	pkg_free(p);
	pkgdb_it_free(it);

	if (!found && rc != EPKG_INSTALLED && rc != EPKG_LOCKED) {
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
			if (rdep_package != NULL) {
				if (!pkg_in_universe(j->universe, rdep_package))
					pkg_free(rdep_package);
				if (!pkg_in_universe(j->universe, p))
					pkg_free(p);
				return (EPKG_END);
			}
		}

		dbg(2, "non-automatic package with pattern %s has not been found in "
				"remote repo", pattern);
		rc = pkg_jobs_universe_add_pkg(j->universe, p, &unit);
		if (rc == EPKG_END) {
			pkg_free(p);
			rc = EPKG_OK;
		}
	}

	return (rc);
}

/*
 * Return EPKG_OK if a package with a name matching the pattern is installed
 * locally.
 */
static int
_pkg_is_installed(struct pkg_jobs *j, const char *pattern, match_t match)
{
	struct pkgdb_it *it;
	struct pkg *pkg = NULL;
	int ret;

	it = pkgdb_query(j->db, pattern, match);
	if (it != NULL) {
		ret = pkgdb_it_next(it, &pkg,
		    PKG_LOAD_BASIC | PKG_LOAD_ANNOTATIONS);
		if (ret == EPKG_OK)
			pkg_free(pkg);
		else if (ret == EPKG_END)
			ret = EPKG_NOTINSTALLED;
		pkgdb_it_free(it);
	} else {
		ret = EPKG_FATAL;
	}
	return (ret);
}

static int
pkg_jobs_find_remote_pattern(struct pkg_jobs *j, struct job_pattern *jp)
{
	int rc = EPKG_OK;
	struct pkg *pkg = NULL;
	struct pkg_job_request *req;

	if (!(jp->flags & PKG_PATTERN_FLAG_FILE)) {
		if (j->type == PKG_JOBS_UPGRADE &&
		    (jp->match == MATCH_INTERNAL || jp->match == MATCH_EXACT)) {
			/*
			 * For upgrade patterns we must ensure that a local package is
			 * installed as well.  This only works if we're operating on an
			 * exact match, as we otherwise don't know exactly what packages
			 * are in store for us.
			 */
			rc = _pkg_is_installed(j, jp->pattern, jp->match);
			if (rc != EPKG_OK) {
				pkg_emit_error(
				    "%s is not installed, therefore upgrade is impossible",
				    jp->pattern);
				return (rc);
			}
		}
		rc = pkg_jobs_find_upgrade(j, jp->pattern, jp->match);
	} else if (pkg_open(&pkg, jp->path, PKG_OPEN_MANIFEST_ONLY) != EPKG_OK) {
		rc = EPKG_FATAL;
	} else if (pkg_validate(pkg, j->db) == EPKG_OK) {
		if (j->type == PKG_JOBS_UPGRADE &&
		    _pkg_is_installed(j, pkg->name, MATCH_INTERNAL) != EPKG_OK) {
			pkg_emit_error(
			    "%s is not installed, therefore upgrade is impossible",
			    pkg->name);
			return (EPKG_NOTINSTALLED);
		}
		pkg->type = PKG_FILE;
		pkg_jobs_add_req(j, pkg);

		req = pkghash_get_value(j->request_add, pkg->uid);
		if (req != NULL)
			req->items.d[0].jp = jp;
	} else {
		pkg_emit_error("cannot load %s: invalid format", jp->pattern);
		rc = EPKG_FATAL;
	}

	return (rc);
}

static bool
charv_diff(const charv_t *local, const charv_t *remote,
    const char *label, char **reason)
{
	xstring *diff = NULL;
	int nd = 0;
	size_t li = 0, ri = 0;
	size_t ll = vec_len(local);
	size_t rl = vec_len(remote);

	while (li < ll || ri < rl) {
		int cmp;
		if (li >= ll) cmp = 1;
		else if (ri >= rl) cmp = -1;
		else cmp = strcmp(local->d[li], remote->d[ri]);
		if (cmp < 0) {
			if (diff == NULL) diff = xstring_new();
			xprintf(diff, "%s%s (removed)",
			    nd ? ", " : "", local->d[li]);
			nd++; li++;
		} else if (cmp > 0) {
			if (diff == NULL) diff = xstring_new();
			xprintf(diff, "%s%s (added)",
			    nd ? ", " : "", remote->d[ri]);
			nd++; ri++;
		} else {
			li++; ri++;
		}
	}
	if (nd > 0) {
		fflush(diff->fp);
		free(*reason);
		xasprintf(reason, "%s: %s", label, diff->buf);
		xstring_free(diff);
		return (true);
	}
	return (false);
}

bool
pkg_jobs_need_upgrade(charv_t *system_shlibs, struct pkg *rp, struct pkg *lp)
{
	int ret, ret1, ret2;
	struct pkg_kv *lo = NULL, *ro = NULL;
	struct pkg_dep *ld = NULL, *rd = NULL;
	struct pkg_conflict *lc = NULL, *rc = NULL;

	/* If no local package, then rp is obviously need to be added */
	if (lp == NULL)
		return true;

	/* Do not upgrade locked packages */
	if (lp->locked) {
		pkg_emit_locked(lp);
		return (false);
	}

	if (lp->digest != NULL && rp->digest != NULL &&
	    STREQ(lp->digest, rp->digest)) {
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
	if (!STREQ(lp->abi, rp->abi)) {
		free(rp->reason);
		xasprintf(&rp->reason, "ABI changed: '%s' -> '%s'",
		    lp->abi, rp->abi);
		return (true);
	}

	if (lp->vital != rp->vital) {
		free(rp->reason);
		xasprintf(&rp->reason, "Vital flag changed: '%s' -> '%s'",
		    lp->vital ? "true" : "false", rp->vital ? "true" : "false");
		return (true);
	}

	/* compare options */
	if (pkg_object_bool(pkg_config_get("PKG_REINSTALL_ON_OPTIONS_CHANGE"))) {
		xstring *optdiff = NULL;
		int ndiffs = 0;
		for (;;) {
			ret1 = pkg_options(rp, &ro);
			ret2 = pkg_options(lp, &lo);
			if (ret1 != ret2) {
				if (optdiff == NULL)
					optdiff = xstring_new();
				if (ro == NULL) {
					xprintf(optdiff, "%s%s (removed)",
					    ndiffs ? ", " : "", lo->key);
				} else if (lo == NULL) {
					xprintf(optdiff, "%s%s (added)",
					    ndiffs ? ", " : "", ro->key);
				}
				ndiffs++;
				/* lists have different lengths, done */
				break;
			}
			if (ret1 != EPKG_OK)
				break;
			if (!STREQ(lo->key, ro->key)) {
				if (optdiff == NULL)
					optdiff = xstring_new();
				xprintf(optdiff, "%s%s (removed), %s (added)",
				    ndiffs ? ", " : "", lo->key, ro->key);
				ndiffs++;
			} else if (!STREQ(lo->value, ro->value)) {
				if (optdiff == NULL)
					optdiff = xstring_new();
				xprintf(optdiff, "%s%s (%s -> %s)",
				    ndiffs ? ", " : "", lo->key,
				    lo->value, ro->value);
				ndiffs++;
			}
		}
		if (ndiffs > 0) {
			fflush(optdiff->fp);
			free(rp->reason);
			xasprintf(&rp->reason, "option changed: %s",
			    optdiff->buf);
			xstring_free(optdiff);
			return (true);
		}
	}

	/* What about the direct deps */

	xstring *diff = NULL;
	int nd = 0;
	for (;;) {
		ret1 = pkg_deps(rp, &rd);
		ret2 = pkg_deps(lp, &ld);
		if (ret1 != ret2) {
			if (diff == NULL) diff = xstring_new();
			if (rd == NULL) {
				xprintf(diff, "%s%s (removed)",
				    nd ? ", " : "", ld->name);
			} else if (ld == NULL) {
				xprintf(diff, "%s%s (added)",
				    nd ? ", " : "", rd->name);
			}
			nd++;
			break;
		}
		if (ret1 != EPKG_OK)
			break;
		if (!STREQ(rd->name, ld->name)) {
			if (diff == NULL) diff = xstring_new();
			xprintf(diff, "%s%s (removed), %s (added)",
			    nd ? ", " : "", ld->name, rd->name);
			nd++;
		} else if (!STREQ(rd->origin, ld->origin)) {
			if (diff == NULL) diff = xstring_new();
			xprintf(diff, "%s%s (origin changed)",
			    nd ? ", " : "", rd->name);
			nd++;
		}
	}
	if (nd > 0) {
		fflush(diff->fp);
		free(rp->reason);
		xasprintf(&rp->reason, "direct dependency changed: %s",
		    diff->buf);
		xstring_free(diff);
		return (true);
	}

	/* Conflicts */
	for (;;) {
		ret1 = pkg_conflicts(rp, &rc);
		ret2 = pkg_conflicts(lp, &lc);
		if (ret1 != ret2) {
			free(rp->reason);
			rp->reason = xstrdup("direct conflict changed");
			return (true);
		}
		if (ret1 == EPKG_OK) {
			if (!STREQ(rc->uid, lc->uid)) {
				free(rp->reason);
				rp->reason = xstrdup("direct conflict changed");
				return (true);
			}
		}
		else
			break;
	}

	if (charv_diff(&lp->provides, &rp->provides,
	    "provides changed", &rp->reason))
		return (true);

	if (charv_diff(&lp->requires, &rp->requires,
	    "requires changed", &rp->reason))
		return (true);

	if (charv_diff(&lp->shlibs_provided, &rp->shlibs_provided,
	    "provided shared library changed", &rp->reason))
		return (true);

	/* shlibs_required needs special handling for system shlibs */
	{
		xstring *diff = NULL;
		int nd = 0;
		size_t cntl = vec_len(&lp->shlibs_required);
		size_t cntr = vec_len(&rp->shlibs_required);
		bool has_system_shlibs = vec_len(system_shlibs) > 0;
		size_t i = 0, j = 0;
		while (i < cntl || j < cntr) {
			if (has_system_shlibs) {
				while (i < cntl &&
				    charv_search(system_shlibs,
				    lp->shlibs_required.d[i]) != NULL)
					i++;
				while (j < cntr &&
				    charv_search(system_shlibs,
				    rp->shlibs_required.d[j]) != NULL)
					j++;
			}
			if (i >= cntl && j >= cntr)
				break;
			int cmp;
			if (i >= cntl) cmp = 1;
			else if (j >= cntr) cmp = -1;
			else cmp = strcmp(lp->shlibs_required.d[i],
			    rp->shlibs_required.d[j]);
			if (cmp < 0) {
				if (diff == NULL) diff = xstring_new();
				xprintf(diff, "%s%s (removed)",
				    nd ? ", " : "",
				    lp->shlibs_required.d[i]);
				nd++; i++;
			} else if (cmp > 0) {
				if (diff == NULL) diff = xstring_new();
				xprintf(diff, "%s%s (added)",
				    nd ? ", " : "",
				    rp->shlibs_required.d[j]);
				nd++; j++;
			} else {
				i++; j++;
			}
		}
		if (nd > 0) {
			fflush(diff->fp);
			free(rp->reason);
			xasprintf(&rp->reason,
			    "required shared library changed: %s",
			    diff->buf);
			xstring_free(diff);
			return (true);
		}
	}

	return (false);
}

static void
pkg_jobs_propagate_automatic(struct pkg_jobs *j)
{
	universe_itemv_t *uv;
	struct pkg_job_universe_item *cur, *local;
	struct pkg_job_request *req;
	bool automatic;
	pkghash_it it;

	it = pkghash_iterator(j->universe->items);
	while (pkghash_next(&it)) {
		uv = (universe_itemv_t *)it.value;
		if (uv->len == 1) {
			/*
			 * For packages that are alone in the installation list
			 * we search them in the corresponding request
			 */
			req = pkghash_get_value(j->request_add, uv->d[0]->pkg->uid);
			if ((req == NULL || req->automatic) &&
			    uv->d[0]->pkg->type != PKG_INSTALLED) {
				automatic = true;
				dbg(2, "set automatic flag for %s", uv->d[0]->pkg->uid);
				uv->d[0]->pkg->automatic = automatic;
			}
			else {
				if (j->type == PKG_JOBS_INSTALL) {
					uv->d[0]->pkg->automatic = false;
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
			vec_foreach(*uv, _i) {
				cur = uv->d[_i];
				if (cur->pkg->type == PKG_INSTALLED) {
					local = cur;
					automatic = local->pkg->automatic;
					break;
				}
			}
			if (local != NULL) {
				vec_foreach(*uv, _i) {
					cur = uv->d[_i];
					/*
					 * Propagate automatic from local package
					 */
					if (cur->pkg->type != PKG_INSTALLED) {
						cur->pkg->automatic = automatic;
					}
				}
			}
			else {
				/*
				 * For packages that are not unique, we might still have
				 * a situation when we need to set automatic for all
				 * non-local packages
				 *
				 * See #1374
				 */
				req = pkghash_get_value(j->request_add, uv->d[0]->pkg->uid);
				if ((req == NULL || req->automatic)) {
					automatic = true;
					dbg(2, "set automatic flag for %s", uv->d[0]->pkg->uid);
					vec_foreach(*uv, _j) {
						uv->d[_j]->pkg->automatic = automatic;
					}
				}
			}
		}
	}
}

static struct pkg_job_request *
pkg_jobs_find_deinstall_request(struct pkg_job_universe_item *item,
		struct pkg_jobs *j, int rec_level)
{
	struct pkg_job_request *found;
	universe_itemv_t *dep_uv;
	struct pkg_dep *d = NULL;
	struct pkg *pkg = item->pkg;

	if (rec_level > PKG_MAX_DEINSTALL_RECURSION) {
		dbg(2, "cannot find deinstall request after %d iterations for %s,"
		    " circular dependency maybe", PKG_MAX_DEINSTALL_RECURSION, pkg->uid);
		return (NULL);
	}

	found = pkghash_get_value(j->request_delete, pkg->uid);
	if (found == NULL) {
		while (pkg_deps(pkg, &d) == EPKG_OK) {
			dep_uv = pkg_jobs_universe_find(j->universe, d->uid);
			if (dep_uv) {
				found = pkg_jobs_find_deinstall_request(dep_uv->d[0], j, rec_level + 1);
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

	vec_foreach(j->jobs, i) {
		sit = j->jobs.d[i];
		jreq = pkg_jobs_find_deinstall_request(sit->items[0], j, 0);
		if (jreq != NULL && jreq->items.d[0].unit != sit->items[0]) {
			req_pkg = jreq->items.d[0].pkg;
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
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	bool force = (j->flags & PKG_FLAG_FORCE);
	vec_foreach(j->patterns, pi) {
		struct job_pattern *jp = &j->patterns.d[pi];
		if ((it = pkgdb_query(j->db, jp->pattern, jp->match)) == NULL)
			return (EPKG_FATAL);

		if (pkgdb_it_count(it) == 0) {
			pkg_emit_notice("No packages matched for pattern '%s'\n", jp->pattern);
		}

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_RDEPS|
		    PKG_LOAD_DEPS|PKG_LOAD_ANNOTATIONS|PKG_LOAD_PROVIDES|
		    PKG_LOAD_SHLIBS_PROVIDED) == EPKG_OK) {
			if(pkg->locked || (pkg->vital && !force)) {
				vec_push(&j->lockedpkgs, pkg);
			}
			else {
				pkg_jobs_add_req(j, pkg);
			}
			pkg = NULL;
		}
		pkgdb_it_free(it);
	}

	j->solved = true;

	return (pkg_jobs_process_delete_request(j));
}

static int
jobs_solve_autoremove(struct pkg_jobs *j)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;

	if ((it = pkgdb_query_cond(j->db, " WHERE automatic=1 AND vital=0 AND locked=0", NULL, MATCH_ALL)) == NULL)
		return (EPKG_FATAL);

	while (pkgdb_it_next(it, &pkg,
			PKG_LOAD_BASIC|PKG_LOAD_RDEPS|PKG_LOAD_DEPS|
			PKG_LOAD_ANNOTATIONS|PKG_LOAD_PROVIDES|
			PKG_LOAD_SHLIBS_PROVIDED)
			== EPKG_OK) {
		if (pkg_jobs_test_automatic(j, pkg)) {
			assert(pkg_jobs_add_req(j, pkg));
		} else {
			pkg_free(pkg);
		}
		pkg = NULL;
	}
	pkgdb_it_free(it);

	j->solved = true;
	pkg_jobs_process_delete_request(j);

	return (EPKG_OK);
}

/*
 * Do we need to touch this package as part of an upgrade job?
 */
static bool
is_upgrade_candidate(struct pkg_jobs *j, struct pkg *pkg)
{
	struct pkgdb_it *it;
	struct pkg *p = NULL;

	/* A forced upgrade upgrades everything. */
	if ((j->flags & PKG_FLAG_FORCE) != 0)
		return (true);

	/* If we have no digest, we need to check this package */
	if (pkg->digest == NULL)
		return (true);

	/* Does a remote repo have a different version of this package? */
	it = pkgdb_repo_query2(j->db, pkg->uid, MATCH_INTERNAL, j->reponames);
	if (it != NULL) {
		while (pkgdb_it_next(it, &p, PKG_LOAD_BASIC) == EPKG_OK) {
			if (!STREQ(p->digest, pkg->digest)) {
				/* Found an upgrade candidate. */
				pkg_free(p);
				pkgdb_it_free(it);
				return (true);
			}
		}
		pkg_free(p);
		pkgdb_it_free(it);
		return (false);
	}

	return (true);
}

static candidates_t *
pkg_jobs_find_upgrade_candidates(struct pkg_jobs *j)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	candidates_t *candidates;

	if ((it = pkgdb_query(j->db, NULL, MATCH_ALL)) == NULL)
		return (NULL);

	candidates = xcalloc(1, sizeof(*candidates));
	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		if (is_upgrade_candidate(j, pkg))
			vec_push(candidates, pkg->id);
	}
	pkg_free(pkg);
	pkgdb_it_free(it);

	return (candidates);
}

static int
jobs_solve_full_upgrade(struct pkg_jobs *j)
{
	struct pkg *pkg = NULL;
	size_t jcount = 0;
	size_t elt_num = 0;
	char sqlbuf[256];
	candidates_t *candidates;
	struct pkg_job_request *req;
	struct pkgdb_it *it;
	pkghash_it hit;
	unsigned flags = PKG_LOAD_BASIC|PKG_LOAD_OPTIONS|PKG_LOAD_DEPS|PKG_LOAD_REQUIRES|
			PKG_LOAD_SHLIBS_REQUIRED|PKG_LOAD_ANNOTATIONS|PKG_LOAD_CONFLICTS;

	assert(!j->solved);

	candidates = pkg_jobs_find_upgrade_candidates(j);
	if (candidates == NULL)
		return (EPKG_FATAL);
	jcount = candidates->len;

	pkg_emit_progress_start("Checking for upgrades (%zd candidates)",
			jcount);

	vec_foreach(*candidates, i) {
		pkg_emit_progress_tick(++elt_num, jcount);
		sqlite3_snprintf(sizeof(sqlbuf), sqlbuf, " WHERE p.id=%" PRId64,
		    candidates->d[i]);
		if ((it = pkgdb_query_cond(j->db, sqlbuf, NULL, MATCH_ALL)) == NULL)
			return (EPKG_FATAL);

		pkg = NULL;
		while (pkgdb_it_next(it, &pkg, flags) == EPKG_OK) {
			if (pkg_jobs_find_upgrade(j, pkg->uid,
			    MATCH_INTERNAL) == EPKG_FATAL &&
			    (j->flags & PKG_FLAG_FORCE))
				pkg_emit_notice("%s is installed but "
				    "not available in any configured "
				    "repository", pkg->uid);
		}
		pkg_free(pkg);
		pkgdb_it_free(it);
	}
	vec_free(candidates);
	free(candidates);
	pkg_emit_progress_tick(jcount, jcount);

	pkg_emit_progress_start("Processing candidates (%zd candidates)",
			jcount);
	elt_num = 0;

	hit = pkghash_iterator(j->request_add);
	while (pkghash_next(&hit)) {
		req = hit.value;
		pkg_emit_progress_tick(++elt_num, jcount);
		pkg_jobs_universe_process(j->universe, req->items.d[0].pkg);
	}
	pkg_emit_progress_tick(jcount, jcount);

	pkg_jobs_universe_process_upgrade_chains(j);

	return (EPKG_OK);
}

static int
jobs_solve_partial_upgrade(struct pkg_jobs *j)
{
	struct pkg_job_request *req;
	bool error_found = false;
	int retcode;
	pkghash_it it;

	assert(!j->solved);

	vec_foreach(j->patterns, pi) {
		struct job_pattern *jp = &j->patterns.d[pi];
		retcode = pkg_jobs_find_remote_pattern(j, jp);
		if (retcode == EPKG_FATAL) {
			pkg_emit_error("No packages available to %s matching '%s' "
					"have been found in the "
					"repositories",
					(j->type == PKG_JOBS_UPGRADE) ? "upgrade" : "install",
					jp->pattern);
			/* delay the return to be sure we print a message for all issues */
			if ((j->flags & PKG_FLAG_UPGRADE_VULNERABLE) == 0)
				error_found = true;
		}
		if (retcode == EPKG_LOCKED) {
			return (retcode);
		}
	}
	if (error_found)
		return (EPKG_FATAL);
	/*
	 * Here we have not selected the proper candidate among all
	 * possible choices.
	 * Hence, we want to perform this procedure now to ensure that
	 * we are processing the correct packages.
	 */
	pkg_jobs_universe_process_upgrade_chains(j);
	/*
	 * Need to iterate request one more time to recurse depends
	 */

	it = pkghash_iterator(j->request_add);
	while (pkghash_next(&it)) {
		req = it.value;
		retcode = pkg_jobs_universe_process(j->universe, req->items.d[0].pkg);
		if (retcode != EPKG_OK)
			return (retcode);
	}
	return (EPKG_OK);
}

static int
jobs_solve_install_upgrade(struct pkg_jobs *j)
{
	struct pkg_job_request *req;
	int retcode = 0;
	pkghash_it it;

	/* Check for new pkg. Skip for 'upgrade -F'. */
	if ((j->flags & PKG_FLAG_SKIP_INSTALL) == 0 &&
	    (j->flags & PKG_FLAG_DRY_RUN) == 0 &&
	    (j->flags & PKG_FLAG_PKG_VERSION_TEST) == PKG_FLAG_PKG_VERSION_TEST)
		if (new_pkg_version(j)) {
			j->flags &= ~PKG_FLAG_PKG_VERSION_TEST;
			j->conservative = false;
			j->pinning = false;
			pkg_emit_newpkgversion();
			goto order;
		}

	if (j->patterns.len == 0 && j->type == PKG_JOBS_INSTALL) {
		pkg_emit_error("no patterns are specified for install job");
		return (EPKG_FATAL);
	}

	if (!j->solved) {
		if (j->patterns.len == 0) {
			retcode = jobs_solve_full_upgrade(j);
			if (retcode != EPKG_OK)
				return (retcode);
		} else {
			retcode = jobs_solve_partial_upgrade(j);
			if (retcode != EPKG_OK)
				return (retcode);
		}
	}
	else {
		/*
		 * If we have tried to solve request, then we just want to re-add all
		 * request packages to the universe to find out any potential conflicts
		 */
		it = pkghash_iterator(j->request_add);
		while (pkghash_next(&it)) {
			req = it.value;
			pkg_jobs_universe_process(j->universe, req->items.d[0].pkg);
		}
	}

	if (pkg_conflicts_request_resolve(j) != EPKG_OK) {
		pkg_emit_error("Cannot resolve conflicts in a request");
		return (EPKG_FATAL);
	}

	pkg_jobs_propagate_automatic(j);

order:

	j->solved = true;

	return (EPKG_OK);
}

static int
jobs_solve_fetch(struct pkg_jobs *j)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	struct pkg_job_request *req;
	pkghash_it hit;
	pkg_error_t rc;

	assert(!j->solved);

	if ((j->flags & PKG_FLAG_UPGRADES_FOR_INSTALLED) == PKG_FLAG_UPGRADES_FOR_INSTALLED) {
		if ((it = pkgdb_query(j->db, NULL, MATCH_ALL)) == NULL)
			return (EPKG_FATAL);

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
			if(pkg->locked) {
				pkg_emit_locked(pkg);
			}
			else {
				/* Do not test we ignore what doesn't exists remotely */
				pkg_jobs_find_upgrade(j, pkg->uid, MATCH_INTERNAL);
			}
			pkg = NULL;
		}
		pkgdb_it_free(it);
	} else {
		vec_foreach(j->patterns, pi) {
			struct job_pattern *jp = &j->patterns.d[pi];
			/* TODO: use repository priority here */
			if (pkg_jobs_find_upgrade(j, jp->pattern, jp->match) == EPKG_FATAL)
				pkg_emit_error("No packages matching '%s' have been found in the "
						"repositories", jp->pattern);
		}
		hit = pkghash_iterator(j->request_add);
		while (pkghash_next(&hit)) {
			req = hit.value;
			rc = pkg_jobs_universe_process(j->universe, req->items.d[0].pkg);
			if (rc != EPKG_OK && rc != EPKG_END)
				return (rc);
		}
	}

	j->solved = true;

	return (EPKG_OK);
}

static int
solve_with_external_cudf_solver(struct pkg_jobs *j, const char *solver)
{
	int ret, pstatus;
	FILE *spipe[2];
	pid_t pchild;

	pchild = process_spawn_pipe(spipe, solver);
	if (pchild == -1)
		return (EPKG_FATAL);

	ret = pkg_jobs_cudf_emit_file(j, j->type, spipe[1]);
	fclose(spipe[1]);

	if (ret == EPKG_OK)
		ret = pkg_jobs_cudf_parse_output(j, spipe[0]);

	fclose(spipe[0]);
	waitpid(pchild, &pstatus, WNOHANG);

	return (ret);
}

static int
solve_with_external_sat_solver(struct pkg_solve_problem *pb, const char *solver)
{
	int ret, pstatus;
	FILE *spipe[2];
	pid_t pchild;

	pchild = process_spawn_pipe(spipe, solver);
	if (pchild == -1)
		return (EPKG_FATAL);

	ret = pkg_solve_dimacs_export(pb, spipe[1]);
	fclose(spipe[1]);

	if (ret == EPKG_OK)
		ret = pkg_solve_parse_sat_output(spipe[0], pb);

	fclose(spipe[0]);
	waitpid(pchild, &pstatus, WNOHANG);

	return (ret);
}

static int
solve_with_sat_solver(struct pkg_jobs *j)
{
	const char *sat_solver = pkg_object_string(pkg_config_get("SAT_SOLVER"));
	struct pkg_solve_problem *problem;
	const char *dotfile;
	FILE *dot = NULL;
	int ret;

	pkg_jobs_universe_process_upgrade_chains(j);
	problem = pkg_solve_jobs_to_sat(j);
	if (problem == NULL) {
		pkg_emit_error("cannot convert job to SAT problem");
		j->solved = false;
		return (EPKG_FATAL);
	}

	if (sat_solver != NULL)
		return (solve_with_external_sat_solver(problem, sat_solver));

	ret = pkg_solve_sat_problem(problem);
	if (ret == EPKG_AGAIN) {
		pkg_solve_problem_free(problem);
		return (solve_with_sat_solver(j));
	}

	if (ret == EPKG_FATAL) {
		pkg_emit_error("cannot solve job using SAT solver");
		j->solved = false;
	} else {
		ret = pkg_solve_sat_to_jobs(problem);
	}

	if ((dotfile = pkg_object_string(pkg_config_get("DOT_FILE")))
		!= NULL) {
		dot = fopen(dotfile, "we");

		if (dot == NULL) {
			pkg_emit_errno("fopen", dotfile);
		} else {
			pkg_solve_dot_export(problem, dot);
			fclose(dot);
		}
	}
	pkg_solve_problem_free(problem);

	return (ret);
}

static int
pkg_jobs_run_solver(struct pkg_jobs *j)
{
	int ret;

	pkgdb_begin_solver(j->db);

	switch (j->type) {
	case PKG_JOBS_AUTOREMOVE:
		ret = jobs_solve_autoremove(j);
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
		const char *cudf_solver;

		cudf_solver = pkg_object_string(pkg_config_get("CUDF_SOLVER"));
		if (cudf_solver != NULL) {
			ret = solve_with_external_cudf_solver(j, cudf_solver);
		} else {
			ret = solve_with_sat_solver(j);
		}
	}

	if (j->type == PKG_JOBS_DEINSTALL && j->solved)
		pkg_jobs_set_deinstall_reasons(j);

	pkgdb_end_solver(j->db);

	return (ret);
}

int
pkg_jobs_solve(struct pkg_jobs *j)
{
	int ret;

	if (j->system_shlibs.len == 0) {
		/* If /usr/bin/uname is in the pkg database, we are targeting
		 * a pkgbase system and should rely on the pkgbase packages to
		 * provide system shlibs. */
		if (!pkgdb_file_exists(j->db, "/usr/bin/uname")) {
			ret = scan_system_shlibs(&j->system_shlibs, ctx.pkg_rootdir);
			if (ret == EPKG_NOCOMPAT32) {
				j->ignore_compat32 = true;
			} else if (ret != EPKG_OK) {
				return (ret);
			}
		}
	}

	ret = pkg_jobs_run_solver(j);
	if (ret != EPKG_OK)
		return (ret);

	/*
	 * We can avoid asking the user for confirmation twice in the case of
	 * conflicts if we can check for and solve conflicts without first
	 * needing to fetch.
	 */
	vec_foreach(j->jobs, i) {
		struct pkg *p;

		p = ((struct pkg_solved *)j->jobs.d[i])->items[0]->pkg;
		if (p->type != PKG_REMOTE)
			continue;

		if (pkgdb_ensure_loaded(j->db, p, PKG_LOAD_FILES|PKG_LOAD_DIRS)
				== EPKG_FATAL) {
			j->need_fetch = true;
			break;
		}
	}

	if (j->solved && !j->need_fetch && j->type != PKG_JOBS_FETCH) {
		int rc, max_attempts = 100;
		bool has_conflicts = false;

		do {
			j->conflicts_registered = 0;
			rc = pkg_jobs_check_conflicts(j);
			if (rc == EPKG_CONFLICT) {
				vec_free_and_free(&j->jobs, free);
				has_conflicts = true;
				ret = pkg_jobs_solve(j);
				if (ret != EPKG_OK || --max_attempts <= 0)
					break;
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

	return (j->jobs.len);
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
pkg_jobs_handle_install(struct pkg_solved *ps, struct pkg_jobs *j)
{
	struct pkg *new, *old;
	struct pkg_job_request *req;
	char path[MAXPATHLEN], *target;
	int flags = 0;
	int retcode = EPKG_FATAL;

	dbg(2, "begin %s", __func__);
	/*
	 * For a split upgrade, pass along the old package even though it's
	 * already deleted, since we need it in order to merge configuration
	 * file changes.
	 */
	new = ps->items[0]->pkg;
	old = NULL;
	if (ps->items[1] != NULL)
		old = ps->items[1]->pkg;
	else if (ps->type == PKG_SOLVED_UPGRADE_INSTALL)
		old = ps->xlink->items[0]->pkg;

	req = pkghash_get_value(j->request_add, new->uid);
	if (req != NULL && req->items.d[0].jp != NULL &&
			(req->items.d[0].jp->flags & PKG_PATTERN_FLAG_FILE)) {
		/*
		 * We have package as a file, set special repository name
		 */
		target = req->items.d[0].jp->path;
		free(new->reponame);
		new->reponame = xstrdup("local file");
	}
	else {
		pkg_snprintf(path, sizeof(path), "%R", new);
		if (*path != '/')
			pkg_repo_cached_name(new, path, sizeof(path));
		target = path;
	}

	if (old != NULL)
		new->old_version = xstrdup(old->version);

	if ((j->flags & PKG_FLAG_FORCE) == PKG_FLAG_FORCE)
		flags |= PKG_ADD_FORCE;
	if ((j->flags & PKG_FLAG_NOSCRIPT) == PKG_FLAG_NOSCRIPT)
		flags |= PKG_ADD_NOSCRIPT;
	if ((j->flags & PKG_FLAG_FORCE_MISSING) == PKG_FLAG_FORCE_MISSING)
		flags |= PKG_ADD_FORCE_MISSING;
	if ((j->flags & PKG_FLAG_REGISTER_ONLY) == PKG_FLAG_REGISTER_ONLY)
		flags |= PKG_ADD_REGISTER_ONLY;
	if (ps->type != PKG_SOLVED_INSTALL) {
		flags |= PKG_ADD_UPGRADE;
		if (ps->type == PKG_SOLVED_UPGRADE_INSTALL)
			flags |= PKG_ADD_SPLITTED_UPGRADE;
	}
	if (new->automatic ||
	    ((j->flags & PKG_FLAG_AUTOMATIC) == PKG_FLAG_AUTOMATIC &&
	    ps->type == PKG_SOLVED_INSTALL))
		flags |= PKG_ADD_AUTOMATIC;

	// Treat installs where there is already a local package (e.g. a forced install)
	// like an upgrade to handle config merging properly.
	struct pkg *local_old = NULL;
	if (old == NULL) {
		old = pkg_jobs_universe_get_local(j->universe, new->uid, 0);
		local_old = old;
	}

	if (new->type == PKG_GROUP_REMOTE)
		retcode = pkg_add_group(new);
	else if (old != NULL)
		retcode = pkg_add_upgrade(j->db, target, flags, NULL, new, old, &j->triggers, &j->rc);
	else
		retcode = pkg_add_from_remote(j->db, target, flags, NULL, new, &j->triggers, &j->rc);

	if (local_old != NULL && !pkg_in_universe(j->universe, local_old))
		pkg_free(local_old);

	dbg(2, "end %s:", __func__);
	return (retcode);
}

static int
pkg_jobs_handle_delete(struct pkg_solved *ps, struct pkg_jobs *j)
{
	struct pkg *rpkg;
	int flags;

	rpkg = NULL;
	flags = 0;
	if ((j->flags & PKG_FLAG_NOSCRIPT) != 0)
		flags |= PKG_DELETE_NOSCRIPT;
	if ((j->flags & PKG_FLAG_KEEPFILES) != 0)
		flags |= PKG_DELETE_KEEPFILES;
	if (ps->type == PKG_SOLVED_UPGRADE_REMOVE) {
		flags |= PKG_DELETE_UPGRADE;
		rpkg = ps->xlink->items[0]->pkg;
	}
	return (pkg_delete(ps->items[0]->pkg, rpkg, j->db, flags,
	    &j->triggers, &j->rc));
}

static int
pkg_jobs_execute(struct pkg_jobs *j)
{
	dbg(1, "execute");
	struct pkg *p;
	int retcode = EPKG_FATAL;
	pkg_plugin_hook_t pre, post;
	size_t total_actions;
	size_t current_action = 0;

//j->triggers.cleanup = triggers_load(true);
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

	if (j->flags & PKG_FLAG_SKIP_INSTALL)
		return (EPKG_OK);

	if (j->flags & PKG_FLAG_DRY_RUN)
		return (EPKG_OK);

	retcode = pkgdb_upgrade_lock(j->db, PKGDB_LOCK_ADVISORY,
			PKGDB_LOCK_EXCLUSIVE);
	if (retcode != EPKG_OK)
		return (retcode);

	pkg_plugins_hook_run(pre, j, j->db);

	retcode = pkg_jobs_schedule(j);
	if (retcode != EPKG_OK)
		return (retcode);

	total_actions = j->jobs.len;
	vec_foreach(j->jobs, i) {
		struct pkg_solved *ps = j->jobs.d[i];

		pkg_emit_new_action(++current_action, total_actions);
		switch (ps->type) {
		case PKG_SOLVED_DELETE:
			if ((j->flags & PKG_FLAG_FORCE) == 0) {
				p = ps->items[0]->pkg;
				if (p->vital) {
					pkg_emit_error(
					    "Cannot delete vital package: %s!", p->name);
					pkg_emit_error(
					    "If you are sure you want to remove %s", p->name);
					pkg_emit_error(
					    "unset the 'vital' flag with: pkg set -v 0 %s", p->name);
					retcode = EPKG_FATAL;
					goto cleanup;
				}
				if (STREQ(p->name, "pkg") ||
				    STREQ(p->name, "pkg-devel")) {
					if (j->patterns.len == 0 ||
					    j->patterns.d[0].match == MATCH_ALL)
						continue;
					pkg_emit_error(
					    "Cannot delete pkg itself without force flag");
					retcode = EPKG_FATAL;
					goto cleanup;
				}
			}
			/* FALLTHROUGH */
		case PKG_SOLVED_UPGRADE_REMOVE:
			retcode = pkg_jobs_handle_delete(ps, j);
			if (retcode != EPKG_OK)
				goto cleanup;
			break;
		case PKG_SOLVED_INSTALL:
		case PKG_SOLVED_UPGRADE_INSTALL:
		case PKG_SOLVED_UPGRADE:
			retcode = pkg_jobs_handle_install(ps, j);
			if (retcode != EPKG_OK)
				goto cleanup;
			break;
		case PKG_SOLVED_FETCH:
			retcode = EPKG_FATAL;
			pkg_emit_error("internal error: bad job type");
			goto cleanup;
		}

	}

	pkg_plugins_hook_run(post, j, j->db);
	triggers_execute(&j->triggers);
	pkg_deferred_rc_execute(&j->rc);

cleanup:
	pkgdb_release_lock(j->db, PKGDB_LOCK_EXCLUSIVE);
	dbg(1, "execute done");

	return (retcode);
}

static void
pkg_jobs_cancel(struct pkg_jobs *j)
{
	pkgdb_release_lock(j->db, PKGDB_LOCK_ADVISORY);
}

int
pkg_jobs_apply(struct pkg_jobs *j)
{
	int rc;
	bool has_conflicts = false;

	if (!j->solved) {
		pkg_emit_error("The jobs hasn't been solved");
		return (EPKG_FATAL);
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
							vec_free_and_free(&j->jobs, free);
							has_conflicts = true;
							rc = pkg_jobs_solve(j);
						}
						else if (rc == EPKG_OK && !has_conflicts) {
							rc = pkg_jobs_execute(j);
							break;
						}
					} while (j->conflicts_registered > 0);

					if (has_conflicts) {
						return (EPKG_CONFLICT);
					}
				}
				else {
					/* Not the first run, conflicts are resolved already */
					rc = pkg_jobs_execute(j);
				}
			}
			else if (rc == EPKG_CANCEL) {
				pkg_jobs_cancel(j);
			}
		}
		else {
			rc = pkg_jobs_execute(j);
		}

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
	struct stat st;
	struct statvfs fs;
	int64_t dlsize = 0, fs_avail = -1;
	const char *cachedir = NULL;
	char cachedpath[MAXPATHLEN];
	bool mirror = (j->flags & PKG_FLAG_FETCH_MIRROR) ? true : false;
	bool symlink = (j->flags & PKG_FLAG_FETCH_SYMLINK) ? true : false;
	int retcode;


	if (j->destdir == NULL || !mirror)
		cachedir = ctx.cachedir;
	else
		cachedir = j->destdir;

	/* check for available size to fetch */
	vec_foreach(j->jobs, i) {
		struct pkg_solved *ps = j->jobs.d[i];
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

	for (;;) {
		if (statvfs(cachedir, &fs) == 0) break;
		if (errno == EINTR) continue;
		if (errno == ENOENT) {
			if (pkg_mkdirs(cachedir) != EPKG_OK) return EPKG_FATAL;
			continue;
		}
		pkg_emit_errno("statvfs", cachedir);
		return EPKG_FATAL;
	}
	fs_avail = fs.f_frsize * (int64_t)fs.f_bavail;

	if (fs_avail != -1 && dlsize > fs_avail) {
		char dlsz[9], fsz[9];

		humanize_number(dlsz, sizeof(dlsz), dlsize, "B",
		    HN_AUTOSCALE, HN_IEC_PREFIXES);
		humanize_number(fsz, sizeof(fsz), fs_avail, "B",
		    HN_AUTOSCALE, HN_IEC_PREFIXES);
		pkg_emit_error("Not enough space in %s, needed %s available %s",
		    cachedir, dlsz, fsz);
		return (EPKG_FATAL);
	}

	if ((j->flags & PKG_FLAG_DRY_RUN) == PKG_FLAG_DRY_RUN)
		return (EPKG_OK); /* don't download anything */

	/* Fetch */
	vec_foreach(j->jobs, i) {
		struct pkg_solved *ps = j->jobs.d[i];
		if (ps->type != PKG_SOLVED_DELETE && ps->type != PKG_SOLVED_UPGRADE_REMOVE) {
			p = ps->items[0]->pkg;
			if (p->type != PKG_REMOTE)
				continue;

			if (mirror) {
				retcode = pkg_repo_mirror_package(p, cachedir, symlink);
				if (retcode != EPKG_OK)
					return (retcode);
			}
			else {
				retcode = pkg_repo_fetch_package(p);
				if (retcode != EPKG_OK)
					return (retcode);
			}
		}
	}

	return (EPKG_OK);
}

#ifdef HAVE_CHFLAGSAT
#if defined(UF_NOUNLINK)
#define NOCHANGESFLAGS	\
    (UF_IMMUTABLE | UF_APPEND | UF_NOUNLINK | \
     SF_IMMUTABLE | SF_APPEND | SF_NOUNLINK)
#else
#define NOCHANGESFLAGS	\
    (UF_IMMUTABLE | UF_APPEND | SF_IMMUTABLE | SF_APPEND)
#endif
#define SYSTEM_FLAGS	(SF_IMMUTABLE | SF_APPEND | SF_NOUNLINK)
#endif

/*
 * Check chflags restrictions from jail or securelevel.
 * Returns:
 *   0  - all chflags operations allowed
 *   1  - jail restricts chflags (no flags allowed)
 *   2  - securelevel restricts system flags (SF_*)
 */
static int
pkg_jobs_chflags_restricted(void)
{
#ifdef HAVE_CHFLAGSAT
#ifdef HAVE_LIBJAIL
	int jailed = 0;
	size_t len = sizeof(jailed);

	if (sysctlbyname("security.jail.jailed", &jailed, &len,
	    NULL, 0) != -1 && jailed == 1) {
		int allowed = 0;
		len = sizeof(allowed);
		if (sysctlbyname("security.jail.chflags_allowed", &allowed,
		    &len, NULL, 0) == -1 || allowed == 0)
			return (1);
	}
#endif
	int securelevel = -1;
	size_t slen = sizeof(securelevel);
	if (sysctlbyname("kern.securelevel", &securelevel, &slen,
	    NULL, 0) != -1 && securelevel >= 1)
		return (2);
#endif
	return (0);
}

static int
pkg_jobs_check_chflags(struct pkg_jobs *j)
{
	struct pkg_file *f;
	struct pkg_dir *d;
	int restriction;
	u_long mask;

#ifdef HAVE_CHFLAGSAT
	restriction = pkg_jobs_chflags_restricted();
	if (restriction == 0)
		return (EPKG_OK);

	/* jail without chflags: no flags at all allowed */
	/* securelevel >= 1: only system flags (SF_*) restricted */
	mask = (restriction == 1) ? NOCHANGESFLAGS : SYSTEM_FLAGS;

	vec_foreach(j->jobs, i) {
		struct pkg_solved *ps = j->jobs.d[i];
		struct pkg *p = ps->items[0]->pkg;

		if (p->type != PKG_REMOTE)
			pkgdb_ensure_loaded(j->db, p,
			    PKG_LOAD_FILES|PKG_LOAD_DIRS);

		f = NULL;
		while (pkg_files(p, &f) == EPKG_OK) {
			if (f->fflags & mask)
				goto restricted;
		}
		d = NULL;
		while (pkg_dirs(p, &d) == EPKG_OK) {
			if (d->fflags & mask)
				goto restricted;
		}
		continue;
restricted:
		if (restriction == 1)
			pkg_emit_error("Package %s has files with flags "
			    "that cannot be managed in this jail. "
			    "Set allow.chflags in the jail configuration.",
			    p->name);
		else
			pkg_emit_error("Package %s has files with system "
			    "flags (schg, sunlnk, ...) that cannot be "
			    "managed at securelevel %d. Lower the "
			    "securelevel to -1 to allow this operation.",
			    p->name, 1);
		return (EPKG_FATAL);
	}
#endif
	return (EPKG_OK);
}

static int
pkg_jobs_check_conflicts(struct pkg_jobs *j)
{
	struct pkg *p = NULL;
	int ret = EPKG_OK, res, added = 0;

	pkg_emit_integritycheck_begin();
	j->conflicts_registered = 0;

	vec_foreach(j->jobs, i) {
		struct pkg_solved *ps = j->jobs.d[i];
		universe_itemv_t *uv;
		if (ps->type == PKG_SOLVED_DELETE || ps->type == PKG_SOLVED_UPGRADE_REMOVE) {
			continue;
		}
		else {
			p = ps->items[0]->pkg;

			if (p->type == PKG_REMOTE)
				pkgdb_ensure_loaded(j->db, p, PKG_LOAD_FILES|PKG_LOAD_DIRS);
		}
		uv = pkg_jobs_universe_find(j->universe, ps->items[0]->pkg->uid);
		if ((res = pkg_conflicts_append_chain(uv, j)) != EPKG_OK)
			ret = res;
		else
			added ++;
	}

	dbg(1, "check integrity for %d items added", added);

	pkg_emit_integritycheck_finished(j->conflicts_registered);
	if (j->conflicts_registered > 0)
		ret = EPKG_CONFLICT;

	if (ret == EPKG_OK) {
		res = pkg_jobs_check_chflags(j);
		if (res != EPKG_OK)
			ret = res;
	}

	return (ret);
}

bool
pkg_jobs_has_lockedpkgs(struct pkg_jobs *j)
{
	return (vec_len(&j->lockedpkgs) > 0);
}

void
pkg_jobs_iter_lockedpkgs(struct pkg_jobs *j, locked_pkgs_cb cb, void * ctx)
{
	vec_foreach(j->lockedpkgs, _i) {
		cb(j->lockedpkgs.d[_i], ctx);
	}
}
