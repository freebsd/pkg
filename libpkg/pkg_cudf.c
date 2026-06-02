/*-
 * SPDX-License-Identifier: LicenseRef-scancode-bsd-unchanged
 *
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

#include <stdio.h>
#include <ctype.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/pkg_jobs.h"

/*
 * CUDF does not support packages with '_' in theirs names, therefore
 * use this ugly function to replace '_' to '@'
 */
static inline int
cudf_print_package_name(FILE *f, const char *name)
{
	const char *p, *c;
	int r = 0;

	p = c = name;
	while (*p) {
		if (*p == '_') {
			r += fprintf(f, "%.*s", (int)(p - c), c);
			fputc('@', f);
			r ++;
			c = p + 1;
		}
		p ++;
	}
	if (p > c) {
		r += fprintf(f, "%.*s", (int)(p - c), c);
	}

	return r;
}

static inline int
cudf_print_element(FILE *f, const char *line, bool has_next, size_t *column)
{
	int ret = 0;
	if (*column > 80) {
		*column = 0;
		ret += fprintf(f, "\n ");
	}

	ret += cudf_print_package_name(f, line);

	if (has_next)
		ret += fprintf(f, ", ");
	else
		ret += fprintf(f, "\n");

	if (ret > 0)
		*column += ret;

	return (ret);
}

static inline int
cudf_print_conflict(FILE *f, const char *uid, int ver, bool has_next, size_t *column)
{
	int ret = 0;
	if (*column > 80) {
		*column = 0;
		ret += fprintf(f, "\n ");
	}

	ret += cudf_print_package_name(f, uid);
	ret += fprintf(f, "=%d", ver);

	if (has_next)
		ret += fprintf(f, ", ");
	else
		ret += fprintf(f, "\n");

	if (ret > 0)
		*column += ret;

	return (ret);
}


static int
cudf_emit_pkg(struct pkg *pkg, int version, FILE *f,
		universe_itemv_t *conflicts_chain)
{
	struct pkg_dep *dep;
	struct pkg_conflict *conflict;
	size_t column = 0;
	int ver;
	bool has_chain_conflict = false;

	if (fprintf(f, "package: ") < 0)
		return (EPKG_FATAL);

	if (cudf_print_package_name(f, pkg->uid) < 0)
		return (EPKG_FATAL);

	if (fprintf(f, "\nversion: %d\n", version) < 0)
		return (EPKG_FATAL);

	if (pkg->depends.len > 0) {
		if (fprintf(f, "depends: ") < 0)
			return (EPKG_FATAL);
		vec_foreach(pkg->depends, _di) {
			dep = &pkg->depends.d[_di];
			if (cudf_print_element(f, dep->name,
			    _di + 1 < pkg->depends.len, &column) < 0) {
				return (EPKG_FATAL);
			}
		}
	}

	column = 0;
	if (vec_len(&pkg->provides) > 0) {
		if (fprintf(f, "provides: ") < 0)
			return (EPKG_FATAL);
		vec_foreach(pkg->provides, i) {
			if (cudf_print_element(f, pkg->provides.d[i],
			    column + 1 == vec_len(&pkg->provides), &column) < 0) {
				return (EPKG_FATAL);
			}
		}
	}

	/* Check if any other item in the chain is a conflict candidate */
	vec_foreach(*conflicts_chain, _ci) {
		if (conflicts_chain->d[_ci]->pkg != pkg &&
		    !conflicts_chain->d[_ci]->cudf_emit_skip) {
			has_chain_conflict = true;
			break;
		}
	}

	column = 0;
	if (pkg->conflicts.len > 0 || has_chain_conflict) {
		if (fprintf(f, "conflicts: ") < 0)
			return (EPKG_FATAL);
		vec_foreach(pkg->conflicts, _ci) {
			conflict = &pkg->conflicts.d[_ci];
			if (cudf_print_element(f, conflict->uid,
					(_ci + 1 < pkg->conflicts.len), &column) < 0) {
				return (EPKG_FATAL);
			}
		}
		ver = 1;
		vec_foreach(*conflicts_chain, _ci) {
			struct pkg_job_universe_item *u = conflicts_chain->d[_ci];
			bool has_next = false;
			/* Check if there's a next non-self non-skipped item */
			for (size_t _nci = _ci + 1; _nci < conflicts_chain->len; _nci++) {
				if (conflicts_chain->d[_nci]->pkg != pkg) {
					has_next = true;
					break;
				}
			}
			if (u->pkg != pkg && !u->cudf_emit_skip) {
				if (cudf_print_conflict(f, pkg->uid, ver,
				   has_next, &column) < 0) {
					return (EPKG_FATAL);
				}
			}
			ver ++;
		}
	}

	if (fprintf(f, "installed: %s\n\n", pkg->type == PKG_INSTALLED ?
			"true" : "false") < 0)
		return (EPKG_FATAL);

	return (EPKG_OK);
}

