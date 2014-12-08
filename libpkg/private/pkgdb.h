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
	bool		 prstmt_initialized;

	struct _pkg_repo_list_item {
		struct pkg_repo *repo;
		struct _pkg_repo_list_item *next;
	} *repos;
};

enum pkgdb_iterator_type {
	PKGDB_IT_LOCAL = 0,
	PKGDB_IT_REPO
};

struct pkgdb_sqlite_it {
	sqlite3	*sqlite;
	sqlite3_stmt	*stmt;
	short	flags;
	short	finished;
	short	pkg_type;
};

struct pkg_repo_it;

struct pkgdb_it {
	enum pkgdb_iterator_type type;
	struct pkgdb *db;
	union _un_pkg_it {
		struct _pkg_repo_it_set {
			struct pkg_repo_it *it;
			struct _pkg_repo_it_set *next;
		} *remote;
		struct pkgdb_sqlite_it local;
	} un;
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
int pkgdb_transaction_begin_sqlite(sqlite3 *sqlite, const char *savepoint);
int pkgdb_transaction_commit_sqlite(sqlite3 *sqlite, const char *savepoint);
int pkgdb_transaction_rollback_sqlite(sqlite3 *sqlite, const char *savepoint);

struct pkgdb_it *pkgdb_it_new_sqlite(struct pkgdb *db, sqlite3_stmt *s,
	int type, short flags);
struct pkgdb_it *pkgdb_it_new_repo(struct pkgdb *db);
void pkgdb_it_repo_attach(struct pkgdb_it *it, struct pkg_repo_it *rit);

/**
 * Load missing flags for a specific package from pkgdb
 */
int pkgdb_ensure_loaded(struct pkgdb *db, struct pkg *pkg, unsigned flags);
int pkgdb_ensure_loaded_sqlite(sqlite3 *sqlite, struct pkg *pkg, unsigned flags);

void pkgshell_open(const char **r);

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
 * Get query for the specified match type
 * @param pattern
 * @param match
 * @return
 */
const char * pkgdb_get_pattern_query(const char *pattern, match_t match);

/**
 * Find provides for a specified require in repos
 * @param db
 * @param provide
 * @param repo
 * @return
 */
struct pkgdb_it *pkgdb_repo_shlib_require(struct pkgdb *db,
		const char *provide, const char *repo);
/**
 * Find requires for a specified provide in repos
 * @param db
 * @param require
 * @param repo
 * @return
 */
struct pkgdb_it *pkgdb_repo_shlib_provide(struct pkgdb *db,
		const char *require, const char *repo);

/**
 * Unregister a package from the database
 * @return An error code.
 */
int pkgdb_unregister_pkg(struct pkgdb *pkg, int64_t id);

/**
 * Optimize db for using of solver
 */
int pkgdb_begin_solver(struct pkgdb *db);

/**
 * Restore normal db operations
 * @param db
 * @return
 */
int pkgdb_end_solver(struct pkgdb *db);

/**
 * Check access mode for the specified file
 * @param mode
 * @param dbdir
 * @param dbname
 * @return
 */
int pkgdb_check_access(unsigned mode, const char* dbdir, const char *dbname);

/**
 * Returns number of attached repositories
 * @param db
 * @return
 */
int pkgdb_repo_count(struct pkgdb *db);
/*
 * SQLite utility functions
 */
void pkgdb_regex(sqlite3_context *ctx, int argc, sqlite3_value **argv);
void pkgdb_regex_delete(void *p);
void pkgdb_split_uid(sqlite3_context *ctx, int argc, sqlite3_value **argv);
void pkgdb_split_version(sqlite3_context *ctx, int argc, sqlite3_value **argv);
void pkgdb_now(sqlite3_context *ctx, int argc, __unused sqlite3_value **argv);
void pkgdb_myarch(sqlite3_context *ctx, int argc, sqlite3_value **argv);
int pkgdb_sqlcmd_init(sqlite3 *db, const char **err, const void *noused);
int pkgdb_update_config_file_content(struct pkg *pkg, sqlite3 *s);

#endif
