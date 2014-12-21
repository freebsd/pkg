/*
 * Copyright (c) 2014, Vsevolod Stakhov
 * Copyright (c) 2012-2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/stat.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <sys/time.h>

#define _WITH_GETLINE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include <archive.h>
#include <archive_entry.h>

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkgdb.h"
#include "private/pkg.h"
#include "binary.h"
#include "binary_private.h"

static int
pkg_repo_binary_init_update(struct pkg_repo *repo, const char *name)
{
	sqlite3 *sqlite;
	const char update_check_sql[] = ""
					"INSERT INTO repo_update VALUES(1);";
	const char update_start_sql[] = ""
					"CREATE TABLE IF NOT EXISTS repo_update (n INT);";

	/* [Re]create repo */
	unlink(name);
	if (repo->ops->create(repo) != EPKG_OK) {
		pkg_emit_notice("Unable to create repository %s", repo->name);
		return (EPKG_FATAL);
	}
	if (repo->ops->open(repo, R_OK|W_OK) != EPKG_OK) {
		pkg_emit_notice("Unable to open created repository %s", repo->name);
		return (EPKG_FATAL);
	}

	repo->ops->init(repo);

	sqlite = PRIV_GET(repo);

	if(sqlite3_exec(sqlite, update_check_sql, NULL, NULL, NULL) == SQLITE_OK) {
		pkg_emit_notice("Previous update has not been finished, restart it");
		return (EPKG_END);
	}
	else {
		sql_exec(sqlite, update_start_sql);
	}

	return (EPKG_OK);
}

static int
pkg_repo_binary_delete_conflicting(const char *origin, const char *version,
			 const char *pkg_path, bool forced)
{
	int ret = EPKG_FATAL;
	const char *oversion;

	if (pkg_repo_binary_run_prstatement(REPO_VERSION, origin) != SQLITE_ROW) {
		ret = EPKG_FATAL;
		goto cleanup;
	}
	oversion = sqlite3_column_text(pkg_repo_binary_stmt_prstatement(REPO_VERSION), 0);
	if (!forced) {
		switch(pkg_version_cmp(oversion, version)) {
		case -1:
			pkg_emit_error("duplicate package origin: replacing older "
					"version %s in repo with package %s for "
					"origin %s", oversion, pkg_path, origin);

			if (pkg_repo_binary_run_prstatement(DELETE, origin, origin) !=
							SQLITE_DONE)
				ret = EPKG_FATAL;
			else
				ret = EPKG_OK;	/* conflict cleared */

			break;
		case 0:
		case 1:
			pkg_emit_error("duplicate package origin: package %s is not "
					"newer than version %s already in repo for "
					"origin %s", pkg_path, oversion, origin);
			ret = EPKG_END;	/* keep what is already in the repo */
			break;
		}
	}
	else {
		if (pkg_repo_binary_run_prstatement(DELETE, origin, origin) != SQLITE_DONE)
			ret = EPKG_FATAL;

		ret = EPKG_OK;
	}

cleanup:
	sqlite3_reset(pkg_repo_binary_stmt_prstatement(REPO_VERSION));

	return (ret);
}

static int
pkg_repo_binary_add_pkg(struct pkg *pkg, const char *pkg_path,
		sqlite3 *sqlite, bool forced)
{
	int			 ret;
	struct pkg_dep		*dep      = NULL;
	struct pkg_option	*option   = NULL;
	struct pkg_shlib	*shlib    = NULL;
	struct pkg_strel	*el;
	struct pkg_kv		*kv;
	const char		*arch;
	int64_t			 package_id;

	arch = pkg->abi != NULL ? pkg->abi : pkg->arch;

try_again:
	if ((ret = pkg_repo_binary_run_prstatement(PKG,
	    pkg->origin, pkg->name, pkg->version, pkg->comment, pkg->desc,
	    arch, pkg->maintainer, pkg->www, pkg->prefix, pkg->pkgsize,
	    pkg->flatsize, (int64_t)pkg->licenselogic, pkg->sum, pkg->repopath,
	    pkg->digest, pkg->old_digest)) != SQLITE_DONE) {
		if (ret == SQLITE_CONSTRAINT) {
			ERROR_SQLITE(sqlite, "grmbl");
			switch(pkg_repo_binary_delete_conflicting(pkg->origin,
			    pkg->version, pkg_path, forced)) {
			case EPKG_FATAL: /* sqlite error */
				ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(PKG));
				return (EPKG_FATAL);
				break;
			case EPKG_END: /* repo already has newer */
				return (EPKG_END);
				break;
			default: /* conflict cleared, try again */
				goto try_again;
				break;
			}
		} else {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(PKG));
			return (EPKG_FATAL);
		}
	}
	package_id = sqlite3_last_insert_rowid(sqlite);

