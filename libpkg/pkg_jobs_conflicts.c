/*-
 * SPDX-License-Identifier: LicenseRef-scancode-bsd-unchanged
 *
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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
#include <sys/types.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/pkg_jobs.h"
#include "siphash.h"

typedef vec_t(struct pkg_job_request *) conflict_chain_t;

static size_t
conflict_items_lower_bound(const conflict_itemv_t *v, uint64_t hash)
{
	size_t lo = 0, hi = v->len;

	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (v->d[mid].hash < hash)
			lo = mid + 1;
		else
			hi = mid;
	}

	return (lo);
}

static struct pkg_jobs_conflict_item *
conflict_items_find(const conflict_itemv_t *v, uint64_t hash)
{
	size_t pos = conflict_items_lower_bound(v, hash);

	if (pos < v->len && v->d[pos].hash == hash)
		return (&v->d[pos]);
	return (NULL);
}

static void
conflict_items_insert(conflict_itemv_t *v, struct pkg_jobs_conflict_item item)
{
	size_t pos = conflict_items_lower_bound(v, item.hash);

	/* Grow the array if needed before inserting */
	if (v->len >= v->cap) {
		v->cap = (v->cap == 0) ? 1 : v->cap * 2;
		v->d = realloc(v->d, v->cap * sizeof(*v->d));
		if (v->d == NULL)
			abort();
	}
	/* Shift elements right to make room at pos */
	if (pos < v->len) {
		memmove(&v->d[pos + 1], &v->d[pos],
		    (v->len - pos) * sizeof(*v->d));
	}
	v->d[pos] = item;
	v->len++;
}

static struct sipkey *
pkg_conflicts_sipkey_init(void)
{
	static struct sipkey *kinit;

	if (kinit == NULL) {
		kinit = xmalloc(sizeof(*kinit));
		arc4random_buf((unsigned char*)kinit, sizeof(*kinit));
	}

	return (kinit);
}

static int
pkg_conflicts_chain_cmp(const void *jra, const void *jrb)
{
	const struct pkg_job_request *a = *(const struct pkg_job_request **)jra;
	const struct pkg_job_request *b = *(const struct pkg_job_request **)jrb;
	const char *vera, *verb;

	if (a->skip || b->skip) {
		return (a->skip - b->skip);
	}

	vera = a->items.d[0].pkg->version;
	verb = b->items.d[0].pkg->version;

	/* Inverse sort to get the maximum version as the first element */
	return (pkg_version_cmp(vera, verb));
}

static int
pkg_conflicts_request_resolve_chain(struct pkg *req, conflict_chain_t *chain)
{
	struct pkg_job_request *selected = NULL;
	const char *slash_pos;

	/*
	 * First of all prefer pure origins, where the last element of
	 * an origin is pkg name
	 */
	vec_foreach(*chain, i) {
		slash_pos = strrchr(chain->d[i]->items.d[0].pkg->origin, '/');
		if (slash_pos != NULL) {
			if (STREQ(slash_pos + 1, req->name)) {
				selected = chain->d[i];
				break;
			}
		}
	}

	if (selected == NULL) {
		/* XXX: add manual selection here */
		/* Sort list by version of package */
		qsort(chain->d, chain->len, sizeof(chain->d[0]), pkg_conflicts_chain_cmp);
		selected = chain->d[0];
	}

	pkg_debug(2, "select %s in the chain of conflicts for %s",
	    selected->items.d[0].pkg->name, req->name);
	/* Disable conflicts from a request */
	vec_foreach(*chain, i) {
		if (chain->d[i] != selected)
			chain->d[i]->skip = true;
	}

	return (EPKG_OK);
}

