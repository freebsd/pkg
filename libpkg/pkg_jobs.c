#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "pkgdb.h"
#include "pkg_event.h"
#include "pkg_private.h"

int
pkg_jobs_new(struct pkg_jobs **jm)
{
	if ((*jm = calloc(1, sizeof(struct pkg_jobs))) == NULL) {
		EMIT_ERRNO("calloc", "pkg_jobs_new");
		return (EPKG_FATAL);
	}

	STAILQ_INIT(&(*jm)->multi);

	return (EPKG_OK);
}

int
pkg_jobs_new_entry(struct pkg_jobs *jm, struct pkg_jobs_entry **je, pkg_jobs_t t, struct pkgdb *db)
{
	assert(jm != NULL);
	assert(db != NULL);
	assert(t != PKG_JOBS_INSTALL || db->type == PKGDB_REMOTE);

	if((*je = calloc(1, sizeof(struct pkg_jobs_entry))) == NULL) {
		EMIT_ERRNO("calloc", "pkg_jobs_new_entry");
		return (EPKG_FATAL);
	}

	STAILQ_INIT(&(*je)->jobs);
	LIST_INIT(&(*je)->nodes);
	(*je)->db = db;
	(*je)->type = t;

	STAILQ_INSERT_TAIL(&jm->multi, *je, next);

	return (EPKG_OK);
}

void
pkg_jobs_free(struct pkg_jobs *jm)
{
	struct pkg_jobs_entry *je;

	if (jm == NULL)
		return;

	while (!STAILQ_EMPTY(&jm->multi)) {
		je = STAILQ_FIRST(&jm->multi);
		STAILQ_REMOVE_HEAD(&jm->multi, next);
		pkg_jobs_free_entry(je);
	}

	free(jm);
}

void
pkg_jobs_free_entry(struct pkg_jobs_entry *j)
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
pkg_jobs_add(struct pkg_jobs_entry *je, struct pkg *pkg)
{
	assert(je != NULL);
	assert(pkg != NULL);

	STAILQ_INSERT_TAIL(&je->jobs, pkg, next);

	return (EPKG_OK);
}