/*	if (pkg_repo_binary_run_prstatement (FTS_APPEND, package_id,
			name, version, origin) != SQLITE_DONE) {
		ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(FTS_APPEND));
		return (EPKG_FATAL);
	}*/

	dep = NULL;
	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		if (pkg_repo_binary_run_prstatement(DEPS, dep->origin,
		    dep->name, dep->version, package_id) != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(DEPS));
			return (EPKG_FATAL);
		}
	}

	LL_FOREACH(pkg->categories, el) {
		ret = pkg_repo_binary_run_prstatement(CAT1, el->value);
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(CAT2, package_id,
			    el->value);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(CAT2));
			return (EPKG_FATAL);
		}
	}

	LL_FOREACH(pkg->licenses, el) {
		ret = pkg_repo_binary_run_prstatement(LIC1, el->value);
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(LIC2, package_id,
			    el->value);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(LIC2));
			return (EPKG_FATAL);
		}
	}

	option = NULL;
	while (pkg_options(pkg, &option) == EPKG_OK) {
		ret = pkg_repo_binary_run_prstatement(OPT1, option->key);
		if (ret == SQLITE_DONE)
		    ret = pkg_repo_binary_run_prstatement(OPT2, option->key,
				option->value, package_id);
		if(ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(OPT2));
			return (EPKG_FATAL);
		}
	}

	shlib = NULL;
	while (pkg_shlibs_required(pkg, &shlib) == EPKG_OK) {
		ret = pkg_repo_binary_run_prstatement(SHLIB1, shlib->name);
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(SHLIB_REQD, package_id,
					shlib->name);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(SHLIB_REQD));
			return (EPKG_FATAL);
		}
	}

	shlib = NULL;
	while (pkg_shlibs_provided(pkg, &shlib) == EPKG_OK) {
		ret = pkg_repo_binary_run_prstatement(SHLIB1, shlib->name);
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(SHLIB_PROV, package_id,
					shlib->name);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(SHLIB_PROV));
			return (EPKG_FATAL);
		}
	}

	LL_FOREACH(pkg->annotations, kv) {
		ret = pkg_repo_binary_run_prstatement(ANNOTATE1, kv->key);
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(ANNOTATE1, kv->value);
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(ANNOTATE2, package_id,
				  kv->key, kv->value);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(ANNOTATE2));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