int
pkg_conflicts_request_resolve(struct pkg_jobs *j)
{
	struct pkg_job_request *req, *found;
	struct pkg_conflict *c;
	universe_itemv_t *uv;

	pkghash_foreach(j->request_add, it) {
		req = it.value;
		conflict_chain_t chain = vec_init();
		if (req->skip)
			continue;

		vec_foreach(req->items.d[0].pkg->conflicts, _ci) {
			c = &req->items.d[0].pkg->conflicts.d[_ci];
			uv = pkg_jobs_universe_find(j->universe, c->uid);
			if (uv != NULL) {
				found = pkghash_get_value(j->request_add,
				    uv->d[0]->pkg->uid);
				if (found != NULL && !found->skip)
					vec_push(&chain, found);
			}
		}
		if (chain.len > 0) {
			/* Add package itself */
			vec_push(&chain, req);

			if (pkg_conflicts_request_resolve_chain(req->items.d[0].pkg, &chain) != EPKG_OK) {
				vec_free(&chain);
				return (EPKG_FATAL);
			}
		}
		vec_free(&chain);
	}

	return (EPKG_OK);
}



/*
 * Checks whether we need to add a conflict between two packages
 */
static bool
pkg_conflicts_need_conflict(struct pkg_jobs *j, struct pkg *p1, struct pkg *p2)
{
	struct pkg_file *fcur;

	if (pkgdb_ensure_loaded(j->db, p1, PKG_LOAD_FILES|PKG_LOAD_DIRS) != EPKG_OK ||
			pkgdb_ensure_loaded(j->db, p2, PKG_LOAD_FILES|PKG_LOAD_DIRS)
						!= EPKG_OK) {
		/*
		 * If some of packages are not loaded we could silently and safely
		 * ignore them
		 */
		pkg_debug(1, "cannot load files from %s and %s to check conflicts",
			p1->name, p2->name);

		return (false);
	}

	/*
	 * Check if we already have this conflict registered
	 */
	if (pkg_get_conflict(p1, p2->uid) != NULL &&
	    pkg_get_conflict(p2, p1->uid) != NULL)
		return false;

	/*
	 * We need to check all files and dirs and find the similar ones
	 */
	vec_foreach(p1->files, _fi) {
		fcur = &p1->files.d[_fi];
		if (pkg_has_file(p2, fcur->path))
			return (true);
		if (pkg_has_dir(p2, fcur->path))
			return (true);
	}
	/* XXX pkg dirs are terribly broken */

	/* No common paths are found in p1 and p2 */
	return (false);
}

static void
pkg_conflicts_register_one(struct pkg *p, struct pkg *op,
    enum pkg_conflict_type type)
{
	struct pkg_conflict c;

	memset(&c, 0, sizeof(c));
	c.type = type;
	c.uid = xstrdup(op->uid);
	c.digest = xstrdup(op->digest);

	struct pkg_conflict *existing = pkg_conflictv_insert_sorted(&p->conflicts, c);
	if (existing != NULL) {
		pkg_conflict_free_content(&c);
		return;
	}
}

/*
 * Record the existence of a file conflict between a pair of packages.
 */
static void
pkg_conflicts_register(struct pkg *p1, struct pkg *p2, const char *path,
    enum pkg_conflict_type type)
{
	pkg_conflicts_register_one(p1, p2, type);
	pkg_conflicts_register_one(p2, p1, type);

	pkg_debug(2, "registering conflict between %s(%s) and %s(%s) on path %s",
	    p1->uid, p1->type == PKG_INSTALLED ? "local" : "remote",
	    p2->uid, p2->type == PKG_INSTALLED ? "local" : "remote", path);
}

/*
 * Register conflicts between packages in the universe chains
 */
