/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
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

#ifndef _PKGDB_H
#define _PKGDB_H

#include "pkg.h"

#include "sqlite3.h"

struct pkgdb {
	sqlite3		*sqlite;
	pkgdb_t		 type;
	int		 lock_count;
	bool		 prstmt_initialized;
};

struct pkgdb_it {
	struct pkgdb	*db;
	sqlite3	*sqlite;
	sqlite3_stmt	*stmt;
	short	type;
	short	flags;
	short	finished;
};

#define PKGDB_IT_FLAG_CYCLED (0x1)
#define PKGDB_IT_FLAG_ONCE (0x1 << 1)
#define PKGDB_IT_FLAG_AUTO (0x1 << 2)


/**
 * Transaction/savepoint handling.
 * @param savepoint -- if NULL or an empty string, use BEGIN, ROLLBACK, COMMIT
 * otherwise use SAVEPOINT, ROLLBACK TO, RELEASE.
 * @return an error code.
 */
int pkgdb_transaction_begin(sqlite3 *sqlite, const char *savepoint);
int pkgdb_transaction_commit(sqlite3 *sqlite, const char *savepoint);
int pkgdb_transaction_rollback(sqlite3 *sqlite, const char *savepoint);

struct pkgdb_it *pkgdb_it_new(struct pkgdb *db, sqlite3_stmt *s, int type, short flags);

void pkgshell_open(const char **r);

/**
 * Open repodb for specified path
 * @param repodb path of repodb
 * @param force create repository if not exists
 * @param sqlite destination db pointer
 * @return EPKG_OK if succeed
 */
int pkgdb_repo_open(const char *repodb, bool force, sqlite3 **sqlite);

/**
 * Init repository for pkgdb_repo* functions
 * @param sqlite sqlite object
 * @return EPKG_OK if succeed
 */
int pkgdb_repo_init(sqlite3 *sqlite);

/**
 * Finalize prepared statements for a repo
 */
void pkgdb_repo_finalize_statements(void);

/**
 * Close repodb and commit/rollback transaction started
 * @param sqlite sqlite pointer
 * @param commit commit transaction if true, rollback otherwise
 * @return EPKG_OK if succeed
 */
int pkgdb_repo_close(sqlite3 *sqlite, bool commit);

/**
 * Check whether a package with the cehcksum specified exists in pkg_repo
 * @param sqlite sqlite pointer
 * @param cksum sha256 printed checksum
 * @return EPKG_OK if checksum exists, EPKG_END if not and EPKG_FATAL if error occurred
 */
int pkgdb_repo_cksum_exists(sqlite3 *sqlite, const char *cksum);

/**
 * Add a package to pkg_repo
 * @param pkg package structure
 * @param pkg_path path triggered package addition
 * @param sqlite sqlite pointer
 * @param manifest_digest sha256 checksum of the manifest of the package
 * @param forced force adding of package even if it is outdated
 * @return EPKG_OK if package added, EPKG_END if package already exists and is newer than
 * inserted one, EPKG_FATAL if error occurred
 */
int pkgdb_repo_add_package(struct pkg *pkg, const char *pkg_path,
		sqlite3 *sqlite, const char *manifest_digest, bool forced);

/**
 * Remove specified pkg from repo
 * @param origin the origin of package to remove
 * @return EPKG_OK if succeeded
 */
int pkgdb_repo_remove_package(const char *origin);

/**
 * Upgrade repo db version if required
 * @param db package database object
 * @param database name of database
 * @return EPKG_OK if succeeded
 */
int pkgdb_repo_check_version(struct pkgdb *db, const char *database);

/**
 * Returns a list of all packages sorted by origin
 * @param sqlite database
 * @return new iterator
 */
struct pkgdb_it *pkgdb_repo_origins(sqlite3 *sqlite);

/**
 * Register a conflicts list in a repo
 * @param origin the origin of a package
 * @param conflicts a list of conflicts origins
 * @param conflicts_num number of conflicts for this package
 * @param sqlite database
 * @return error code
 */
int pkgdb_repo_register_conflicts(const char *origin, char **conflicts,
		int conflicts_num, sqlite3 *sqlite);

/**
 * Execute SQL statement on all attached databases
 * @param s
 * @param sql
 * @param multireposql
 * @param compound
 * @return
 */
int
pkgdb_sql_all_attached(sqlite3 *s, struct sbuf *sql, const char *multireposql,
    const char *compound);

/**
 * Get repository name
 * @param db
 * @param repo
 * @return
 */
const char *pkgdb_get_reponame(struct pkgdb *db, const char *repo);

/**
 * Get query for the specified match type
 * @param pattern
 * @param match
 * @return
 */
const char * pkgdb_get_pattern_query(const char *pattern, match_t match);

/**
 * Returns whether the specified database is attached
 * @param s
 * @param name
 * @return
 */
bool pkgdb_is_attached(sqlite3 *s, const char *name);

/**
 * Find provides for a specified require in repos
 * @param db
 * @param provide
 * @param repo
 * @return
 */
struct pkgdb_it *pkgdb_find_shlib_require(struct pkgdb *db,
		const char *provide, const char *repo);
/**
 * Find requires for a specified provide in repos
 * @param db
 * @param require
 * @param repo
 * @return
 */
struct pkgdb_it *pkgdb_find_shlib_provide(struct pkgdb *db,
		const char *require, const char *repo);

#endif
