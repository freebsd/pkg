#include <sys/param.h>
#include <sys/mount.h>

#include <assert.h>
#include <errno.h>
#include <libutil.h>
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
pkg_jobs_keep_files_to_del(struct pkg *p1, struct pkg *p2)
{
	struct pkg_file *f1 = NULL, *f2 = NULL;
	struct pkg_dir *d1 = NULL, *d2 = NULL;

	while (pkg_files(p1, &f1) == EPKG_OK) {
		if (f1->keep == 1)
			continue;

		f2 = NULL;
		while (pkg_files(p2, &f2)) {
			if (strcmp(pkg_file_path(f1), pkg_file_path(f2)) == 0) {
				f1->keep = 1;
				break;
			}
		}
	}

	while (pkg_dirs(p1, &d1) == EPKG_OK) {
		if (d1->keep == 1)
			continue;
		d2 = NULL;
		while (pkg_dirs(p2, &d2)) {
			if (strcmp(pkg_dir_path(d1), pkg_dir_path(d2)) == 0) {
				d1->keep = 1;
				break;
			}
		}
	}

	return (EPKG_OK);
}

static int
pkg_jobs_install(struct pkg_jobs *j)
{
	struct pkg *p = NULL;
	struct pkg *pkg = NULL;
	struct pkg *newpkg = NULL;
	struct pkg *pkg_temp = NULL;
	struct pkgdb_it *it = NULL;
	struct sbuf *buf = sbuf_new_auto();
	STAILQ_HEAD(,pkg) pkg_queue;
	const char *cachedir;
	char path[MAXPATHLEN + 1];
	int ret = EPKG_OK;
	int flags = 0;
	int64_t dlsize = 0;
	struct statfs fs;
	char dlsz[7];
	char fsz[7];

	STAILQ_INIT(&pkg_queue);

	/* check for available size to fetch */
	while (pkg_jobs(j, &p) == EPKG_OK)
		dlsize += pkg_new_pkgsize(p);

	if (pkg_config_string(PKG_CONFIG_CACHEDIR, &cachedir) != EPKG_OK)
		return (EPKG_FATAL);

	while (statfs(cachedir, &fs) == -1) {
		if (errno == ENOENT) {
			if (mkdirs(cachedir) != EPKG_OK)
				return (EPKG_FATAL);
		} else {
			pkg_emit_errno("statfs", cachedir);
			return (EPKG_FATAL);
		}
	}

	if (dlsize > ((int64_t)fs.f_bsize * (int64_t)fs.f_bfree)) {
		humanize_number(dlsz, sizeof(dlsz), dlsize, "B", HN_AUTOSCALE, 0);
		humanize_number(fsz, sizeof(fsz), (int64_t)fs.f_bsize * (int64_t)fs.f_bfree, "B", HN_AUTOSCALE, 0);
		pkg_emit_error("Not enough space in %s, needed %s available %s", cachedir, dlsz, fsz);
		return (EPKG_FATAL);
	}
		
	/* Fetch */
	p = NULL;
	while (pkg_jobs(j, &p) == EPKG_OK) {
		if (pkg_repo_fetch(p) != EPKG_OK)
			return (EPKG_FATAL);
	}

	p = NULL;
	/* integrity checking */
	pkg_emit_integritycheck_begin();

	while (pkg_jobs(j, &p) == EPKG_OK) {
		snprintf(path, sizeof(path), "%s/%s", cachedir,
				pkg_get(p, PKG_REPOPATH));
		if (pkg_open(&pkg, path, buf) != EPKG_OK)
			return (EPKG_FATAL);

		if (pkgdb_integrity_append(j->db, pkg) != EPKG_OK)
			ret = EPKG_FATAL;
	}

	pkg_free(pkg);
	sbuf_delete(buf);

	if (pkgdb_integrity_check(j->db) != EPKG_OK || ret != EPKG_OK)
		return (EPKG_FATAL);

	pkg_emit_integritycheck_finished();
	p = NULL;
	/* Install */
	sql_exec(j->db->sqlite, "SAVEPOINT upgrade;");
	while (pkg_jobs(j, &p) == EPKG_OK) {
		flags = 0;
		it = pkgdb_integrity_conflict_local(j->db, pkg_get(p, PKG_ORIGIN));

		if (it != NULL) {
			pkg = NULL;
			while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_FILES|PKG_LOAD_SCRIPTS|PKG_LOAD_DIRS) == EPKG_OK) {
				STAILQ_INSERT_TAIL(&pkg_queue, pkg, next);
				pkg_script_run(pkg, PKG_SCRIPT_PRE_DEINSTALL);
				pkgdb_unregister_pkg(j->db, pkg_get(pkg, PKG_ORIGIN));
				pkg = NULL;
			}
			pkgdb_it_free(it);
		}
		snprintf(path, sizeof(path), "%s/%s", cachedir,
				 pkg_get(p, PKG_REPOPATH));

		pkg_open(&newpkg, path, NULL);
		if (pkg_get(p, PKG_NEWVERSION) != NULL) {
			pkg_emit_upgrade_begin(p);
		} else {
			pkg_emit_install_begin(newpkg);
		}
		STAILQ_FOREACH(pkg, &pkg_queue, next)
			pkg_jobs_keep_files_to_del(pkg, newpkg);

		STAILQ_FOREACH_SAFE(pkg, &pkg_queue, next, pkg_temp) {
			if (strcmp(pkg_get(p, PKG_ORIGIN), pkg_get(pkg, PKG_ORIGIN)) == 0) {
				STAILQ_REMOVE(&pkg_queue, pkg, pkg, next);
				pkg_delete_files(pkg, 1);
				pkg_script_run(pkg, PKG_SCRIPT_POST_DEINSTALL);
				pkg_delete_dirs(j->db, pkg, 0);
				pkg_free(pkg);
				break;
			}
		}

		flags |= PKG_ADD_UPGRADE;
		if (pkg_is_automatic(p))
			flags |= PKG_ADD_AUTOMATIC;

		if (pkg_add(j->db, path, flags) != EPKG_OK) {
			sql_exec(j->db->sqlite, "ROLLBACK TO upgrade;");
			return (EPKG_FATAL);
		}

		if (pkg_get(p, PKG_NEWVERSION) != NULL)
			pkg_emit_upgrade_finished(p);
		else
			pkg_emit_install_finished(newpkg);

		if (STAILQ_EMPTY(&pkg_queue)) {
			sql_exec(j->db->sqlite, "RELEASE upgrade;");
			sql_exec(j->db->sqlite, "SAVEPOINT upgrade;");
		}
	}
	sql_exec(j->db->sqlite, "RELEASE upgrade;");

	pkg_free(newpkg);

	return (EPKG_OK);
}

static int
pkg_jobs_deinstall(struct pkg_jobs *j, int force)
{
	struct pkg *p = NULL;
	int retcode;

	while (pkg_jobs(j, &p) == EPKG_OK) {
		if (force)
			retcode = pkg_delete(p, j->db, PKG_DELETE_FORCE);
		else
			retcode = pkg_delete(p, j->db, 0);
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