static int
pkg_repo_binary_register_conflicts(const char *origin, char **conflicts,
		int conflicts_num, sqlite3 *sqlite)
{
	const char clean_conflicts_sql[] = ""
			"DELETE FROM pkg_conflicts "
			"WHERE package_id = ?1;";
	const char select_id_sql[] = ""
			"SELECT id FROM packages "
			"WHERE origin = ?1;";
	const char insert_conflict_sql[] = ""
			"INSERT INTO pkg_conflicts "
			"(package_id, conflict_id) "
			"VALUES (?1, ?2);";
	sqlite3_stmt *stmt = NULL;
	int ret, i;
	int64_t origin_id, conflict_id;

	pkg_debug(4, "pkgdb_repo_register_conflicts: running '%s'", select_id_sql);
	if (sqlite3_prepare_v2(sqlite, select_id_sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite, select_id_sql);
		return (EPKG_FATAL);
	}

	sqlite3_bind_text(stmt, 1, origin, -1, SQLITE_TRANSIENT);
	ret = sqlite3_step(stmt);

	if (ret == SQLITE_ROW) {
		origin_id = sqlite3_column_int64(stmt, 0);
	}
	else {
		ERROR_SQLITE(sqlite, select_id_sql);
		return (EPKG_FATAL);
	}
	sqlite3_finalize(stmt);

	pkg_debug(4, "pkgdb_repo_register_conflicts: running '%s'", clean_conflicts_sql);
	if (sqlite3_prepare_v2(sqlite, clean_conflicts_sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite, clean_conflicts_sql);
		return (EPKG_FATAL);
	}

	sqlite3_bind_int64(stmt, 1, origin_id);
	/* Ignore cleanup result */
	(void)sqlite3_step(stmt);

	sqlite3_finalize(stmt);

	for (i = 0; i < conflicts_num; i ++) {
		/* Select a conflict */
		pkg_debug(4, "pkgdb_repo_register_conflicts: running '%s'", select_id_sql);
		if (sqlite3_prepare_v2(sqlite, select_id_sql, -1, &stmt, NULL) != SQLITE_OK) {
			ERROR_SQLITE(sqlite, select_id_sql);
			return (EPKG_FATAL);
		}

		sqlite3_bind_text(stmt, 1, conflicts[i], -1, SQLITE_TRANSIENT);
		ret = sqlite3_step(stmt);

		if (ret == SQLITE_ROW) {
			conflict_id = sqlite3_column_int64(stmt, 0);
		}
		else {
			ERROR_SQLITE(sqlite, select_id_sql);
			return (EPKG_FATAL);
		}

		sqlite3_finalize(stmt);

		/* Insert a pair */
		pkg_debug(4, "pkgdb_repo_register_conflicts: running '%s'", insert_conflict_sql);
		if (sqlite3_prepare_v2(sqlite, insert_conflict_sql, -1, &stmt, NULL) != SQLITE_OK) {
			ERROR_SQLITE(sqlite, insert_conflict_sql);
			return (EPKG_FATAL);
		}

		sqlite3_bind_int64(stmt, 1, origin_id);
		sqlite3_bind_int64(stmt, 2, conflict_id);
		ret = sqlite3_step(stmt);

		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, insert_conflict_sql);
			return (EPKG_FATAL);
		}

		sqlite3_finalize(stmt);
	}

	return (EPKG_OK);
}

static int
pkg_repo_binary_add_from_manifest(char *buf, sqlite3 *sqlite, size_t len,
		struct pkg_manifest_key **keys, struct pkg **p __unused, bool is_legacy,
		struct pkg_repo *repo)
{
	int rc = EPKG_OK;
	struct pkg *pkg;

	rc = pkg_new(&pkg, PKG_REMOTE);
	if (rc != EPKG_OK)
		return (EPKG_FATAL);

	pkg_manifest_keys_new(keys);
	rc = pkg_parse_manifest(pkg, buf, len, *keys);
	if (rc != EPKG_OK) {
		goto cleanup;
	}

	if (pkg->digest == NULL || !pkg_checksum_is_valid(pkg->digest, strlen(pkg->digest)))
		pkg_checksum_calculate(pkg, NULL);
	if (pkg->arch == NULL || !is_valid_abi(pkg->arch, true)) {
		rc = EPKG_FATAL;
		pkg_emit_error("repository %s contains packages with wrong ABI: %s",
			repo->name, pkg->arch);
		goto cleanup;
	}

	free(pkg->reponame);
	pkg->reponame = strdup(repo->name);

	rc = pkg_repo_binary_add_pkg(pkg, NULL, sqlite, true);

cleanup:
	pkg_free(pkg);

	return (rc);
}