static bool
pkg_conflicts_register_chain(struct pkg_jobs *j, universe_itemv_t *uv1,
	universe_itemv_t *uv2, const char *path)
{
	enum pkg_conflict_type type;
	bool ret = false;

	vec_foreach(*uv1, _i1) {
		struct pkg *p1 = uv1->d[_i1]->pkg;
		vec_foreach(*uv2, _i2) {
			struct pkg *p2 = uv2->d[_i2]->pkg;

			if (p1->type == PKG_INSTALLED && p2->type == PKG_INSTALLED)
				type = PKG_CONFLICT_LOCAL_LOCAL;
			else if (p1->type == PKG_INSTALLED || p2->type == PKG_INSTALLED)
				type = PKG_CONFLICT_REMOTE_LOCAL;
			else
				type = PKG_CONFLICT_REMOTE_REMOTE;

			/* A pair of installed packages cannot conflict. */
			if (type != PKG_CONFLICT_LOCAL_LOCAL &&
			    pkg_conflicts_need_conflict(j, p1, p2)) {
				pkg_emit_conflicts(p1, p2, path);
				pkg_conflicts_register(p1, p2, path, type);
				j->conflicts_registered++;
				ret = true;
			}
		}
	}

	return (ret);
}

/*
 * Check whether the specified path is registered locally and returns
 * the package that contains that path or NULL if no conflict was found
 */
static struct pkg *
pkg_conflicts_check_local_path(const char *path, const char *uid,
	struct pkg_jobs *j)
{
	const char sql_local_conflict[] = ""
		"SELECT p.name as uniqueid FROM packages AS p "
		"INNER JOIN files AS f "
		"ON p.id = f.package_id "
		"WHERE f.path = ?1;";
	sqlite3_stmt *stmt;
	int ret;
	struct pkg *p = NULL;

	ret = sqlite3_prepare_v2(j->db->sqlite, sql_local_conflict, -1,
		&stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(j->db->sqlite, sql_local_conflict);
		return (NULL);
	}

	sqlite3_bind_text(stmt, 1,
		path, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2,
		uid, -1, SQLITE_STATIC);
	pkgdb_debug(4, stmt);

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		/*
		 * We have found the conflict with some other chain, so find that chain
		 * or update the universe
		 */
		const char *uid_local = sqlite3_column_text(stmt, 0);

		p = pkg_jobs_universe_get_local(j->universe,
			uid_local, 0);
		assert(p != NULL);

		assert(!STREQ(uid, p->uid));

		if (pkg_get_conflict(p, uid) == NULL) {
			/* We need to register the conflict between two universe chains */
			sqlite3_finalize(stmt);
			return (p);
		}
	}

	sqlite3_finalize(stmt);
	return (NULL);
}

static struct pkg_job_universe_item *
pkg_conflicts_check_all_paths(struct pkg_jobs *j, const char *path,
	struct pkg_job_universe_item *it, struct sipkey *k)
{
	const char *uid1, *uid2;
	struct pkg_jobs_conflict_item *cit;
	struct pkg_conflict *c;
	uint64_t hv;

	hv = siphash24(path, strlen(path), k);
	cit = conflict_items_find(&j->conflict_items, hv);

	if (cit == NULL) {
		/* New entry */
		struct pkg_jobs_conflict_item new_item = { .hash = hv, .item = it };
		conflict_items_insert(&j->conflict_items, new_item);
	}
	else {
		/* Check the same package */
		if (cit->item == it)
			return (NULL);

		uid1 = it->pkg->uid;
		uid2 = cit->item->pkg->uid;
		if (STREQ(uid1, uid2)) {
			/* The same upgrade chain, just upgrade item for speed */
			cit->item = it;
			return (NULL);
		}

		/* Here we can have either collision or a real conflict */
		c = pkg_get_conflict(it->pkg, uid2);
		if (c != NULL) {
			/*
			 * Collision found, change the key following the
			 * Cuckoo principle
			 */
			struct sipkey nk;

			pkg_debug(2, "found a collision on path %s between %s and %s, key: %lu",
				path, uid1, uid2, (unsigned long)k->k[0]);

			nk = *k;
			nk.k[0] ++;
			return (pkg_conflicts_check_all_paths(j, path, it, &nk));
		}

		/* Look up the universe vecs for both items */
		universe_itemv_t *uv1 = pkg_jobs_universe_find(j->universe, uid1);
		universe_itemv_t *uv2 = pkg_jobs_universe_find(j->universe, uid2);
		if (uv1 == NULL || uv2 == NULL ||
		    !pkg_conflicts_register_chain(j, uv1, uv2, path)) {
			struct sipkey nk;

			pkg_debug(2, "found a collision on path %s between %s and %s, key: %lu",
				path, uid1, uid2, (unsigned long)k->k[0]);

			nk = *k;
			nk.k[0] ++;
			return (pkg_conflicts_check_all_paths(j, path, it, &nk));
		}

		return (cit->item);
	}

