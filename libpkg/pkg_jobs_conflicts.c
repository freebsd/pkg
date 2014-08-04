/*-
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

struct pkg_conflict_chain {
	struct pkg_job_request *req;
	struct pkg_conflict_chain *next;
};

static int
pkg_conflicts_chain_cmp_cb(struct pkg_conflict_chain *a, struct pkg_conflict_chain *b)
{
	const char *vera, *verb;

	if (a->req->skip || b->req->skip) {
		return (a->req->skip - b->req->skip);
	}

	pkg_get(a->req->item->pkg, PKG_VERSION, &vera);
	pkg_get(b->req->item->pkg, PKG_VERSION, &verb);

	/* Inverse sort to get the maximum version as the first element */
	return (pkg_version_cmp(vera, verb));
}

static int
pkg_conflicts_request_resolve_chain(struct pkg *req, struct pkg_conflict_chain *chain)
{
	struct pkg_conflict_chain *elt, *selected = NULL;
	const char *name, *origin, *slash_pos;

	pkg_get(req, PKG_NAME, &name);
	/*
	 * First of all prefer pure origins, where the last element of
	 * an origin is pkg name
	 */
	LL_FOREACH(chain, elt) {
		pkg_get(elt->req->item->pkg, PKG_ORIGIN, &origin);
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
		LL_SORT(chain, pkg_conflicts_chain_cmp_cb);
		selected = chain;
	}

	pkg_get(selected->req->item->pkg, PKG_ORIGIN, &origin);
	pkg_debug(2, "select %s in the chain of conflicts for %s", origin, name);
	/* Disable conflicts from a request */
	LL_FOREACH(chain, elt) {
		if (elt != selected)
			elt->req->skip = true;
	}

	return (EPKG_OK);
}

static void
pkg_conflicts_request_add_chain(struct pkg_conflict_chain **chain, struct pkg_job_request *req)
{
	struct pkg_conflict_chain *elt;

	elt = calloc(1, sizeof(struct pkg_conflict_chain));
	if (elt == NULL) {
		pkg_emit_errno("resolve_request_conflicts", "calloc: struct pkg_conflict_chain");
	}
	elt->req = req;
	LL_PREPEND(*chain, elt);
}

int
pkg_conflicts_request_resolve(struct pkg_jobs *j)
{
	struct pkg_job_request *req, *rtmp, *found;
	struct pkg_conflict *c, *ctmp;
	struct pkg_conflict_chain *chain;
	struct pkg_job_universe_item *unit;
	const char *origin;

	HASH_ITER(hh, j->request_add, req, rtmp) {
		chain = NULL;
		if (req->skip)
			continue;

		HASH_ITER(hh, req->item->pkg->conflicts, c, ctmp) {
			unit = pkg_jobs_universe_find(j->universe, pkg_conflict_uniqueid(c));
			if (unit != NULL) {
				HASH_FIND_PTR(j->request_add, unit, found);
				if (found && !found->skip) {
					pkg_conflicts_request_add_chain(&chain, found);
				}
			}
		}
		if (chain != NULL) {
			pkg_get(req->item->pkg, PKG_ORIGIN, &origin);
			/* Add package itself */
			pkg_conflicts_request_add_chain(&chain, req);

			if (pkg_conflicts_request_resolve_chain(req->item->pkg, chain) != EPKG_OK) {
				LL_FREE(chain, free);
				return (EPKG_FATAL);
			}
			LL_FREE(chain, free);
		}
	}

	return (EPKG_OK);
}

void
pkg_conflicts_register(struct pkg *p1, struct pkg *p2, enum pkg_conflict_type type)
{
	struct pkg_conflict *c1, *c2, *test;
	const char *u1, *u2;

	pkg_get(p1, PKG_UNIQUEID, &u1);
	pkg_get(p2, PKG_UNIQUEID, &u2);

	pkg_conflict_new(&c1);
	pkg_conflict_new(&c2);
	if (c1 != NULL && c2 != NULL) {
		c1->type = c2->type = type;
		HASH_FIND_STR(p1->conflicts, u2, test);
		if (test == NULL) {
			sbuf_set(&c1->uniqueid, u2);
			HASH_ADD_KEYPTR(hh, p1->conflicts, pkg_conflict_uniqueid(c1), sbuf_size(c1->uniqueid), c1);
			pkg_debug(2, "registering conflict between %s and %s", u1, u2);
		}

		HASH_FIND_STR(p2->conflicts, u1, test);
		if (test == NULL) {
			sbuf_set(&c2->uniqueid, u1);
			HASH_ADD_KEYPTR(hh, p2->conflicts, pkg_conflict_uniqueid(c2), sbuf_size(c2->uniqueid), c2);
			pkg_debug(2, "registering conflict between %s and %s", u2, u1);
		}
	}
}


static int
pkg_conflicts_add_missing(struct pkg_jobs *j, const char *uid)
{
	struct pkg *npkg;


	npkg = pkg_jobs_universe_get_local(j->universe, uid, 0);
	if (npkg == NULL) {
		npkg = pkg_jobs_universe_get_remote(j->universe, uid, 0);
		pkg_debug(2, "conflicts: add missing remote %s(%d)", uid);
	}
	else {
		pkg_debug(2, "conflicts: add missing local %s(%d)", uid);
	}

	if (npkg == NULL) {
		pkg_emit_error("cannot register conflict with non-existing %s",
				uid);
		return (EPKG_FATAL);
	}

	return pkg_jobs_universe_process(j->universe, npkg);
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

	u1 = pkg_jobs_universe_find(j->universe, o1);
	u2 = pkg_jobs_universe_find(j->universe, o2);

	if (u1 == NULL && u2 == NULL) {
		pkg_emit_error("cannot register conflict with non-existing %s and %s",
				o1, o2);
		return;
	}
	else if (u1 == NULL) {
		if (pkg_conflicts_add_missing(j, o1) != EPKG_OK)
			return;
		u1 = pkg_jobs_universe_find(j->universe, o1);
	}
	else if (u2 == NULL) {
		if (pkg_conflicts_add_missing(j, o2) != EPKG_OK)
			return;
		u2 = pkg_jobs_universe_find(j->universe, o2);
	}
	else {
		/* Maybe we have registered this conflict already */
		HASH_FIND(hh, u1->pkg->conflicts, o2, strlen(o2), c);
		if (c != NULL)
			return;
	}

	/*
	 * Here we have some unit but we do not know, where is a conflict, e.g.
	 * if we have several units U1 and U2 with the same uniqueid O that are in
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

	u1 = pkg_jobs_universe_find(j->universe, o1);
	u2 = pkg_jobs_universe_find(j->universe, o2);

	/*
	 * In case of remote conflict we need to register it only between remote
	 * packets
	 */

	if (u1 == NULL || u2 == NULL) {
		pkg_emit_error("cannot register remote conflict with non-existing %s and %s",
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
