/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
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

	if ((*j = calloc(1, sizeof(struct pkg_jobs))) == NULL) {
		pkg_emit_errno("calloc", "pkg_jobs");
		return (EPKG_FATAL);
	}

	(*j)->db = db;
	(*j)->type = t;
	(*j)->solved = false;
	(*j)->flags = PKG_FLAG_NONE;

	return (EPKG_OK);
}

void
pkg_jobs_set_flags(struct pkg_jobs *j, pkg_flags flags)
{
	j->flags = flags;
}

int
pkg_jobs_set_repository(struct pkg_jobs *j, const char *name)
{
	/* TODO should validate if the repository exists */
	j->reponame = name;

	return (EPKG_OK);
}

void
pkg_jobs_free(struct pkg_jobs *j)
{
	if (j == NULL)
		return;

	if ((j->flags & PKG_FLAG_DRY_RUN) == 0)
		pkgdb_release_lock(j->db);

	HASH_FREE(j->jobs, pkg, pkg_free);
	LL_FREE(j->patterns, job_pattern, free);

	free(j);
}

int
pkg_jobs_add(struct pkg_jobs *j, match_t match, char **argv, int argc)
{
	struct job_pattern *jp;

	if (j->solved) {
		pkg_emit_error("The job has already been solved. "
		    "Impossible to append new elements");
		return (EPKG_FATAL);
	}

	jp = malloc(sizeof(struct job_pattern));
	jp->pattern = argv;
	jp->nb = argc;
	jp->match = match;
	LL_APPEND(j->patterns, jp);

	return (EPKG_OK);
}

static int
jobs_solve_deinstall(struct pkg_jobs *j)
{
	struct job_pattern *jp = NULL;
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	char *origin;
	bool recursive = false;

	if ((j->flags & PKG_FLAG_RECURSIVE) == PKG_FLAG_RECURSIVE)
		recursive = true;

	LL_FOREACH(j->patterns, jp) {
		if ((it = pkgdb_query_delete(j->db, jp->match, jp->nb,
		    jp->pattern, recursive)) == NULL)
			return (EPKG_FATAL);

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
			pkg_get(pkg, PKG_ORIGIN, &origin);
			HASH_ADD_KEYPTR(hh, j->jobs, origin, strlen(origin), pkg);
			pkg = NULL;
		}
		pkgdb_it_free(it);
	}
	j->solved = true;

	return( EPKG_OK);
}

static int
jobs_solve_autoremove(struct pkg_jobs *j)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	char *origin;

	if ((it = pkgdb_query_autoremove(j->db)) == NULL)
		return (EPKG_FATAL);

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		pkg_get(pkg, PKG_ORIGIN, &origin);
		HASH_ADD_KEYPTR(hh, j->jobs, origin, strlen(origin), pkg);
		pkg = NULL;
	}
	pkgdb_it_free(it);
	j->solved = true;

	return (EPKG_OK);
}

static int
jobs_solve_upgrade(struct pkg_jobs *j)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	char *origin;
	bool all = false;
	bool pkgversiontest = false;
	unsigned flags = PKG_LOAD_BASIC;

	if ((j->flags & PKG_FLAG_FORCE) != 0)
		all = true;

	if ((j->flags & PKG_FLAG_PKG_VERSION_TEST) != 0)
		pkgversiontest = true;

	if ((j->flags & PKG_FLAG_WITH_DEPS) != 0)
		flags |= PKG_LOAD_DEPS;

	if ((it = pkgdb_query_upgrades(j->db, j->reponame, all,
	        pkgversiontest)) == NULL)
		return (EPKG_FATAL);

	while (pkgdb_it_next(it, &pkg, flags) == EPKG_OK) {
		pkg_get(pkg, PKG_ORIGIN, &origin);
		HASH_ADD_KEYPTR(hh, j->jobs, origin, strlen(origin), pkg);
		pkg = NULL;
	}
	pkgdb_it_free(it);
	j->solved = true;

	return (EPKG_OK);
}

static int
jobs_solve_install(struct pkg_jobs *j)
{
	struct job_pattern *jp = NULL;
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	char *origin;
	bool force = false;
	bool recursive = false;
	bool pkgversiontest = false;
	int retcode = EPKG_OK;

	if ((j->flags & PKG_FLAG_FORCE) != 0)
		force = true;

	if ((j->flags & PKG_FLAG_RECURSIVE) != 0)
		recursive = true;

	if ((j->flags & PKG_FLAG_PKG_VERSION_TEST) != 0)
		pkgversiontest = true;

	LL_FOREACH(j->patterns, jp) {
		if ((it = pkgdb_query_installs(j->db, jp->match, jp->nb,
		        jp->pattern, j->reponame, force, recursive,
			pkgversiontest, &retcode)) == NULL)
			return (EPKG_FATAL);

		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_DEPS) == EPKG_OK) {
			pkg_get(pkg, PKG_ORIGIN, &origin);
			if ((j->flags & PKG_FLAG_AUTOMATIC) == PKG_FLAG_AUTOMATIC)
				pkg_set(pkg, PKG_AUTOMATIC, true);
			HASH_ADD_KEYPTR(hh, j->jobs, origin, strlen(origin), pkg);
			pkg = NULL;
		}
		pkgdb_it_free(it);
	}
	j->solved = true;

	return (retcode);
}

