/* Copyright (c) 2014, Vsevolod Stakhov
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

#include <sys/param.h>
#include <sys/mount.h>

#include <assert.h>
#include <errno.h>
#include <regex.h>
#include <grp.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include <sqlite3.h>

#include <bsd_compat.h>

#ifdef HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/utils.h"
#include "binary.h"
#include "binary_private.h"

static void
sqlite_file_exists(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
	char	 fpath[MAXPATHLEN];
	sqlite3	*db = sqlite3_context_db_handle(ctx);
	char	*path = bsd_dirname(sqlite3_db_filename(db, "main"));
	char	 cksum[SHA256_DIGEST_LENGTH * 2 +1];

	if (argc != 2) {
		sqlite3_result_error(ctx, "file_exists needs two argument", -1);
		return;
	}

	snprintf(fpath, sizeof(fpath), "%s/%s", path, sqlite3_value_text(argv[0]));

	if (access(fpath, R_OK) == 0) {
		sha256_file(fpath, cksum);
		if (strcmp(cksum, sqlite3_value_text(argv[1])) == 0)
			sqlite3_result_int(ctx, 1);
		else
			sqlite3_result_int(ctx, 0);
	} else {
		sqlite3_result_int(ctx, 0);
	}
}

static int
pkg_repo_binary_get_user_version(sqlite3 *sqlite, int *reposcver)
{
	sqlite3_stmt *stmt;
	int retcode;
	const char *sql = "PRAGMA user_version;";

	if (sqlite3_prepare_v2(sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite, sql);
		return (EPKG_FATAL);
	}

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		*reposcver = sqlite3_column_int(stmt, 0);
		retcode = EPKG_OK;
	} else {
		*reposcver = -1;
		retcode = EPKG_FATAL;
	}
	sqlite3_finalize(stmt);
	return (retcode);
}

static int
pkg_repo_binary_set_version(sqlite3 *sqlite, int reposcver)
{
	const char	*sql = "PRAGMA user_version = %d;";

	if (sql_exec(sqlite, sql, reposcver) != EPKG_OK) {
		ERROR_SQLITE(sqlite, sql);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

static int
pkg_repo_binary_apply_change(struct pkg_repo *repo, sqlite3 *sqlite,
		  const struct repo_changes *repo_changes, const char *updown,
		  int version, int *next_version)
{
	const struct repo_changes	*change;
	bool			 found = false, in_trans = false;
	int			 ret = EPKG_OK;
	char			*errmsg;

	for (change = repo_changes; change->version != -1; change++) {
		if (change->version == version) {
			found = true;
			break;
		}
	}
	if (!found) {
		pkg_emit_error("Failed to %s \"%s\" repo schema "
			" version %d (target version %d) "
			"-- change not found", updown, repo->name, version,
			REPO_SCHEMA_VERSION);
		return (EPKG_FATAL);
	}

	/* begin transaction */
	if ((ret = pkgdb_transaction_begin_sqlite(sqlite, "SCHEMA")) == EPKG_OK)
		in_trans = true;

	/* apply change */
	if (ret == EPKG_OK) {
		pkg_debug(4, "Pkgdb: running '%s'", change->sql);
		ret = sqlite3_exec(sqlite, change->sql, NULL, NULL, &errmsg);
		if (ret != SQLITE_OK) {
			pkg_emit_error("sqlite: %s", errmsg);
			sqlite3_free(errmsg);
			ret = EPKG_FATAL;
		}
	}

	/* update repo user_version */
	if (ret == EPKG_OK) {
		*next_version = change->next_version;
		ret = pkg_repo_binary_set_version(sqlite, *next_version);
	}

	/* commit or rollback */
	if (in_trans) {
		if (ret != EPKG_OK)
			pkgdb_transaction_rollback_sqlite(sqlite, "SCHEMA");

		if (pkgdb_transaction_commit_sqlite(sqlite, "SCHEMA") != EPKG_OK)
			ret = EPKG_FATAL;
	}

	if (ret == EPKG_OK) {
		pkg_emit_notice("Repo \"%s\" %s schema %d to %d: %s",
				repo->name, updown, version,
				change->next_version, change->message);
	}

	return (ret);
}

