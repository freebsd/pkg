/*
 * Copyright (c) 2014, Vsevolod Stakhov
 * Copyright (c) 2024, Baptiste Daroussin
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
#include <fcntl.h>
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

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/utils.h"
#include "binary.h"
#include "binary_private.h"

extern struct pkg_ctx ctx;

static int
pkg_repo_binary_get_user_version(sqlite3 *sqlite, int *reposcver)
{
	sqlite3_stmt *stmt;
	int retcode;
	const char *sql = "PRAGMA user_version;";

	if ((stmt = prepare_sql(sqlite, sql)) == NULL)
		return (EPKG_FATAL);

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		*reposcver = sqlite3_column_int64(stmt, 0);
		retcode = EPKG_OK;
	} else {
		*reposcver = -1;
		retcode = EPKG_FATAL;
	}
	sqlite3_finalize(stmt);
	return (retcode);
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

	if (reposcver != REPO_SCHEMA_VERSION)
		return (EPKG_REPOSCHEMA);
	return (EPKG_OK);
}

int
pkg_repo_binary_open(struct pkg_repo *repo, unsigned mode)
{
	const char *filepath;
	sqlite3 *sqlite = NULL;
	int flags, dbdirfd, fd, reposfd, thisrepofd;
	int64_t res;
	struct pkg_repo_it *it;
	struct pkg *pkg = NULL;

	sqlite3_initialize();

	pkgdb_syscall_overload();

	dbdirfd = pkg_get_dbdirfd();
	reposfd = pkg_get_reposdirfd();
	thisrepofd = openat(reposfd, repo->name, O_DIRECTORY|O_CLOEXEC);
	if (thisrepofd == -1) {
		if (mkdirat(reposfd, repo->name, 0755) == -1)
			return( EPKG_FATAL);
		thisrepofd = openat(reposfd, repo->name, O_DIRECTORY|O_CLOEXEC);
	}

	/* Open metafile */
	if ((fd = openat(thisrepofd, "meta", O_RDONLY)) != -1) {
		if (pkg_repo_meta_load(fd, &repo->meta) != EPKG_OK) {
			pkg_emit_error("Repository %s load error: "
			    "meta file cannot be loaded", repo->name);
			close(fd);
			return (EPKG_FATAL);
		}
		close(fd);
	}

	filepath = pkg_repo_binary_get_filename(repo);

	/* Always want read mode here */
	if (faccessat(dbdirfd, filepath, R_OK | mode, 0) != 0) {
		return (EPKG_ENOACCESS);
	}

	flags = (mode & W_OK) != 0 ? SQLITE_OPEN_READWRITE : SQLITE_OPEN_READONLY;
	if (sqlite3_open_v2(filepath, &sqlite, flags, NULL) != SQLITE_OK) {
		pkgdb_nfs_corruption(sqlite);
		pkg_emit_error("Repository %s load error: "
		    "cannot open sqlite3 db: %s",
		    pkg_repo_name(repo), sqlite3_errmsg(sqlite));
		return (EPKG_FATAL);
	}

	/* Sanitise sqlite database */
	if (get_pragma(sqlite, "SELECT count(name) FROM sqlite_master "
		"WHERE type='table' AND name='repodata';", &res, false) != EPKG_OK) {
		pkg_emit_error("Repository %s load error: unable to query db",
		    pkg_repo_name(repo));
		sqlite3_close(sqlite);
		return (EPKG_FATAL);
	}

	if (res != 1) {
		pkg_emit_error("Repository %s contains no repodata table, "
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
		pkg_emit_error("Repository %s has a wrong packagesite, need to "
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
			(void)unlinkat(dbdirfd, filepath, 0);
		return (EPKG_REPOSCHEMA);
	}

	repo->priv = sqlite;
	/* Check digests format */
	if ((it = pkg_repo_binary_query(repo, NULL, NULL, MATCH_ALL)) == NULL)
		return (EPKG_OK);

	if (it->ops->next(it, &pkg, PKG_LOAD_BASIC) != EPKG_OK) {
		it->ops->free(it);
		return (EPKG_OK);
	}
	it->ops->free(it);
	if (pkg->digest == NULL || !pkg_checksum_is_valid(pkg->digest, strlen(pkg->digest))) {
		pkg_emit_error("Repository %s has incompatible checksum format, need to "
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
	const char *filepath;
	sqlite3 *sqlite = NULL;
	int retcode, dbdirfd;

	sqlite3_initialize();

	dbdirfd = pkg_get_dbdirfd();
	filepath = pkg_repo_binary_get_filename(repo);
	/* Should never ever happen */
	if (faccessat(dbdirfd, filepath, R_OK, 0) == 0)
		return (EPKG_CONFLICT);

	pkgdb_syscall_overload();

	/* Open for read/write/create */
	if (sqlite3_open(filepath, &sqlite) != SQLITE_OK) {
		pkgdb_nfs_corruption(sqlite);
		return (EPKG_FATAL);
	}

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
			ERROR_STMT_SQLITE(sqlite, stmt);
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

	retcode = sql_exec(sqlite, "PRAGMA journal_mode=TRUNCATE;");
	if (retcode != EPKG_OK)
		return (retcode);

	retcode = sql_exec(sqlite, "PRAGMA synchronous=FULL");
	if (retcode != EPKG_OK)
		return (retcode);

	retcode = sql_exec(sqlite, "PRAGMA foreign_keys=on");
	if (retcode != EPKG_OK)
		return (retcode);
	sql_exec(sqlite, "PRAGMA mmap_size=268435456;");


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
	sqlite3_close(sqlite);

	repo->priv = NULL;

	return (retcode);
}

int
pkg_repo_binary_access(struct pkg_repo *repo, unsigned mode)
{
	int			 ret = EPKG_OK;

	ret = pkgdb_check_access(mode,
		pkg_repo_binary_get_filename(repo));

	return (ret);
}