static int
jobs_solve_fetch(struct pkg_jobs *j)
{
	struct job_pattern *jp = NULL;
	struct pkg *pkg = NULL;
	struct pkgdb_it *it;
	char *origin;
	unsigned flag = PKG_LOAD_BASIC;

	if ((j->flags & PKG_FLAG_UPGRADES_FOR_INSTALLED) != 0)
		return (jobs_solve_upgrade(j));

	if ((j->flags & PKG_FLAG_WITH_DEPS) != 0)
		flag |= PKG_LOAD_DEPS;

	LL_FOREACH(j->patterns, jp) {
		if ((it = pkgdb_query_fetch(j->db, jp->match, jp->nb,
		    jp->pattern, j->reponame, flag)) == NULL)
			return (EPKG_FATAL);

		while (pkgdb_it_next(it, &pkg, flag) == EPKG_OK) {
			pkg_get(pkg, PKG_ORIGIN, &origin);
			HASH_ADD_KEYPTR(hh, j->jobs, origin, strlen(origin), pkg);
			pkg = NULL;
		}
		pkgdb_it_free(it);
	}
	j->solved = true;

	return (EPKG_OK);
}

int
pkg_jobs_solve(struct pkg_jobs *j)
{
	bool dry_run = false;

	if ((j->flags & PKG_FLAG_DRY_RUN) == PKG_FLAG_DRY_RUN)
		dry_run = true;

	if (!dry_run && pkgdb_obtain_lock(j->db) != EPKG_OK)
		return (EPKG_FATAL);


	switch (j->type) {
	case PKG_JOBS_AUTOREMOVE:
		return (jobs_solve_autoremove(j));
	case PKG_JOBS_DEINSTALL:
		return (jobs_solve_deinstall(j));
	case PKG_JOBS_UPGRADE:
		return (jobs_solve_upgrade(j));
	case PKG_JOBS_INSTALL:
		return (jobs_solve_install(j));
	case PKG_JOBS_FETCH:
		return (jobs_solve_fetch(j));
	default:
		return (EPKG_FATAL);
	}
}

int
pkg_jobs_find(struct pkg_jobs *j, const char *origin, struct pkg **p)
{
	struct pkg *pkg;

	HASH_FIND_STR(j->jobs, __DECONST(char *, origin), pkg);
	if (pkg == NULL)
		return (EPKG_FATAL);

	if (p != NULL)
		*p = pkg;

	return (EPKG_OK);
}

int
pkg_jobs_count(struct pkg_jobs *j)
{
	assert(j != NULL);

	return (HASH_COUNT(j->jobs));
}

pkg_jobs_t
pkg_jobs_type(struct pkg_jobs *j)
{
	return (j->type);
}

int
pkg_jobs(struct pkg_jobs *j, struct pkg **pkg)
{
	assert(j != NULL);

	HASH_NEXT(j->jobs, (*pkg));
}