static int
cudf_emit_request_packages(const char *op, struct pkg_jobs *j, FILE *f)
{
	struct pkg_job_request *req;
	size_t column = 0, cnt = 0, max;
	bool printed = false;
	pkghash_it it;

	max = pkghash_count(j->request_add);
	if (fprintf(f, "%s: ", op) < 0)
		return (EPKG_FATAL);
	it = pkghash_iterator(j->request_add);
	while (pkghash_next(&it)) {
		req = it.value;
		cnt++;
		if (req->skip)
			continue;
		if (cudf_print_element(f, req->items.d[0].pkg->uid,
		    (max > cnt), &column) < 0) {
			return (EPKG_FATAL);
		}
		printed = true;
	}

	if (!printed)
		if (fputc('\n', f) < 0)
			return (EPKG_FATAL);

	column = 0;
	printed = false;
	if (fprintf(f, "remove: ") < 0)
		return (EPKG_FATAL);
	max = pkghash_count(j->request_delete);
	it = pkghash_iterator(j->request_delete);
	while (pkghash_next(&it)) {
		req = it.value;
		cnt++;
		if (req->skip)
			continue;
		if (cudf_print_element(f, req->items.d[0].pkg->uid,
		    (max > cnt), &column) < 0) {
			return (EPKG_FATAL);
		}
		printed = true;
	}

	if (!printed)
		if (fputc('\n', f) < 0)
			return (EPKG_FATAL);

	return (EPKG_OK);
}

static int
pkg_cudf_version_cmp(const void *pa, const void *pb)
{
	struct pkg_job_universe_item *a = *(struct pkg_job_universe_item *const *)pa;
	struct pkg_job_universe_item *b = *(struct pkg_job_universe_item *const *)pb;
	int ret;

	ret = pkg_version_cmp(a->pkg->version, b->pkg->version);
	if (ret == 0) {
		/* Ignore remote packages whose versions are equal to ours */
		if (a->pkg->type != PKG_INSTALLED)
			a->cudf_emit_skip = true;
		else if (b->pkg->type != PKG_INSTALLED)
			b->cudf_emit_skip = true;
	}


	return (ret);
}

int
pkg_jobs_cudf_emit_file(struct pkg_jobs *j, pkg_jobs_t t, FILE *f)
{
	struct pkg *pkg;
	universe_itemv_t *uv;
	int version;
	pkghash_it hit;

	if (fprintf(f, "preamble: \n\n") < 0)
		return (EPKG_FATAL);

	hit = pkghash_iterator(j->universe->items);
	while (pkghash_next(&hit)) {
		uv = (universe_itemv_t *)hit.value;
		/* Sort the vec by version */
		qsort(uv->d, uv->len, sizeof(uv->d[0]), pkg_cudf_version_cmp);

		version = 1;
		vec_foreach(*uv, _i) {
			struct pkg_job_universe_item *icur = uv->d[_i];
			if (!icur->cudf_emit_skip) {
				pkg = icur->pkg;

				if (cudf_emit_pkg(pkg, version ++, f, uv) != EPKG_OK)
					return (EPKG_FATAL);
			}
		}
	}

	if (fprintf(f, "request: \n") < 0)
			return (EPKG_FATAL);

	switch (t) {
	case PKG_JOBS_FETCH:
	case PKG_JOBS_INSTALL:
	case PKG_JOBS_DEINSTALL:
	case PKG_JOBS_AUTOREMOVE:
		if (cudf_emit_request_packages("install", j, f) != EPKG_OK)
			return (EPKG_FATAL);
		break;
	case PKG_JOBS_UPGRADE:
		if (cudf_emit_request_packages("upgrade", j, f) != EPKG_OK)
			return (EPKG_FATAL);
		break;
	}
	return (EPKG_OK);
}

/*
 * Perform backward conversion of an uid replacing '@' to '_'
 */
static char *
cudf_strdup(const char *in)
{
	size_t len = strlen(in);
	char *out, *d;
	const char *s;

	out = xmalloc(len + 1);

	s = in;
	d = out;
	while (isspace(*s))
		s++;
	while (*s) {
		if (!isspace(*s))
			*d++ = (*s == '@') ? '_' : *s;
		s++;
	}

	*d = '\0';
	return (out);
}

static void
pkg_jobs_cudf_insert_res_job (pkg_solved_list *target,
		struct pkg_job_universe_item *it_new,
		struct pkg_job_universe_item *it_old,
		int type)
{
	struct pkg_solved *res;

	res = xcalloc(1, sizeof(struct pkg_solved));

	res->items[0] = it_new;
	res->type = type;
	if (it_old != NULL)
		res->items[1] = it_old;

	vec_push(target, res);
}