	return (NULL);
}

static void
pkg_conflicts_check_chain_conflict(struct pkg_job_universe_item *it,
	struct pkg_job_universe_item *local, struct pkg_jobs *j)
{
	struct pkg_file *fcur;
	struct pkg *p;
	struct pkg_job_universe_item *cun;
	struct sipkey *k;

	vec_foreach(it->pkg->files, _fi) {
		fcur = &it->pkg->files.d[_fi];
		k = pkg_conflicts_sipkey_init();
		/* Check in hash tree */
		cun = pkg_conflicts_check_all_paths(j, fcur->path, it, k);

		if (local != NULL) {
			/* Filter only new files for remote packages */
			if (pkg_has_file(local->pkg, fcur->path))
				continue;
		}
		/* Check for local conflict in db */
		p = pkg_conflicts_check_local_path(fcur->path, it->pkg->uid, j);
		pkg_debug(4, "integrity: check path %s of package %s", fcur->path,
			it->pkg->uid);

		if (p != NULL) {
			if (pkg_jobs_universe_process_item(j->universe, p, &cun))
				continue;
			assert(cun != NULL);
			universe_itemv_t *uv_it = pkg_jobs_universe_find(j->universe, it->pkg->uid);
			universe_itemv_t *uv_cun = pkg_jobs_universe_find(j->universe, cun->pkg->uid);
			if (uv_it != NULL && uv_cun != NULL)
				pkg_conflicts_register_chain(j, uv_it, uv_cun, fcur->path);
		}
	}
	/* XXX: dirs are currently broken terribly */
}

void
pkg_conflicts_free(struct pkg_jobs *j)
{
	vec_free(&j->conflict_items);
}

int
pkg_conflicts_append_chain(universe_itemv_t *uv,
	struct pkg_jobs *j)
{
	struct pkg_job_universe_item *lp = NULL, *cur;

	/* conflict_items vec is zero-initialized by pkg_jobs_new */

	/* Find local package */
	vec_foreach(*uv, _i) {
		cur = uv->d[_i];
		if (cur->pkg->type == PKG_INSTALLED) {
			lp = cur;
			if (pkgdb_ensure_loaded(j->db, cur->pkg, PKG_LOAD_FILES|PKG_LOAD_DIRS)
							!= EPKG_OK)
				return (EPKG_FATAL);

			/* Local package is found */
			break;
		}
	}

	/*
	 * Now we go through the all packages in the chain and check them against
	 * conflicts with the locally installed files
	 */
	vec_foreach(*uv, _i) {
		cur = uv->d[_i];
		if (cur != lp) {
			if (pkgdb_ensure_loaded(j->db, cur->pkg, PKG_LOAD_FILES|PKG_LOAD_DIRS)
							!= EPKG_OK) {
				/*
				 * This means that a package was not downloaded, so we can safely
				 * ignore this conflict, since we are not going to install it
				 * anyway
				 */
				pkg_debug (3, "cannot load files from %s to check integrity",
					cur->pkg->name);
			}
			else {
				pkg_conflicts_check_chain_conflict(cur, lp, j);
			}
		}
	}

	return (EPKG_OK);
}
