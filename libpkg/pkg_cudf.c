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

#define _WITH_GETLINE
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

static inline int
cudf_print_conflict(FILE *f, const char *uid, int ver, bool has_next, int *column)
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
		struct pkg_job_universe_item *conflicts_chain)
{
	struct pkg_dep *dep, *dtmp;
	struct pkg_provide *prov, *ptmp;
	struct pkg_conflict *conflict, *ctmp;
	struct pkg_job_universe_item *u;
	int column = 0, ver;

	if (fprintf(f, "package: ") < 0)
		return (EPKG_FATAL);

	if (cudf_print_package_name(f, pkg->uid) < 0)
		return (EPKG_FATAL);

	if (fprintf(f, "\nversion: %d\n", version) < 0)
		return (EPKG_FATAL);

	if (HASH_COUNT(pkg->deps) > 0) {
		if (fprintf(f, "depends: ") < 0)
			return (EPKG_FATAL);
		HASH_ITER(hh, pkg->deps, dep, dtmp) {
			if (cudf_print_element(f, dep->origin,
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
			if (cudf_print_element(f, prov->provide,
					(prov->hh.next != NULL), &column) < 0) {
				return (EPKG_FATAL);
			}
		}
	}

	column = 0;
	if (HASH_COUNT(pkg->conflicts) > 0 ||
			(conflicts_chain->next != NULL &&
			conflicts_chain->next->priority != INT_MIN)) {
		if (fprintf(f, "conflicts: ") < 0)
			return (EPKG_FATAL);
		HASH_ITER(hh, pkg->conflicts, conflict, ctmp) {
			if (cudf_print_element(f, conflict->uid,
					(conflict->hh.next != NULL), &column) < 0) {
				return (EPKG_FATAL);
			}
		}
		ver = 1;
		LL_FOREACH(conflicts_chain, u) {
			if (u->pkg != pkg && u->priority != INT_MIN) {
				if (cudf_print_conflict(f, pkg->uid, ver,
				   (u->next != NULL && u->next->pkg != pkg), &column) < 0) {
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
	struct pkg_job_request *req, *tmp;
	int column = 0;
	bool printed = false;

	if (fprintf(f, "%s: ", op) < 0)
		return (EPKG_FATAL);
	HASH_ITER(hh, j->request_add, req, tmp) {
		if (req->skip)
			continue;
		if (cudf_print_element(f, req->item->pkg->uid,
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
		if (cudf_print_element(f, req->item->pkg->uid,
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
	int ret;

	ret = pkg_version_cmp(a->pkg->version, b->pkg->version);
	if (ret == 0) {
		/* Ignore remote packages whose versions are equal to ours */
		if (a->pkg->type != PKG_INSTALLED)
			a->priority = INT_MIN;
		else if (b->pkg->type != PKG_INSTALLED)
			b->priority = INT_MIN;
	}


	return (ret);
}

int
pkg_jobs_cudf_emit_file(struct pkg_jobs *j, pkg_jobs_t t, FILE *f)
{
	struct pkg *pkg;
	struct pkg_job_universe_item *it, *itmp, *icur;
	int version;

	if (fprintf(f, "preamble: \n\n") < 0)
		return (EPKG_FATAL);

	HASH_ITER(hh, j->universe->items, it, itmp) {
		/* XXX
		 * Here are dragons:
		 * after sorting it we actually modify the head of the list, but there is
		 * no simple way to update a pointer in uthash, therefore universe hash
		 * contains not a head of list but a random elt of the conflicts chain:
		 * before:
		 * head -> elt1 -> elt2 -> elt3
		 * after:
		 * elt1 -> elt3 -> head -> elt2
		 *
		 * But hash would still point to head whilst the real head is elt1.
		 * So after sorting we need to rotate conflicts chain back to find the new
		 * head.
		 */
		DL_SORT(it, pkg_cudf_version_cmp);

		version = 1;
		LL_FOREACH(it, icur) {
			if (icur->priority != INT_MIN) {
				pkg = icur->pkg;

				if (cudf_emit_pkg(pkg, version ++, f, it) != EPKG_OK)
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

static void
pkg_jobs_cudf_insert_res_job (struct pkg_solved **target,
		struct pkg_job_universe_item *it_new,
		struct pkg_job_universe_item *it_old,
		int type)
{
	struct pkg_solved *res;

	res = calloc(1, sizeof(struct pkg_solved));
	if (res == NULL) {
		pkg_emit_errno("calloc", "pkg_solved");
		return;
	}

	res->items[0] = it_new;
	res->type = type;
	if (it_old != NULL)
		res->items[1] = it_old;

	DL_APPEND(*target, res);
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
	struct pkg_job_universe_item *it, *cur, *selected = NULL, *old = NULL, *head;
	int ver, n;

	it = pkg_jobs_universe_find(j->universe, entry->uid);
	if (it == NULL) {
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

	/* Find the old head, see the comment in `pkg_jobs_cudf_emit_file` */
	cur = it;
	do {
		head = cur;
		cur = cur->prev;
	} while (cur->next != NULL);

	n = 1;
	LL_FOREACH(head, cur) {
		if (n == ver) {
			selected = cur;
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
			j->count ++;
		}
		else if (!entry->installed && selected->pkg->type == PKG_INSTALLED) {
			pkg_debug(3, "pkg_cudf: schedule removing of %s(%d)",
					entry->uid, ver);
			pkg_jobs_cudf_insert_res_job (&j->jobs, selected, NULL, PKG_SOLVED_DELETE);
			j->count ++;
		}
	}
	else {
		/* Define upgrade */
		LL_FOREACH(head, cur) {
			if (cur != selected) {
				old = cur;
				break;
			}
		}
		pkg_debug(3, "pkg_cudf: schedule upgrade of %s(to %d)",
				entry->uid, ver);
		assert(old != NULL);
		/* XXX: this is a hack due to iterators stupidity */
		selected->pkg->old_version = old->pkg->version;
		pkg_jobs_cudf_insert_res_job (&j->jobs, selected, old, PKG_SOLVED_UPGRADE);
		j->count ++;
	}

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
		else if (strcmp(param, "version") == 0) {
			if (cur_pkg.uid == NULL) {
				pkg_emit_error("version line has no corresponding uid in CUDF output");
				free(line);
				return (EPKG_FATAL);
			}
			cur_pkg.version = cudf_strdup(value);
		}
		else if (strcmp(param, "installed") == 0) {
			if (cur_pkg.uid == NULL) {
				pkg_emit_error("installed line has no corresponding uid in CUDF output");
				free(line);
				return (EPKG_FATAL);
			}
			if (strncmp(value, "true", 4) == 0)
				cur_pkg.installed = true;
		}
		else if (strcmp(param, "was-installed") == 0) {
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