struct pkg_cudf_entry {
	char *uid;
	bool was_installed;
	bool installed;
	char *version;
};

static int
pkg_jobs_cudf_add_package(struct pkg_jobs *j, struct pkg_cudf_entry *entry)
{
	universe_itemv_t *uv;
	struct pkg_job_universe_item *selected = NULL, *old = NULL;
	int ver, n;

	uv = pkg_jobs_universe_find(j->universe, entry->uid);
	if (uv == NULL) {
		pkg_emit_error("package %s is found in CUDF output but not in the universe",
				entry->uid);
		return (EPKG_FATAL);
	}

	/*
	 * Now we need to select an appropriate version. We assume that
	 * the order of packages in list is the same as was passed to the
	 * cudf solver.
	 */
	ver = strtoul(entry->version, NULL, 10);

	n = 1;
	vec_foreach(*uv, _i) {
		if (n == ver) {
			selected = uv->d[_i];
			break;
		}
		n ++;
	}

	if (selected == NULL) {
		pkg_emit_error("package %s-%d is found in CUDF output but the "
				"universe has no such version (only %d versions found)",
				entry->uid, ver, n);
		return (EPKG_FATAL);
	}

	if (n == 1) {
		if (entry->installed && selected->pkg->type != PKG_INSTALLED) {
			pkg_debug(3, "pkg_cudf: schedule installation of %s(%d)",
					entry->uid, ver);
			pkg_jobs_cudf_insert_res_job (&j->jobs, selected, NULL, PKG_SOLVED_INSTALL);
		}
		else if (!entry->installed && selected->pkg->type == PKG_INSTALLED) {
			pkg_debug(3, "pkg_cudf: schedule removing of %s(%d)",
					entry->uid, ver);
			pkg_jobs_cudf_insert_res_job (&j->jobs, selected, NULL, PKG_SOLVED_DELETE);
		}
	}
	else {
		/* Define upgrade */
		vec_foreach(*uv, _i) {
			if (uv->d[_i] != selected) {
				old = uv->d[_i];
				break;
			}
		}
		pkg_debug(3, "pkg_cudf: schedule upgrade of %s(to %d)",
				entry->uid, ver);
		assert(old != NULL);
		/* XXX: this is a hack due to iterators stupidity */
		selected->pkg->old_version = old->pkg->version;
		pkg_jobs_cudf_insert_res_job (&j->jobs, selected, old, PKG_SOLVED_UPGRADE);
	}

	return (EPKG_OK);
}

int
pkg_jobs_cudf_parse_output(struct pkg_jobs *j, FILE *f)
{
	char *line = NULL, *begin, *param, *value;
	size_t linecap = 0;
	struct pkg_cudf_entry cur_pkg;

	memset(&cur_pkg, 0, sizeof(cur_pkg));

	while (getline(&line, &linecap, f) > 0) {
		/* Split line, cut spaces */
		begin = line;
		param = strsep(&begin, ": \t");
		value = begin;
		while(begin != NULL)
			value = strsep(&begin, " \t");

		if (STREQ(param, "package")) {
			if (cur_pkg.uid != NULL) {
				if (pkg_jobs_cudf_add_package(j, &cur_pkg) != EPKG_OK)  {
					free(line);
					return (EPKG_FATAL);
				}
			}
			cur_pkg.uid = cudf_strdup(value);
			cur_pkg.was_installed = false;
			cur_pkg.installed = false;
			cur_pkg.version = NULL;
		}
		else if (STREQ(param, "version")) {
			if (cur_pkg.uid == NULL) {
				pkg_emit_error("version line has no corresponding uid in CUDF output");
				free(line);
				return (EPKG_FATAL);
			}
			cur_pkg.version = cudf_strdup(value);
		}
		else if (STREQ(param, "installed")) {
			if (cur_pkg.uid == NULL) {
				pkg_emit_error("installed line has no corresponding uid in CUDF output");
				free(line);
				return (EPKG_FATAL);
			}
			if (strncmp(value, "true", 4) == 0)
				cur_pkg.installed = true;
		}
		else if (STREQ(param, "was-installed")) {
			if (cur_pkg.uid == NULL) {
				pkg_emit_error("was-installed line has no corresponding uid in CUDF output");
				free(line);
				return (EPKG_FATAL);
			}
			if (strncmp(value, "true", 4) == 0)
				cur_pkg.was_installed = true;
		}
	}

	if (cur_pkg.uid != NULL) {
		if (pkg_jobs_cudf_add_package(j, &cur_pkg) != EPKG_OK)  {
			free(line);
			return (EPKG_FATAL);
		}
	}

	free(line);

	return (EPKG_OK);
}