static void __unused
pkg_repo_binary_parse_conflicts(FILE *f, sqlite3 *sqlite)
{
	size_t linecap = 0;
	ssize_t linelen;
	char *linebuf = NULL, *p, **deps;
	const char *origin, *pdep;
	int ndep, i;
	const char conflicts_clean_sql[] = ""
			"DELETE FROM pkg_conflicts;";

	pkg_debug(4, "pkg_parse_conflicts_file: running '%s'", conflicts_clean_sql);
	(void)sql_exec(sqlite, conflicts_clean_sql);

	while ((linelen = getline(&linebuf, &linecap, f)) > 0) {
		p = linebuf;
		origin = strsep(&p, ":");
		/* Check dependencies number */
		pdep = p;
		ndep = 1;
		while (*pdep != '\0') {
			if (*pdep == ',')
				ndep ++;
			pdep ++;
		}
		deps = malloc(sizeof(char *) * ndep);
		for (i = 0; i < ndep; i ++) {
			deps[i] = strsep(&p, ",\n");
		}
		pkg_repo_binary_register_conflicts(origin, deps, ndep, sqlite);
		free(deps);
	}

	free(linebuf);
}

static int
pkg_repo_binary_update_proceed(const char *name, struct pkg_repo *repo,
	time_t *mtime, bool force)
{
	struct pkg *pkg = NULL;
	unsigned char *walk;
	int rc = EPKG_FATAL;
	sqlite3 *sqlite = NULL;
	int cnt = 0;
	time_t local_t;
	time_t packagesite_t;
	struct pkg_manifest_key *keys = NULL;
	unsigned char *map = MAP_FAILED;
	size_t len = 0;
	bool in_trans = false, legacy_repo = false;

	pkg_debug(1, "Pkgrepo, begin update of '%s'", name);

	/* In forced mode, ignore mtime */
	if (force)
		*mtime = 0;

	/* Fetch meta */
	local_t = *mtime;
	if (pkg_repo_fetch_meta(repo, &local_t) == EPKG_FATAL)
		pkg_emit_notice("repository %s has no meta file, using "
		    "default settings", repo->name);

	/* Fetch packagesite */
	local_t = *mtime;
	map = pkg_repo_fetch_remote_extract_mmap(repo,
		repo->meta->manifests, &local_t, &rc, &len);
	if (map == NULL || map == MAP_FAILED)
		goto cleanup;

	packagesite_t = local_t;
	*mtime = local_t;
	/*fconflicts = repo_fetch_remote_extract_tmp(repo,
			repo_conflicts_archive, "txz", &local_t,
			&rc, repo_conflicts_file);*/

	/* Load local repository data */
	rc = pkg_repo_binary_init_update(repo, name);
	if (rc != EPKG_OK) {
		rc = EPKG_FATAL;
		goto cleanup;
	}

	/* Here sqlite is initialized */
	sqlite = PRIV_GET(repo);

	pkg_debug(1, "Pkgrepo, reading new packagesite.yaml for '%s'", name);

	pkg_emit_progress_start("Processing entries");

	sql_exec(sqlite, "PRAGMA page_size = %d;", getpagesize());
	sql_exec(sqlite, "PRAGMA cache_size = 10000;");
	sql_exec(sqlite, "PRAGMA foreign_keys = OFF;");

	rc = pkgdb_transaction_begin_sqlite(sqlite, "REPO");
	if (rc != EPKG_OK)
		goto cleanup;

	in_trans = true;

	walk = map;
	unsigned char *next;

	while (walk -map < len) {
		cnt++;
		next = strchr(walk, '\n');
		if ((cnt % 10 ) == 0)
			pkg_emit_progress_tick(next - map, len);
		rc = pkg_repo_binary_add_from_manifest(walk, sqlite, next - walk,
		    &keys, &pkg, legacy_repo, repo);
		if (rc != EPKG_OK) {
			pkg_emit_progress_tick(len, len);
			break;
		}
		walk = next + 1;
	}
	pkg_emit_progress_tick(len, len);

	if (rc == EPKG_OK)
		pkg_emit_incremental_update(repo->name, cnt);

	sql_exec(sqlite, ""
	 "INSERT INTO pkg_search SELECT id, name || '-' || version, origin FROM packages;"
	"CREATE INDEX packages_origin ON packages(origin COLLATE NOCASE);"
	"CREATE INDEX packages_name ON packages(name COLLATE NOCASE);"
	"CREATE INDEX packages_uid_nocase ON packages(name COLLATE NOCASE, origin COLLATE NOCASE);"
	"CREATE INDEX packages_version_nocase ON packages(name COLLATE NOCASE, version);"
	"CREATE INDEX packages_uid ON packages(name, origin);"
	"CREATE INDEX packages_version ON packages(name, version);"
	"CREATE UNIQUE INDEX packages_digest ON packages(manifestdigest);"
	 );

cleanup:

	if (in_trans) {
		if (rc != EPKG_OK)
			pkgdb_transaction_rollback_sqlite(sqlite, "REPO");

		if (pkgdb_transaction_commit_sqlite(sqlite, "REPO") != EPKG_OK)
			rc = EPKG_FATAL;
	}

	pkg_free(pkg);
	if (map != NULL && map != MAP_FAILED)
		munmap(map, len);

	return (rc);
}

