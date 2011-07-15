#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "pkgdb.h"
#include "pkg_error.h"
#include "pkg_private.h"

int
pkg_jobs_new(struct pkg_jobs **j, struct pkgdb *db)
{
	if (db == NULL || db->type != PKGDB_REMOTE)
		return (ERROR_BAD_ARG("db"));

	if((*j = calloc(1, sizeof(struct pkg_jobs))) == NULL)
		err(1, "calloc()");

	STAILQ_INIT(&(*j)->jobs);
	LIST_INIT(&(*j)->nodes);
	(*j)->db = db;

	return (EPKG_OK);
}

void
pkg_jobs_free(struct pkg_jobs *j)
{
	struct pkg *p;

	if (j == NULL)
		return;

	while (!STAILQ_EMPTY(&j->jobs)) {
		p = STAILQ_FIRST(&j->jobs);
		STAILQ_REMOVE_HEAD(&j->jobs, next);
		pkg_free(p);
	}
	free(j);
}

int
pkg_jobs_add(struct pkg_jobs *j, struct pkg *pkg)
{
	if (j == NULL)
		return (ERROR_BAD_ARG("jobs"));
	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	STAILQ_INSERT_TAIL(&j->jobs, pkg, next);

	return (EPKG_OK);
}

int
pkg_jobs(struct pkg_jobs *j, struct pkg **pkg)
{
	if (j == NULL)
		return (ERROR_BAD_ARG("jobs"));

	pkg_jobs_resolv(j, 0);

	if (*pkg == NULL)
		*pkg = STAILQ_FIRST(&j->jobs);
	else
		*pkg = STAILQ_NEXT(*pkg, next);

	if (*pkg == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

int
pkg_jobs_apply(struct pkg_jobs *j, status_cb scb)
{
	struct pkg *p = NULL;
	struct pkg *pfile = NULL;
	const char *cachedir;
	char path[MAXPATHLEN];

	/* Fetch */
	while (pkg_jobs(j, &p) == EPKG_OK) {
		if (pkg_repo_fetch(p) != EPKG_OK)
			return (EPKG_FATAL);
	}

	/* Install */
	cachedir = pkg_config("PKG_CACHEDIR");
	p = NULL;
	while (pkg_jobs(j, &p) == EPKG_OK) {
		snprintf(path, sizeof(path), "%s/%s", cachedir,
				 pkg_get(p, PKG_REPOPATH));

		if (scb != NULL)
			scb(NULL, p);
		if (pkg_add(j->db, path, &pfile) != EPKG_OK) {
			pkg_free(pfile);
			return (EPKG_FATAL);
		}
		if (scb != NULL)
			scb(NULL, pfile);
	}

	pkg_free(pfile);
	return (EPKG_OK);
}

static struct pkg_jobs_node *
get_node(struct pkg_jobs *j, const char *name)
{
	struct pkg_jobs_node *n;

	/* XXX hashmap? */
	LIST_FOREACH(n, &j->nodes, entries) {
		if (strcmp(name, pkg_get(n->pkg, PKG_ORIGIN)) == 0) {
			return (n);
		}
	}

	n = calloc(1, sizeof(struct pkg_jobs_node));
	LIST_INSERT_HEAD(&j->nodes, n, entries);
	return (n);
}

static void
add_dep(struct pkg_jobs *j, struct pkg_jobs_node *n)
{
	struct pkg_dep *d = NULL;
	struct pkg_jobs_node *nd;

	while (pkg_deps(n->pkg, &d) != EPKG_END) {
		n->nrefs++;
		nd = get_node(j, pkg_dep_origin(d));

		if (nd->pkg == NULL) {
			/* XXX should query with origin */
			nd->pkg = pkgdb_query_remote(j->db, pkg_dep_origin(d));
			if (nd->pkg == NULL)
				err(1, "%s", pkg_error_string());
			add_dep(j, nd);
		}

		if (nd->parents_len == nd->parents_cap) {
			if (nd->parents_cap == 0)
				nd->parents_cap = 5;
			else
				nd->parents_cap *= 2;
			nd->parents = realloc(nd->parents, nd->parents_cap *
								  sizeof(struct pkg_jobs_node));
		}
		nd->parents[nd->parents_len] = n;
		nd->parents_len++;
	}
}

static void
remove_node(struct pkg_jobs *j, struct pkg_jobs_node *n)
{
	struct pkg_jobs_node *np;
	size_t i;

	assert(n->nrefs == 0);

	if (j->reverse == 1)
		STAILQ_INSERT_HEAD(&j->jobs, n->pkg, next);
	else
		STAILQ_INSERT_TAIL(&j->jobs, n->pkg, next);

	LIST_REMOVE(n, entries);

	for (i = 0; i < n->parents_len; i++) {
		np = n->parents[i];
		np->nrefs--;
	}
	free(n->parents);
	free(n);
}

int
pkg_jobs_resolv(struct pkg_jobs *j, int reverse)
{
	struct pkg_jobs_node *n, *tmp;
	struct pkg *p;

	if (j == NULL)
		return (ERROR_BAD_ARG("jobs"));

	if (j->resolved == 1)
		return (EPKG_OK);

	j->reverse = reverse;

	/* Create nodes and remove jobs form the queue */
	while (!STAILQ_EMPTY(&j->jobs)) {
		p = STAILQ_FIRST(&j->jobs);
		STAILQ_REMOVE_HEAD(&j->jobs, next);

		n = get_node(j, pkg_get(p, PKG_ORIGIN));

		n->pkg = p;
	}

	/* Add dependencies into nodes */
	LIST_FOREACH(n, &j->nodes, entries) {
		add_dep(j, n);
	}

	/* Resolv !*/
	do {
		LIST_FOREACH_SAFE(n, &j->nodes, entries, tmp) {
			if (n->nrefs == 0)
				remove_node(j, n);
		}
	} while (!LIST_EMPTY(&j->nodes));

	j->resolved = 1;
	return (EPKG_OK);
}