static int
pkg_repo_binary_upgrade(struct pkg_repo *repo, sqlite3 *sqlite, int current_version)
{
	int version;
	int next_version;
	int ret = EPKG_OK;

	for (version = current_version;
	     version < REPO_SCHEMA_VERSION;
	     version = next_version)  {
		ret = pkg_repo_binary_apply_change(repo, sqlite, repo_upgrades,
					"upgrade", version, &next_version);
		if (ret != EPKG_OK)
			break;
		pkg_debug(1, "Upgrading repo database schema from %d to %d",
				version, next_version);
	}
	return (ret);
}

static int
pkg_repo_binary_downgrade(struct pkg_repo *repo, sqlite3 *sqlite, int current_version)
{
	int version;
	int next_version;
	int ret = EPKG_OK;

	for (version = current_version;
	     version > REPO_SCHEMA_VERSION;
	     version = next_version)  {

		ret = pkg_repo_binary_apply_change(repo, sqlite, repo_downgrades,
					"downgrade", version, &next_version);
		if (ret != EPKG_OK)
			break;
		pkg_debug(1, "Downgrading repo database schema from %d to %d",
				version, next_version);
	}
	return (ret);
}

int
pkg_repo_binary_check_version(struct pkg_repo *repo, sqlite3 *sqlite)
{
	int reposcver;
	int repomajor;
	int ret;

	if ((ret = pkg_repo_binary_get_user_version(sqlite, &reposcver))
	    != EPKG_OK)
		return (ret);	/* sqlite error */

	/*
	 * If the local pkgng uses a repo schema behind that used to
	 * create the repo, we may still be able use it for reading
	 * (ie pkg install), but pkg repo can't do an incremental
	 * update unless the actual schema matches the compiled in
	 * schema version.
	 *
	 * Use a major - minor version schema: as the user_version
	 * PRAGMA takes an integer version, encode this as MAJOR *
	 * 1000 + MINOR.
	 *
	 * So long as the major versions are the same, the local pkgng
	 * should be compatible with any repo created by a more recent
	 * pkgng, although it may need some modification of the repo
	 * schema
	 */

	/* --- Temporary ---- Grandfather in the old repo schema
	   version so this patch doesn't immediately invalidate all
	   the repos out there */

	if (reposcver == 2)
		reposcver = 2000;
	if (reposcver == 3)
		reposcver = 2001;

	repomajor = reposcver / 1000;

	if (repomajor < REPO_SCHEMA_MAJOR) {
		pkg_emit_error("Repo %s (schema version %d) is too old - "
		    "need at least schema %d", repo->name, reposcver,
		    REPO_SCHEMA_MAJOR * 1000);
		return (EPKG_REPOSCHEMA);
	}

	if (repomajor > REPO_SCHEMA_MAJOR) {
		pkg_emit_error("Repo %s (schema version %d) is too new - "
		    "we can accept at most schema %d", repo->name, reposcver,
		    ((REPO_SCHEMA_MAJOR + 1) * 1000) - 1);
		return (EPKG_REPOSCHEMA);
	}

	/* This is a repo schema version we can work with */

	ret = EPKG_OK;

	if (reposcver < REPO_SCHEMA_VERSION) {
		if (sqlite3_db_readonly(sqlite, "main")) {
			pkg_emit_error("Repo %s needs schema upgrade from "
			    "%d to %d but it is opened readonly", repo->name,
			    reposcver, REPO_SCHEMA_VERSION);
			ret = EPKG_FATAL;
		} else
			ret = pkg_repo_binary_upgrade(repo, sqlite, reposcver);
	} else if (reposcver > REPO_SCHEMA_VERSION) {
		if (sqlite3_db_readonly(sqlite, "main")) {
			pkg_emit_error("Repo %s needs schema downgrade from "
			"%d to %d but it is opened readonly", repo->name,
			       reposcver, REPO_SCHEMA_VERSION
			);
			ret = EPKG_FATAL;
		} else
			ret = pkg_repo_binary_downgrade(repo, sqlite, reposcver);
	}

	return (ret);
}

