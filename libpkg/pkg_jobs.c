/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
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

static int pkg_jobs_fetch(struct pkg_jobs *j);

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
			if (strcmp(pkg_file_get(f1, PKG_FILE_PATH), pkg_file_get(f2, PKG_FILE_PATH)) == 0) {
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
	STAILQ_HEAD(,pkg) pkg_queue;
	char path[MAXPATHLEN + 1];
	const char *cachedir = NULL;
	int flags = 0;

	bool handle_rc = false;

	STAILQ_INIT(&pkg_queue);

	/* Fetch */
	pkg_jobs_fetch(j);

	if (pkg_config_string(PKG_CONFIG_CACHEDIR, &cachedir) != EPKG_OK)
		return (EPKG_FATAL);
	
	p = NULL;
	/* Install */
	sql_exec(j->db->sqlite, "SAVEPOINT upgrade;");
	while (pkg_jobs(j, &p) == EPKG_OK) {
		const char *pkgorigin, *pkgrepopath, *newversion, *origin;
		bool automatic;
		flags = 0;

		pkg_get(p, PKG_ORIGIN, &pkgorigin, PKG_REPOPATH, &pkgrepopath,
		    PKG_NEWVERSION, &newversion, PKG_AUTOMATIC, &automatic);

		if (newversion != NULL) {
			pkg = NULL;
			it = pkgdb_query(j->db, pkgorigin, MATCH_EXACT);
			if (it != NULL) {
				if (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_FILES|PKG_LOAD_SCRIPTS|PKG_LOAD_DIRS) == EPKG_OK) {
					STAILQ_INSERT_TAIL(&pkg_queue, pkg, next);
					pkg_script_run(pkg, PKG_SCRIPT_PRE_DEINSTALL);
					pkg_get(pkg, PKG_ORIGIN, &origin);
					/*
					 * stop the different related services if the users do want that
					 * and that the service is running
					 */
					pkg_config_bool(PKG_CONFIG_HANDLE_RC_SCRIPTS, &handle_rc);
					if (handle_rc)
						pkg_start_stop_rc_scripts(pkg, PKG_RC_STOP);
					pkgdb_unregister_pkg(j->db, origin);
					pkg = NULL;
				}
				pkgdb_it_free(it);
			}
		}

		it = pkgdb_integrity_conflict_local(j->db, pkgorigin);

		if (it != NULL) {
			pkg = NULL;
			while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_FILES|PKG_LOAD_SCRIPTS|PKG_LOAD_DIRS) == EPKG_OK) {
				STAILQ_INSERT_TAIL(&pkg_queue, pkg, next);
				pkg_script_run(pkg, PKG_SCRIPT_PRE_DEINSTALL);
				pkg_get(pkg, PKG_ORIGIN, &origin);
				/*
				 * stop the different related services if the users do want that
				 * and that the service is running
				 */
				pkg_config_bool(PKG_CONFIG_HANDLE_RC_SCRIPTS, &handle_rc);
				if (handle_rc)
					pkg_start_stop_rc_scripts(pkg, PKG_RC_STOP);
				pkgdb_unregister_pkg(j->db, origin);
				pkg = NULL;
			}
			pkgdb_it_free(it);
		}
		snprintf(path, sizeof(path), "%s/%s", cachedir, pkgrepopath);

		pkg_open(&newpkg, path, NULL);
		if (newversion != NULL) {
			pkg_emit_upgrade_begin(p);
		} else {
			pkg_emit_install_begin(newpkg);
		}
		STAILQ_FOREACH(pkg, &pkg_queue, next)
			pkg_jobs_keep_files_to_del(pkg, newpkg);

		STAILQ_FOREACH_SAFE(pkg, &pkg_queue, next, pkg_temp) {
			pkg_get(pkg, PKG_ORIGIN, &origin);
			if (strcmp(pkgorigin, origin) == 0) {
				STAILQ_REMOVE(&pkg_queue, pkg, pkg, next);
				pkg_delete_files(pkg, 1);
				pkg_script_run(pkg, PKG_SCRIPT_POST_DEINSTALL);
				pkg_delete_dirs(j->db, pkg, 0);
				pkg_free(pkg);
				break;
			}
		}

		flags |= PKG_ADD_UPGRADE;
		if (automatic)
			flags |= PKG_ADD_AUTOMATIC;

		if (pkg_add(j->db, path, flags) != EPKG_OK) {
			sql_exec(j->db->sqlite, "ROLLBACK TO upgrade;");
			return (EPKG_FATAL);
		}

		if (newversion != NULL)
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
	if (j->type == PKG_JOBS_FETCH)
		return (pkg_jobs_fetch(j));

	pkg_emit_error("bad jobs argument");
	return (EPKG_FATAL);
}

static int
pkg_jobs_fetch(struct pkg_jobs *j)
{
	struct pkg *p = NULL;
	struct pkg *pkg = NULL;
	struct sbuf *buf = sbuf_new_auto();
	struct statfs fs;
	char path[MAXPATHLEN + 1];
	int64_t dlsize = 0;
	const char *cachedir = NULL;
	char dlsz[7];
	char fsz[7];
	int ret = EPKG_OK;
	
	/* check for available size to fetch */
	while (pkg_jobs(j, &p) == EPKG_OK) {
		int64_t pkgsize;
		pkg_get(p, PKG_NEW_PKGSIZE, &pkgsize);
		dlsize += pkgsize;
	}

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
		const char *pkgrepopath;

		pkg_get(p, PKG_REPOPATH, &pkgrepopath);
		snprintf(path, sizeof(path), "%s/%s", cachedir,
		    pkgrepopath);
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

	return (EPKG_OK);
}
