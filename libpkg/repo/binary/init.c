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
	char	*path = dirname(sqlite3_db_filename(db, "main"));
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
pkg_repo_binary_get_user_version(sqlite3 *sqlite, const char *database, int *reposcver)
{
	sqlite3_stmt *stmt;
	int retcode;
	char sql[BUFSIZ];
	const char *fmt = "PRAGMA %Q.user_version";

	assert(database != NULL);

	sqlite3_snprintf(sizeof(sql), sql, fmt, database);

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
pkg_repo_binary_init_prstatements(sqlite3 *sqlite)
{
	sql_prstmt_index i, last;
	int ret;

	last = PRSTMT_LAST;

	for (i = 0; i < last; i++) {
		ret = sqlite3_prepare_v2(sqlite, SQL(i), -1, &STMT(i), NULL);
		if (ret != SQLITE_OK) {
			ERROR_SQLITE(sqlite, SQL(i));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

void
pkg_repo_binary_finalize_prstatements(void)
{
	sql_prstmt_index i, last;

	last = PRSTMT_LAST;

	for (i = 0; i < last; i++)
	{
		if (STMT(i) != NULL) {
			sqlite3_finalize(STMT(i));
			STMT(i) = NULL;
		}
	}
	return;
}

int
pkg_repo_binary_open(struct pkg_repo *repo, unsigned mode)
{
	int reposcver;
	int retcode = EPKG_OK;
	char filepath[MAXPATHLEN];
	const char *dbdir = NULL;
	sqlite3 *sqlite = NULL;
	int flags;
	int64_t res;

	sqlite3_initialize();
	dbdir = pkg_object_string(pkg_config_get("PKG_DBDIR"));

	snprintf(filepath, sizeof(filepath), "%s/%s.sqlite", dbdir, pkg_repo_name(repo));

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
	retcode = pkg_repo_binary_get_user_version(sqlite, "main", &reposcver);
	if (retcode != EPKG_OK) {
		sqlite3_close(sqlite);
		return (EPKG_FATAL);
	}
	if (reposcver != REPO_SCHEMA_VERSION) {
		if (reposcver != REPO_SCHEMA_VERSION)
			pkg_emit_error("re-creating repo to upgrade schema version "
				"from %d to %d", reposcver,
				REPO_SCHEMA_VERSION);
		sqlite3_close(sqlite);
		if (mode & W_OK)
			unlink(filepath);
		return (EPKG_REPOSCHEMA);
	}

	repo->priv = sqlite;

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

	snprintf(filepath, sizeof(filepath), "%s/%s.sqlite", dbdir, pkg_repo_name(repo));
	/* Should never ever happen */
	if (access(filepath, R_OK) != 0)
		return (EPKG_CONFLICT);

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
		if (pkgdb_transaction_commit(sqlite, NULL) != SQLITE_OK)
			retcode = EPKG_FATAL;
	}
	else {
		if (pkgdb_transaction_rollback(sqlite, NULL) != SQLITE_OK)
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

	ret = pkgdb_check_access(mode, dbdir, pkg_repo_name(repo));

	return (ret);
}