int
pkg_repo_binary_open(struct pkg_repo *repo, unsigned mode)
{
	char filepath[MAXPATHLEN];
	const char *dbdir = NULL;
	sqlite3 *sqlite = NULL;
	int flags;
	int64_t res;
	struct pkg_repo_it *it;
	struct pkg *pkg = NULL;

	sqlite3_initialize();
	dbdir = pkg_object_string(pkg_config_get("PKG_DBDIR"));

#ifdef MNT_LOCAL
	struct statfs stfs;
	/*
	 * Fall back on unix-dotfile locking strategy if on a network filesystem
	 */
	if (statfs(dbdir, &stfs) == 0) {
		if ((stfs.f_flags & MNT_LOCAL) != MNT_LOCAL)
			sqlite3_vfs_register(sqlite3_vfs_find("unix-dotfile"), 1);
	}
#endif

	snprintf(filepath, sizeof(filepath), "%s/%s.meta",
		dbdir, pkg_repo_name(repo));

	/* Open metafile */
	if (access(filepath, R_OK) != -1) {
		if (pkg_repo_meta_load(filepath, &repo->meta) != EPKG_OK)
			return (EPKG_FATAL);
	}

	snprintf(filepath, sizeof(filepath), "%s/%s",
		dbdir, pkg_repo_binary_get_filename(pkg_repo_name(repo)));

	/* Always want read mode here */
	if (access(filepath, R_OK | mode) != 0)
		return (EPKG_ENOACCESS);

	flags = (mode & W_OK) != 0 ? SQLITE_OPEN_READWRITE : SQLITE_OPEN_READONLY;
	if (sqlite3_open_v2(filepath, &sqlite, flags, NULL) != SQLITE_OK)
		return (EPKG_FATAL);

	/* Sanitise sqlite database */
	if (get_pragma(sqlite, "SELECT count(name) FROM sqlite_master "
		"WHERE type='table' AND name='repodata';", &res, false) != EPKG_OK) {
		pkg_emit_error("Unable to query repository");
		sqlite3_close(sqlite);
		return (EPKG_FATAL);
	}

	if (res != 1) {
		pkg_emit_notice("Repository %s contains no repodata table, "
			"need to re-create database", repo->name);
		sqlite3_close(sqlite);
		return (EPKG_FATAL);
	}

	/* Check package site */
	char *req = sqlite3_mprintf("select count(key) from repodata "
		"WHERE key = \"packagesite\" and value = '%q'", pkg_repo_url(repo));

	res = 0;
	get_pragma(sqlite, req, &res, true);
	sqlite3_free(req);
	if (res != 1) {
		pkg_emit_notice("Repository %s has a wrong packagesite, need to "
			"re-create database", repo->name);
		sqlite3_close(sqlite);
		return (EPKG_FATAL);
	}

	/* Check version */
	if (pkg_repo_binary_check_version(repo, sqlite) != EPKG_OK) {
		pkg_emit_error("need to re-create repo %s to upgrade schema version",
			repo->name);
		sqlite3_close(sqlite);
		if (mode & W_OK)
			unlink(filepath);
		return (EPKG_REPOSCHEMA);
	}

	repo->priv = sqlite;
	/* Check digests format */
	if ((it = pkg_repo_binary_query(repo, NULL, MATCH_ALL)) == NULL)
		return (EPKG_OK);

	if (it->ops->next(it, &pkg, PKG_LOAD_BASIC) != EPKG_OK) {
		it->ops->free(it);
		return (EPKG_OK);
	}
	it->ops->free(it);
	if (pkg->digest == NULL || !pkg_checksum_is_valid(pkg->digest, strlen(pkg->digest))) {
		pkg_emit_notice("Repository %s has incompatible checksum format, need to "
			"re-create database", repo->name);
		pkg_free(pkg);
		sqlite3_close(sqlite);
		repo->priv = NULL;
		return (EPKG_FATAL);
	}
	pkg_free(pkg);

	return (EPKG_OK);
}