int
pkg_repo_binary_update(struct pkg_repo *repo, bool force)
{
	char filepath[MAXPATHLEN];
	const char update_finish_sql[] = ""
		"DROP TABLE repo_update;";
	sqlite3 *sqlite;

	const char *dbdir = NULL;
	struct stat st;
	time_t t = 0;
	int res = EPKG_FATAL;

	bool got_meta = false;

	sqlite3_initialize();

	if (!pkg_repo_enabled(repo))
		return (EPKG_OK);

	dbdir = pkg_object_string(pkg_config_get("PKG_DBDIR"));
	pkg_debug(1, "PkgRepo: verifying update for %s", pkg_repo_name(repo));

	/* First of all, try to open and init repo and check whether it is fine */
	if (repo->ops->open(repo, R_OK|W_OK) != EPKG_OK) {
		pkg_debug(1, "PkgRepo: need forced update of %s", pkg_repo_name(repo));
		t = 0;
		force = true;
		snprintf(filepath, sizeof(filepath), "%s/%s", dbdir,
		    pkg_repo_binary_get_filename(pkg_repo_name(repo)));
	}
	else {
		repo->ops->close(repo, false);
		snprintf(filepath, sizeof(filepath), "%s/%s.meta", dbdir, pkg_repo_name(repo));
		if (stat(filepath, &st) != -1) {
			t = force ? 0 : st.st_mtime;
			got_meta = true;
		}

		snprintf(filepath, sizeof(filepath), "%s/%s", dbdir,
			pkg_repo_binary_get_filename(pkg_repo_name(repo)));
		if (stat(filepath, &st) != -1) {
			if (!got_meta && !force)
				t = st.st_mtime;
		}
	}

	res = pkg_repo_binary_update_proceed(filepath, repo, &t, force);
	if (res != EPKG_OK && res != EPKG_UPTODATE) {
		pkg_emit_notice("Unable to update repository %s", repo->name);
		goto cleanup;
	}

	/* Finish updated repo */
	if (res == EPKG_OK) {
		sqlite = PRIV_GET(repo);
		sql_exec(sqlite, update_finish_sql);
	}

cleanup:
	/* Set mtime from http request if possible */
	if (t != 0 && res == EPKG_OK) {
		struct timeval ftimes[2] = {
			{
			.tv_sec = t,
			.tv_usec = 0
			},
			{
			.tv_sec = t,
			.tv_usec = 0
			}
		};

		utimes(filepath, ftimes);
		if (got_meta) {
			snprintf(filepath, sizeof(filepath), "%s/%s.meta", dbdir, pkg_repo_name(repo));
			utimes(filepath, ftimes);
		}
	}

	if (repo->priv != NULL)
		repo->ops->close(repo, false);

	return (res);
}