static int
pkg_jobs_keep_files_to_del(struct pkg *p1, struct pkg *p2)
{
	struct pkg_file *f = NULL;
	struct pkg_dir *d = NULL;

	while (pkg_files(p1, &f) == EPKG_OK) {
		if (f->keep)
			continue;

		f->keep = pkg_has_file(p2, pkg_file_path(f));
	}

	while (pkg_dirs(p1, &d) == EPKG_OK) {
		if (d->keep)
			continue;

		d->keep = pkg_has_dir(p2, pkg_dir_path(d));
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
	struct pkg *pkg_queue = NULL;
	char path[MAXPATHLEN + 1];
	const char *cachedir = NULL;
	int flags = 0;
	int retcode = EPKG_FATAL;
	int lflags = PKG_LOAD_BASIC | PKG_LOAD_FILES | PKG_LOAD_SCRIPTS |
	    PKG_LOAD_DIRS;
	bool handle_rc = false;

	/* Fetch */
	if (pkg_jobs_fetch(j) != EPKG_OK)
		return (EPKG_FATAL);

	if (pkg_config_string(PKG_CONFIG_CACHEDIR, &cachedir) != EPKG_OK)
		return (EPKG_FATAL);
	
	pkg_config_bool(PKG_CONFIG_HANDLE_RC_SCRIPTS, &handle_rc);

	p = NULL;
	/* Install */
	pkgdb_transaction_begin(j->db->sqlite, "upgrade");

	while (pkg_jobs(j, &p) == EPKG_OK) {
		const char *pkgorigin, *pkgrepopath, *newversion, *origin;
		bool automatic, locked;
		flags = 0;

		pkg_get(p, PKG_ORIGIN, &pkgorigin, PKG_REPOPATH, &pkgrepopath,
		    PKG_NEWVERSION, &newversion, PKG_AUTOMATIC, &automatic);

		if (newversion != NULL) {
			pkg = NULL;
			it = pkgdb_query(j->db, pkgorigin, MATCH_EXACT);
			if (it != NULL) {
				if (pkgdb_it_next(it, &pkg, lflags) == EPKG_OK) {
					pkg_get(pkg, PKG_LOCKED, &locked);
					if (locked) {
						pkg_emit_locked(pkg);
						pkgdb_it_free(it);
						retcode = EPKG_LOCKED;
						pkgdb_transaction_rollback(j->db->sqlite, "upgrade");
						goto cleanup; /* Bail out */
					}

					LL_APPEND(pkg_queue, pkg);
					if ((j->flags & PKG_FLAG_NOSCRIPT) == 0)
						pkg_script_run(pkg,
						    PKG_SCRIPT_PRE_DEINSTALL);
					pkg_get(pkg, PKG_ORIGIN, &origin);
					/*
					 * stop the different related services
					 * if the user wants that and the
					 * service is running
					 */
					if (handle_rc)
						pkg_start_stop_rc_scripts(pkg,
						    PKG_RC_STOP);
					pkgdb_unregister_pkg(j->db, origin);
					pkg = NULL;
				}
				pkgdb_it_free(it);
			}
		}

		it = pkgdb_integrity_conflict_local(j->db, pkgorigin);

		if (it != NULL) {
			pkg = NULL;
			while (pkgdb_it_next(it, &pkg, lflags) == EPKG_OK) {

				pkg_get(pkg, PKG_LOCKED, &locked);
				if (locked) {
					pkg_emit_locked(pkg);
					pkgdb_it_free(it);
					retcode = EPKG_LOCKED;
					pkgdb_transaction_rollback(j->db->sqlite, "upgrade");
					goto cleanup; /* Bail out */
				}

				LL_APPEND(pkg_queue, pkg);
				if ((j->flags & PKG_FLAG_NOSCRIPT) == 0)
					pkg_script_run(pkg,
					    PKG_SCRIPT_PRE_DEINSTALL);
				pkg_get(pkg, PKG_ORIGIN, &origin);
				/*
				 * stop the different related services if the
				 * user wants that and the service is running
				 */
				if (handle_rc)
					pkg_start_stop_rc_scripts(pkg,
					    PKG_RC_STOP);
				pkgdb_unregister_pkg(j->db, origin);
				pkg = NULL;
			}
			pkgdb_it_free(it);
		}
		snprintf(path, sizeof(path), "%s/%s", cachedir, pkgrepopath);

		pkg_open(&newpkg, path);
		if (newversion != NULL) {
			pkg_emit_upgrade_begin(p);
		} else {
			pkg_emit_install_begin(newpkg);
		}
		LL_FOREACH(pkg_queue, pkg)
			pkg_jobs_keep_files_to_del(pkg, newpkg);

		LL_FOREACH_SAFE(pkg_queue, pkg, pkg_temp) {
			pkg_get(pkg, PKG_ORIGIN, &origin);
			if (strcmp(pkgorigin, origin) == 0) {
				LL_DELETE(pkg_queue, pkg);
				pkg_delete_files(pkg, 1);
				if ((j->flags & PKG_FLAG_NOSCRIPT) == 0)
					pkg_script_run(pkg,
					    PKG_SCRIPT_POST_DEINSTALL);
				pkg_delete_dirs(j->db, pkg, 0);
				pkg_free(pkg);
				break;
			}
		}

		if ((j->flags & PKG_FLAG_FORCE) != 0)
			flags |= PKG_ADD_FORCE;
		if ((j->flags & PKG_FLAG_NOSCRIPT) != 0)
			flags |= PKG_ADD_NOSCRIPT;
		flags |= PKG_ADD_UPGRADE;
		if (automatic)
			flags |= PKG_ADD_AUTOMATIC;

		if (pkg_add(j->db, path, flags) != EPKG_OK) {
			pkgdb_transaction_rollback(j->db->sqlite, "upgrade");
			goto cleanup;
		}

		if (newversion != NULL)
			pkg_emit_upgrade_finished(p);
		else
			pkg_emit_install_finished(newpkg);

		if (pkg_queue == NULL) {
			pkgdb_transaction_commit(j->db->sqlite, "upgrade");
			pkgdb_transaction_begin(j->db->sqlite, "upgrade");
		}
	}

	retcode = EPKG_OK;

	cleanup:
	pkgdb_transaction_commit(j->db->sqlite, "upgrade");
	pkg_free(newpkg);

	return (retcode);
}

static int
pkg_jobs_deinstall(struct pkg_jobs *j)
{
	struct pkg *p = NULL;
	int retcode;
	int flags = 0;

	if ((j->flags & PKG_FLAG_DRY_RUN) != 0)
		return (EPKG_OK); /* Do nothing */

	if ((j->flags & PKG_FLAG_FORCE) != 0)
		flags = PKG_DELETE_FORCE;

	if ((j->flags & PKG_FLAG_NOSCRIPT) != 0)
		flags |= PKG_DELETE_NOSCRIPT;

	while (pkg_jobs(j, &p) == EPKG_OK) {
		retcode = pkg_delete(p, j->db, flags);

		if (retcode != EPKG_OK)
			return (retcode);
	}

	return (EPKG_OK);
}

int
pkg_jobs_apply(struct pkg_jobs *j)
{
	int rc;

	if (!j->solved) {
		pkg_emit_error("The jobs hasn't been solved");
		return (EPKG_FATAL);
	}

	switch (j->type) {
	case PKG_JOBS_INSTALL:
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PRE_INSTALL, j, j->db);
		rc = pkg_jobs_install(j);
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_POST_INSTALL, j, j->db);
		break;
	case PKG_JOBS_DEINSTALL:
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PRE_DEINSTALL, j, j->db);
		rc = pkg_jobs_deinstall(j);
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_POST_DEINSTALL, j, j->db);
		break;
	case PKG_JOBS_FETCH:
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PRE_FETCH, j, j->db);
		rc = pkg_jobs_fetch(j);
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_POST_FETCH, j, j->db);
		break;
	case PKG_JOBS_UPGRADE:
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PRE_UPGRADE, j, j->db);
		rc = pkg_jobs_install(j);
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_POST_UPGRADE, j, j->db);
		break;
	case PKG_JOBS_AUTOREMOVE:
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PRE_AUTOREMOVE, j, j->db);
		rc = pkg_jobs_deinstall(j);
		pkg_plugins_hook_run(PKG_PLUGIN_HOOK_POST_AUTOREMOVE, j, j->db);
		break;
	default:
		rc = EPKG_FATAL;
		pkg_emit_error("bad jobs argument");
	}

	return (rc);
}