int
pkg_repo_binary_create(struct pkg_repo *repo)
{
	char filepath[MAXPATHLEN];
	const char *dbdir = NULL;
	sqlite3 *sqlite = NULL;
	int retcode;

	sqlite3_initialize();
	dbdir = pkg_object_string(pkg_config_get("PKG_DBDIR"));

	snprintf(filepath, sizeof(filepath), "%s/%s",
		dbdir, pkg_repo_binary_get_filename(pkg_repo_name(repo)));
	/* Should never ever happen */
	if (access(filepath, R_OK) == 0)
		return (EPKG_CONFLICT);

#ifdef MNT_LOCAL
	struct statfs stfs;
	/*
	 * Fall back on unix-dotfile locking strategy if on a network filesystem
	 */
	if (statfs(dbdir, &stfs) == 0) {
		if ((stfs.f_flags & MNT_LOCAL) != MNT_LOCAL)
			sqlite3_vfs_register(sqlite3_vfs_find("unix-dotfile"), 1);
	}
#endif

	/* Open for read/write/create */
	if (sqlite3_open(filepath, &sqlite) != SQLITE_OK)
		return (EPKG_FATAL);

	retcode = sql_exec(sqlite, binary_repo_initsql, REPO_SCHEMA_VERSION);

	if (retcode == EPKG_OK) {
		sqlite3_stmt *stmt;
		const char sql[] = ""
			"INSERT OR REPLACE INTO repodata (key, value) "
			"VALUES (\"packagesite\", ?1);";

		/* register the packagesite */
		if (sql_exec(sqlite, "CREATE TABLE IF NOT EXISTS repodata ("
			"   key TEXT UNIQUE NOT NULL,"
			"   value TEXT NOT NULL"
			");") != EPKG_OK) {
			pkg_emit_error("Unable to register the packagesite in the "
				"database");
			retcode = EPKG_FATAL;
			goto cleanup;
		}

		if (sqlite3_prepare_v2(sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
			ERROR_SQLITE(sqlite, sql);
			retcode = EPKG_FATAL;
			goto cleanup;
		}

		sqlite3_bind_text(stmt, 1, pkg_repo_url(repo), -1, SQLITE_STATIC);

		if (sqlite3_step(stmt) != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, sql);
			sqlite3_finalize(stmt);
			retcode = EPKG_FATAL;
			goto cleanup;
		}

		sqlite3_finalize(stmt);
	}

cleanup:
	sqlite3_close(sqlite);

	return (retcode);
}

int
pkg_repo_binary_init(struct pkg_repo *repo)
{
	int retcode = EPKG_OK;
	sqlite3 *sqlite = PRIV_GET(repo);

	sqlite3_create_function(sqlite, "file_exists", 2, SQLITE_ANY, NULL,
		    sqlite_file_exists, NULL, NULL);
	retcode = sql_exec(sqlite, "PRAGMA synchronous=default");
	if (retcode != EPKG_OK)
		return (retcode);

	retcode = sql_exec(sqlite, "PRAGMA foreign_keys=on");
	if (retcode != EPKG_OK)
		return (retcode);

	pkgdb_sqlcmd_init(sqlite, NULL, NULL);

	retcode = pkg_repo_binary_init_prstatements(sqlite);
	if (retcode != EPKG_OK)
		return (retcode);

	repo->priv = sqlite;

	return (EPKG_OK);
}

int
pkg_repo_binary_close(struct pkg_repo *repo, bool commit)
{
	int retcode = EPKG_OK;
	sqlite3 *sqlite = PRIV_GET(repo);

	if (sqlite == NULL)
		return (retcode);

	if (commit) {
		if (pkgdb_transaction_commit_sqlite(sqlite, NULL) != SQLITE_OK)
			retcode = EPKG_FATAL;
	}

	pkg_repo_binary_finalize_prstatements();
	sqlite3_free(sqlite);

	repo->priv = NULL;

	return (retcode);
}

int
pkg_repo_binary_access(struct pkg_repo *repo, unsigned mode)
{
	const pkg_object	*o;
	const char		*dbdir;
	int			 ret = EPKG_OK;

	o = pkg_config_get("PKG_DBDIR");
	dbdir = pkg_object_string(o);

	ret = pkgdb_check_access(mode, dbdir,
		pkg_repo_binary_get_filename(pkg_repo_name(repo)));

	return (ret);
}