int
pkg_jobs(struct pkg_jobs *jm, struct pkg_jobs_entry **je)
{
	assert(jm != NULL);

	if (*je == NULL)
		*je = STAILQ_FIRST(&jm->multi);
	else
		*je = STAILQ_NEXT(*je, next);

	if (*je == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

int
pkg_jobs_entry(struct pkg_jobs_entry *je, struct pkg **pkg)
{
	assert(je != NULL);

	pkg_jobs_resolv(je);

	if (*pkg == NULL)
		*pkg = STAILQ_FIRST(&je->jobs);
	else
		*pkg = STAILQ_NEXT(*pkg, next);

	if (*pkg == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

int
pkg_jobs_exists(struct pkg_jobs *jobs, struct pkg *pkg, struct pkg **res)
{
	struct pkg_jobs_entry *je = NULL;
	struct pkg *p = NULL;

	while (pkg_jobs(jobs, &je) == EPKG_OK) {
		p = NULL; /* starts with the first package job */
		while (pkg_jobs_entry(je, &p) == EPKG_OK) {
			if (strcmp(pkg_get(p, PKG_ORIGIN), pkg_get(pkg, PKG_ORIGIN)) == 0) {
				*res = p;
				return (EPKG_FATAL);
			}
		}
	}

	return (EPKG_OK);
}

static int
pkg_jobs_install(struct pkg_jobs_entry *je)
{
	struct pkg *p = NULL;
	const char *cachedir;
	char path[MAXPATHLEN];

	/* Fetch */
	while (pkg_jobs_entry(je, &p) == EPKG_OK) {
		if (pkg_repo_fetch(p) != EPKG_OK)
			return (EPKG_FATAL);
	}

	/* Install */
	cachedir = pkg_config("PKG_CACHEDIR");
	p = NULL;
	while (pkg_jobs_entry(je, &p) == EPKG_OK) {
		snprintf(path, sizeof(path), "%s/%s", cachedir,
				 pkg_get(p, PKG_REPOPATH));

		if (pkg_add(je->db, path) != EPKG_OK) {
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

static int
pkg_jobs_deinstall(struct pkg_jobs_entry *je, int force)
{
	struct pkg *p = NULL;
	int retcode = EPKG_OK;

	while (pkg_jobs_entry(je, &p) == EPKG_OK) {
		retcode = pkg_delete(p, je->db, force);
		if (retcode != EPKG_OK)
			return (retcode);
	}

	return (EPKG_OK);
}

int
pkg_jobs_apply(struct pkg_jobs_entry *je, int force)
{
	if (je->type == PKG_JOBS_INSTALL)
		return (pkg_jobs_install(je));
	if (je->type == PKG_JOBS_DEINSTALL)
		return (pkg_jobs_deinstall(je, force));

	EMIT_PKG_ERROR("%s", "bad jobs argument");
	return (EPKG_FATAL);
}

static struct pkg_jobs_node *
get_node(struct pkg_jobs_entry *je, const char *name, int create)
{
	struct pkg_jobs_node *n = NULL;

	/* XXX hashmap? */
	LIST_FOREACH(n, &je->nodes, entries) {
		if (strcmp(name, pkg_get(n->pkg, PKG_ORIGIN)) == 0) {
			return (n);
		}
	}

	if (create == 0)
		return (NULL);

	n = calloc(1, sizeof(struct pkg_jobs_node));
	LIST_INSERT_HEAD(&je->nodes, n, entries);
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
			n->parents = realloc(n->parents, n->parents_cap *
								  sizeof(struct pkg_jobs_node));
		}
		n->parents[n->parents_len] = p;
		n->parents_len++;
}

static void
add_dep(struct pkg_jobs_entry *je, struct pkg_jobs_node *n)
{
	struct pkg_dep *dep = NULL;
	struct pkg_jobs_node *ndep = NULL;

	while (pkg_deps(n->pkg, &dep) != EPKG_END) {
		ndep = get_node(je, pkg_dep_origin(dep), 1);
		if (ndep->pkg == NULL) {
			ndep->pkg = pkgdb_query_remote(je->db, pkg_dep_origin(dep));
			if (ndep->pkg == NULL)
				EMIT_MISSING_DEP(n->pkg, dep);
			else
				add_dep(je, ndep);
		}
		add_parent(ndep, n);
	}
}

static void
add_rdep(struct pkg_jobs_entry *je, struct pkg_jobs_node *n)
{
	struct pkg_jobs_node *nrdep = NULL;
	struct pkg_dep *rdep = NULL;

	pkgdb_loadrdeps(je->db, n->pkg);

	while (pkg_rdeps(n->pkg, &rdep) == EPKG_OK) {
		nrdep = get_node(je, pkg_dep_origin(rdep), 0);
		if (nrdep != NULL)
			add_parent(nrdep, n);
	}
}

static void
remove_node(struct pkg_jobs_entry *je, struct pkg_jobs_node *n)
{
	struct pkg_jobs_node *np = 0;
	size_t i = 0;

	assert(n->nrefs == 0);

	STAILQ_INSERT_TAIL(&je->jobs, n->pkg, next);

	LIST_REMOVE(n, entries);

	for (i = 0; i < n->parents_len; i++) {
		np = n->parents[i];
		np->nrefs--;
	}
	free(n->parents);
	free(n);
}

int
pkg_jobs_resolv(struct pkg_jobs_entry *je)
{
	struct pkg_jobs_node *n = NULL, *tmp = NULL;
	struct pkg *p = NULL;

	assert(je != NULL);

	if (je->resolved == 1)
		return (EPKG_OK);

	/* Create nodes and remove jobs form the queue */
	while (!STAILQ_EMPTY(&je->jobs)) {
		p = STAILQ_FIRST(&je->jobs);
		STAILQ_REMOVE_HEAD(&je->jobs, next);

		n = get_node(je, pkg_get(p, PKG_ORIGIN), 1);

		n->pkg = p;
	}

	/* Add dependencies into nodes */
	LIST_FOREACH(n, &je->nodes, entries) {
		if (je->type == PKG_JOBS_INSTALL)
			add_dep(je, n);
		if (je->type == PKG_JOBS_DEINSTALL)
			add_rdep(je, n);
	}

	/* Resolv !*/
	do {
		LIST_FOREACH_SAFE(n, &je->nodes, entries, tmp) {
			if (n->nrefs == 0)
				remove_node(je, n);
		}
	} while (!LIST_EMPTY(&je->nodes));

	je->resolved = 1;
	return (EPKG_OK);
}
