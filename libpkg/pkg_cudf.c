/*-
 * Copyright (c) 2013 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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

#include <assert.h>
#include <errno.h>
#include <libutil.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"

static int
cudf_emit_pkg(struct pkg *pkg, FILE *f, struct pkgdb *db)
{
	const char *origin, *version;
	struct pkg_dep *dep, *dtmp;
	struct pkg_provide *prov, *ptmp;
	struct pkg_conflict *conflict, *ctmp;

	pkg_get(pkg, PKG_ORIGIN, &origin, PKG_VERSION, &version);

	if (fprintf(f, "package: %s\nversion: %s\ndepends: ", origin, version) < 0)
		return (EPKG_FATAL);

	/* Iterate all dependencies */
	HASH_ITER(hh, pkg->deps, dep, dtmp) {
		if (fprintf(f, "%s%c", pkg_dep_get(dep, PKG_DEP_ORIGIN),
				(dep->hh.hh_next == NULL) ?
				'\n' : ',') < 0) {
			return (EPKG_FATAL);
		}
	}

	if (fprintf(f, "provides: ") < 0)
			return (EPKG_FATAL);
	HASH_ITER(hh, pkg->provides, prov, ptmp) {
		if (fprintf(f, "%s%c", pkg_provide_name(prov),
				(prov->hh.hh_next == NULL) ?
				'\n' : ',') < 0) {
			return (EPKG_FATAL);
		}
	}

	if (fprintf(f, "conflicts: ") < 0)
		return (EPKG_FATAL);
	HASH_ITER(hh, pkg->conflicts, conflict, ctmp) {
		if (fprintf(f, "%s%c", pkg_conflict_origin(conflict),
				(conflict->hh.hh_next == NULL) ?
				'\n' : ',') < 0) {
			return (EPKG_FATAL);
		}
	}

	if (fprintf(f, "installed: %s\n\n", pkg_is_installed(db, origin) ?
			"true" : "false") < 0)
		return (EPKG_FATAL);

	return (EPKG_OK);
}

static int
cudf_emit_request_packages(const char *op, struct pkg_jobs *j, FILE *f)
{
	struct pkg_job_request *req, *tmp;
	const char *origin;

	if (fprintf(f, "%s: ", op) < 0)
		return (EPKG_FATAL);
	HASH_ITER(hh, j->request, req, tmp) {
		pkg_get(req->pkg, PKG_ORIGIN, &origin);
		if (fprintf(f, "%s%c", origin,
				(req->hh.hh_next == NULL) ?
						'\n' : ',') < 0) {
			return (EPKG_FATAL);
		}
	}

	if (fputc('\n', f) < 0)
		return (EPKG_FATAL);

	return (EPKG_OK);
}

int
pkg_jobs_cudf_emit_file(struct pkg_jobs *j, pkg_jobs_t t, FILE *f, struct pkgdb *db)
{
	struct pkg *pkg, *tmp;

	if (fprintf(f, "preamble: \n\n") < 0)
		return (EPKG_FATAL);

	HASH_ITER(hh, j->jobs, pkg, tmp) {
		if (cudf_emit_pkg(pkg, f, db) != EPKG_OK)
			return (EPKG_FATAL);
	}

	if (fprintf(f, "request: \n") < 0)
			return (EPKG_FATAL);

	switch (t) {
	case PKG_JOBS_FETCH:
	case PKG_JOBS_INSTALL:
		if (cudf_emit_request_packages("install", j, f) != EPKG_OK)
			return (EPKG_FATAL);
		break;
	case PKG_JOBS_DEINSTALL:
	case PKG_JOBS_AUTOREMOVE:
		if (cudf_emit_request_packages("remove", j, f) != EPKG_OK)
			return (EPKG_FATAL);
		break;
	case PKG_JOBS_UPGRADE:
		if (cudf_emit_request_packages("upgrade", j, f) != EPKG_OK)
			return (EPKG_FATAL);
		break;
	}
	return (EPKG_OK);
}
