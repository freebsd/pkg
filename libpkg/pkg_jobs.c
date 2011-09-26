#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "pkgdb.h"
#include "pkg_event.h"
#include "pkg_private.h"

int
pkg_jobs_new(struct pkg_jobs **j, pkg_jobs_t t, struct pkgdb *db)
{
	assert(db != NULL);
	assert(t != PKG_JOBS_INSTALL || db->type == PKGDB_REMOTE);

	if((*j = calloc(1, sizeof(struct pkg_jobs))) == NULL) {
		pkg_emit_errno("calloc", "pkg_jobs");
		return (EPKG_FATAL);
	}

	STAILQ_INIT(&(*j)->jobs);
	LIST_INIT(&(*j)->nodes);
	(*j)->db = db;
	(*j)->type = t;

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
	assert(j != NULL);
	assert(pkg != NULL);
	assert(j->resolved != 1);

	STAILQ_INSERT_TAIL(&j->jobs, pkg, next);

	return (EPKG_OK);
}

int
pkg_jobs_isempty(struct pkg_jobs *j)
{
	assert(j != NULL);

	pkg_jobs_resolv(j);

	return (STAILQ_EMPTY(&j->jobs));
}

int
pkg_jobs(struct pkg_jobs *j, struct pkg **pkg)
{
	assert(j != NULL);

	pkg_jobs_resolv(j);

	if (*pkg == NULL)
		*pkg = STAILQ_FIRST(&j->jobs);
	else
		*pkg = STAILQ_NEXT(*pkg, next);

	if (*pkg == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

static int
pkg_jobs_install(struct pkg_jobs *j)
{
	struct pkg *p = NULL;
	const char *cachedir;
	char path[MAXPATHLEN + 1];

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

		if (pkg_get(p, PKG_NEWVERSION) != NULL) {
			p->type = PKG_INSTALLED;
			if (pkg_delete2(p, j->db, 1, 0) != EPKG_OK)
				return (EPKG_FATAL);
		}

		if (pkg_add2(j->db, path, 0, pkg_isautomatic(p)) != EPKG_OK)
			return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

static int
pkg_jobs_deinstall(struct pkg_jobs *j, int force)
{
	struct pkg *p = NULL;
	int retcode;

	while (pkg_jobs(j, &p) == EPKG_OK) {
		retcode = pkg_delete(p, j->db, force);
		if (retcode != EPKG_OK)
			return (retcode);
	}

	return (EPKG_OK);
}

int
pkg_jobs_apply(struct pkg_jobs *j, int force)
{
	if (j->type == PKG_JOBS_INSTALL)
		return (pkg_jobs_install(j));
	if (j->type == PKG_JOBS_DEINSTALL)
		return (pkg_jobs_deinstall(j, force));

	pkg_emit_error("bad jobs argument");
	return (EPKG_FATAL);
}

static struct pkg_jobs_node *
get_node(struct pkg_jobs *j, const char *name, int create)
{
	struct pkg_jobs_node *n;

	/* XXX hashmap? */
	LIST_FOREACH(n, &j->nodes, entries) {
		if (strcmp(name, pkg_get(n->pkg, PKG_ORIGIN)) == 0) {
			return (n);
		}
	}

	if (create == 0)
		return (NULL);

	if ((n = calloc(1, sizeof(struct pkg_jobs_node))) == NULL) {
		pkg_emit_errno("calloc", "pkg_jobs_node");
		return (NULL);
	}

	LIST_INSERT_HEAD(&j->nodes, n, entries);
	return (n);
}

static void
add_parent(struct pkg_jobs_node *n, struct pkg_jobs_node *p)
{
		p->nrefs++;

		if (n->parents_len == n->parents_cap) {
			if (n->parents_cap == 0)
				n->parents_cap = 5;
			else
				n->parents_cap *= 2;
			if ((n->parents = reallocf(n->parents,
					n->parents_cap * sizeof(struct pkg_jobs_node))) == NULL) {
					pkg_emit_errno("realloc", "pkg_jobs_node");
					return;
			}
		}
		n->parents[n->parents_len] = p;
		n->parents_len++;
}

static void
add_rdep(struct pkg_jobs *j, struct pkg_jobs_node *n)
{
	struct pkg_jobs_node *nrdep;
	struct pkg_dep *rdep = NULL;

	pkgdb_loadrdeps(j->db, n->pkg);

	while (pkg_rdeps(n->pkg, &rdep) == EPKG_OK) {
		nrdep = get_node(j, pkg_dep_origin(rdep), 0);
		if (nrdep != NULL)
			add_parent(nrdep, n);
	}
}

static void
remove_node(struct pkg_jobs *j, struct pkg_jobs_node *n)
{
	struct pkg_jobs_node *np;
	size_t i;

	assert(n->nrefs == 0);

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
pkg_jobs_resolv(struct pkg_jobs *j)
{
	struct pkg_jobs_node *n, *tmp;
	struct pkg *p;

	assert(j != NULL);

	if (j->resolved == 1)
		return (EPKG_OK);

	/* Create nodes and remove jobs form the queue */
	while (!STAILQ_EMPTY(&j->jobs)) {
		p = STAILQ_FIRST(&j->jobs);
		STAILQ_REMOVE_HEAD(&j->jobs, next);

		n = get_node(j, pkg_get(p, PKG_ORIGIN), 1);

		if (n->pkg == NULL)
			n->pkg = p;
		else
			pkg_free(p);
	}

	/* Add dependencies into nodes */
	LIST_FOREACH(n, &j->nodes, entries) {
		if (j->type == PKG_JOBS_DEINSTALL)
			add_rdep(j, n);
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
