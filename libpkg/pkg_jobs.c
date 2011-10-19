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

	STAILQ_INSERT_TAIL(&j->jobs, pkg, next);

	return (EPKG_OK);
}

int
pkg_jobs_is_empty(struct pkg_jobs *j)
{
	assert(j != NULL);

	return (STAILQ_EMPTY(&j->jobs));
}

int
pkg_jobs(struct pkg_jobs *j, struct pkg **pkg)
{
	assert(j != NULL);

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
	struct pkg *pkg = NULL;
	struct sbuf *buf = sbuf_new_auto();
	const char *cachedir;
	char path[MAXPATHLEN + 1];
	int ret = EPKG_OK;

	/* Fetch */
	while (pkg_jobs(j, &p) == EPKG_OK) {
		if (pkg_repo_fetch(p) != EPKG_OK)
			return (EPKG_FATAL);
	}

	cachedir = pkg_config("PKG_CACHEDIR");
	p = NULL;
	/* integrity checking */
	pkg_emit_integritycheck_begin();

	while (pkg_jobs(j, &p) == EPKG_OK) {
		snprintf(path, sizeof(path), "%s/%s", cachedir,
				pkg_get(p, PKG_REPOPATH));
		if (pkg_open(&pkg, path, buf) != EPKG_OK)
			return (EPKG_FATAL);

		ret = pkgdb_integrity_append(j->db, pkg);
	}

	pkg_free(pkg);
	sbuf_delete(buf);

	if (pkgdb_integrity_check(j->db) != EPKG_OK || ret != EPKG_OK)
		return (EPKG_FATAL);

	pkg_emit_integritycheck_finished();
	p = NULL;
	/* Install */
	while (pkg_jobs(j, &p) == EPKG_OK) {
		printf("%s\n", path);
		snprintf(path, sizeof(path), "%s/%s", cachedir,
				 pkg_get(p, PKG_REPOPATH));

		if (pkg_get(p, PKG_NEWVERSION) != NULL) {
			p->type = PKG_INSTALLED;
			if (pkg_delete2(p, j->db, 1, 0) != EPKG_OK)
				return (EPKG_FATAL);
		}

		if (pkg_add2(j->db, path, 0, pkg_is_automatic(p)) != EPKG_OK)
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