static int
pkg_jobs_fetch(struct pkg_jobs *j)
{
	struct pkg *p = NULL;
	struct pkg *pkg = NULL;
	struct statfs fs;
	struct stat st;
	char path[MAXPATHLEN + 1];
	int64_t dlsize = 0;
	const char *cachedir = NULL;
	const char *repopath = NULL;
	char cachedpath[MAXPATHLEN];
	int ret = EPKG_OK;
	
	if (pkg_config_string(PKG_CONFIG_CACHEDIR, &cachedir) != EPKG_OK)
		return (EPKG_FATAL);

	/* check for available size to fetch */
	while (pkg_jobs(j, &p) == EPKG_OK) {
		int64_t pkgsize;
		pkg_get(p, PKG_NEW_PKGSIZE, &pkgsize, PKG_REPOPATH, &repopath);
		snprintf(cachedpath, MAXPATHLEN, "%s/%s", cachedir, repopath);
		if (stat(cachedpath, &st) == -1)
			dlsize += pkgsize;
		else
			dlsize += pkgsize - st.st_size;
	}

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
		int64_t fsize = (int64_t)fs.f_bsize * (int64_t)fs.f_bfree;
		char dlsz[7], fsz[7];

		humanize_number(dlsz, sizeof(dlsz), dlsize, "B", HN_AUTOSCALE, 0);
		humanize_number(fsz, sizeof(fsz), fsize, "B", HN_AUTOSCALE, 0);
		pkg_emit_error("Not enough space in %s, needed %s available %s",
		    cachedir, dlsz, fsz);
		return (EPKG_FATAL);
	}

	if ((j->flags & PKG_FLAG_DRY_RUN) != 0)
		return (EPKG_OK); /* don't download anything */

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
		if (pkg_open(&pkg, path) != EPKG_OK) {
			return (EPKG_FATAL);
		}

		if (pkgdb_integrity_append(j->db, pkg) != EPKG_OK)
			ret = EPKG_FATAL;
	}

	pkg_free(pkg);

	if (pkgdb_integrity_check(j->db) != EPKG_OK || ret != EPKG_OK)
		return (EPKG_FATAL);

	pkg_emit_integritycheck_finished();

	return (EPKG_OK);
}
