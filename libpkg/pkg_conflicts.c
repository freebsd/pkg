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
	const char *origin;

	HASH_ITER(hh, j->request_add, req, rtmp) {
		chain = NULL;
		if (req->skip)
			continue;

		HASH_ITER(hh, req->item->pkg->conflicts, c, ctmp) {
			HASH_FIND_STR(j->request_add, pkg_conflict_origin(c), found);
			if (found && !found->skip) {
				pkg_conflicts_request_add_chain(&chain, found);
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
	const char *o1, *o2;

	pkg_get(p1, PKG_ORIGIN, &o1);
	pkg_get(p2, PKG_ORIGIN, &o2);

	pkg_conflict_new(&c1);
	pkg_conflict_new(&c2);
	if (c1 != NULL && c2 != NULL) {
		c1->type = c2->type = type;
		HASH_FIND_STR(p1->conflicts, o2, test);
		if (test == NULL) {
			sbuf_set(&c1->origin, o2);
			HASH_ADD_KEYPTR(hh, p1->conflicts, pkg_conflict_origin(c1), sbuf_size(c1->origin), c1);
			pkg_debug(2, "registering conflict between %s and %s", o1, o2);
		}

		HASH_FIND_STR(p2->conflicts, o1, test);
		if (test == NULL) {
			sbuf_set(&c2->origin, o1);
			HASH_ADD_KEYPTR(hh, p2->conflicts, pkg_conflict_origin(c2), sbuf_size(c2->origin), c2);
			pkg_debug(2, "registering conflict between %s and %s", o2, o1);
		}
	}
}
