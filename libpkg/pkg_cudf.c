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
#define _WITH_GETLINE
#include <stdio.h>
#include <ctype.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"

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
cudf_print_element(FILE *f, const char *line, bool has_next, int *column)
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

static int
cudf_emit_pkg(struct pkg *pkg, int version, FILE *f)
{
	const char *origin;
	struct pkg_dep *dep, *dtmp;
	struct pkg_provide *prov, *ptmp;
	struct pkg_conflict *conflict, *ctmp;
	int column = 0;

	pkg_get(pkg, PKG_ORIGIN, &origin);
	if (fprintf(f, "package: ") < 0)
		return (EPKG_FATAL);

	if (cudf_print_package_name(f, origin) < 0)
		return (EPKG_FATAL);

	if (fprintf(f, "\nversion: %d\n", version) < 0)
		return (EPKG_FATAL);

	if (HASH_COUNT(pkg->deps) > 0) {
		if (fprintf(f, "depends: ") < 0)
			return (EPKG_FATAL);
		HASH_ITER(hh, pkg->deps, dep, dtmp) {
			if (cudf_print_element(f, pkg_dep_get(dep, PKG_DEP_ORIGIN),
					(dep->hh.next != NULL), &column) < 0) {
				return (EPKG_FATAL);
			}
		}
	}

	column = 0;
	if (HASH_COUNT(pkg->provides) > 0) {
		if (fprintf(f, "provides: ") < 0)
			return (EPKG_FATAL);
		HASH_ITER(hh, pkg->provides, prov, ptmp) {
			if (cudf_print_element(f, pkg_provide_name(prov),
					(prov->hh.next != NULL), &column) < 0) {
				return (EPKG_FATAL);
			}
		}
	}

	column = 0;
	if (HASH_COUNT(pkg->conflicts) > 0) {
		if (fprintf(f, "conflicts: ") < 0)
			return (EPKG_FATAL);
		HASH_ITER(hh, pkg->conflicts, conflict, ctmp) {
			if (cudf_print_element(f, pkg_conflict_origin(conflict),
					(conflict->hh.next != NULL), &column) < 0) {
				return (EPKG_FATAL);
			}
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
	struct pkg_job_request *req, *tmp;
	const char *origin;
	int column = 0;
	bool printed = false;

	if (fprintf(f, "%s: ", op) < 0)
		return (EPKG_FATAL);
	HASH_ITER(hh, j->request_add, req, tmp) {
		if (req->skip)
			continue;
		pkg_get(req->pkg, PKG_ORIGIN, &origin);
		if (cudf_print_element(f, origin,
				(req->hh.next != NULL), &column) < 0) {
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
	HASH_ITER(hh, j->request_delete, req, tmp) {
		if (req->skip)
			continue;
		pkg_get(req->pkg, PKG_ORIGIN, &origin);
		if (cudf_print_element(f, origin,
				(req->hh.next != NULL), &column) < 0) {
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
pkg_cudf_version_cmp(struct pkg_job_universe_item *a, struct pkg_job_universe_item *b)
{
	const char *vera, *verb;

	pkg_get(a->pkg, PKG_VERSION, &vera);
	pkg_get(b->pkg, PKG_VERSION, &verb);

	return (pkg_version_cmp(vera, verb));
}

int
pkg_jobs_cudf_emit_file(struct pkg_jobs *j, pkg_jobs_t t, FILE *f)
{
	struct pkg *pkg;
	struct pkg_job_universe_item *it, *itmp, *icur;
	int version;

	if (fprintf(f, "preamble: \n\n") < 0)
		return (EPKG_FATAL);

	HASH_ITER(hh, j->universe, it, itmp) {
		LL_SORT(it, pkg_cudf_version_cmp);
		version = 1;
		LL_FOREACH(it, icur) {
			pkg = icur->pkg;
			if (cudf_emit_pkg(pkg, version++, f) != EPKG_OK)
				return (EPKG_FATAL);
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
 * Perform backward conversion of an origin replacing '@' to '_'
 */
static char *
cudf_strdup(const char *in)
{
	size_t len = strlen(in);
	char *out, *d;
	const char *s;

	out = malloc(len + 1);
	if (out == NULL)
		return (NULL);

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

struct pkg_cudf_entry {
	char *origin;
	bool was_installed;
	bool installed;
	char *version;
};

static int
pkg_jobs_cudf_add_package(struct pkg_jobs *j, struct pkg_cudf_entry *entry)
{
	struct pkg_job_universe_item *it, *cur, *selected = NULL;
	const char *origin;
	int ver;

	HASH_FIND(hh, j->universe, entry->origin, strlen(entry->origin), it);
	if (it == NULL) {
		pkg_emit_error("package %s is found in CUDF output but not in the universe", entry->origin);
		return (EPKG_FATAL);
	}

	/*
	 * Now we need to select an appropriate version. We assume that
	 * the order of packages in list is the same as was passed to the
	 * cudf solver.
	 */
	ver = strtoul(entry->version, NULL, 10);

	LL_FOREACH(it, cur) {
		if (--ver == 0) {
			selected = cur;
			break;
		}
	}

	if (selected == NULL) {
		pkg_emit_error("package %s is found in CUDF output but the universe has no such version", entry->origin);
		return (EPKG_FATAL);
	}

	pkg_get(selected->pkg, PKG_ORIGIN, &origin);
	/* XXX: handle forced versions here including reinstall */
	if (entry->installed && selected->pkg->type != PKG_INSTALLED)
		HASH_ADD_KEYPTR(hh, j->jobs_add, origin, strlen(origin), selected->pkg);
	else if (!entry->installed && selected->pkg->type == PKG_INSTALLED)
		HASH_ADD_KEYPTR(hh, j->jobs_delete, origin, strlen(origin), selected->pkg);

	return (EPKG_OK);
}

int
pkg_jobs_cudf_parse_output(struct pkg_jobs *j, FILE *f)
{
	char *line = NULL, *begin, *param, *value;
	size_t linecap = 0;
	ssize_t linelen;
	struct pkg_cudf_entry cur_pkg;

	memset(&cur_pkg, 0, sizeof(cur_pkg));

	while ((linelen = getline(&line, &linecap, f)) > 0) {
		/* Split line, cut spaces */
		begin = line;
		param = strsep(&begin, ": \t");
		value = begin;
		while(begin != NULL)
			value = strsep(&begin, " \t");

		if (strcmp(param, "package") == 0) {
			if (cur_pkg.origin != NULL) {
				if (pkg_jobs_cudf_add_package(j, &cur_pkg) != EPKG_OK)  {
					free(line);
					return (EPKG_FATAL);
				}
			}
			cur_pkg.origin = cudf_strdup(value);
			cur_pkg.was_installed = false;
			cur_pkg.installed = false;
			cur_pkg.version = NULL;
		}
		else if (strcmp(param, "version") == 0) {
			if (cur_pkg.origin == NULL) {
				pkg_emit_error("version line has no corresponding origin in CUDF output");
				free(line);
				return (EPKG_FATAL);
			}
			cur_pkg.version = cudf_strdup(value);
		}
		else if (strcmp(param, "installed") == 0) {
			if (cur_pkg.origin == NULL) {
				pkg_emit_error("installed line has no corresponding origin in CUDF output");
				free(line);
				return (EPKG_FATAL);
			}
			if (strncmp(value, "true", 4) == 0)
				cur_pkg.installed = true;
		}
		else if (strcmp(param, "was-installed") == 0) {
			if (cur_pkg.origin == NULL) {
				pkg_emit_error("was-installed line has no corresponding origin in CUDF output");
				free(line);
				return (EPKG_FATAL);
			}
			if (strncmp(value, "true", 4) == 0)
				cur_pkg.was_installed = true;
		}
	}

	if (cur_pkg.origin != NULL) {
		if (pkg_jobs_cudf_add_package(j, &cur_pkg) != EPKG_OK)  {
			free(line);
			return (EPKG_FATAL);
		}
	}

	if (line != NULL)
		free(line);

	return (EPKG_OK);
}
