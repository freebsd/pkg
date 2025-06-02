/*-
 * Copyright (c) 2011-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2023 Serenity Cyber Security, LLC
 *                    Author: Gleb Popov <arrowd@FreeBSD.org>
 *
 */

#ifndef _PKGDB_H
#define _PKGDB_H

#include "pkg.h"

#include <sqlite3.h>
#include "pkg/vec.h"

typedef vec_t(struct pkg_repo *) repos_t;
struct pkgdb {
	sqlite3		*sqlite;
	bool		 prstmt_initialized;
	repos_t repos;
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
	struct pkgdb *db;
	vec_t(struct pkg_repo_it *) remote;
	size_t remote_pos;
	struct pkgdb_sqlite_it *local;
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
		const char *provide, c_charv_t *repos);
/**
 * Find requires for a specified provide in repos
 * @param db
 * @param require
 * @param repo
 * @return
 */
struct pkgdb_it *pkgdb_repo_shlib_provide(struct pkgdb *db,
		const char *require, c_charv_t *repos);

struct pkgdb_it *pkgdb_repo_provide(struct pkgdb *db, const char *require, c_charv_t *repo);

struct pkgdb_it *pkgdb_repo_require(struct pkgdb *db, const char *provide, c_charv_t *repo);

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
int pkgdb_check_access(unsigned mode, const char *dbname);

/*
 * SQLite utility functions
 */
void pkgdb_regex(sqlite3_context *ctx, int argc, sqlite3_value **argv);
void pkgdb_regex_delete(void *p);
void pkgdb_now(sqlite3_context *ctx, int argc, __unused sqlite3_value **argv);
int pkgdb_sqlcmd_init(sqlite3 *db, const char **err, const void *noused);
int pkgdb_update_config_file_content(struct pkg *pkg, sqlite3 *s);
void pkgdb_syscall_overload(void);
void pkgdb_nfs_corruption(sqlite3 *s);
bool pkgdb_file_exists(struct pkgdb *db, const char *path);
struct sqlite3_stmt *prepare_sql(sqlite3 *s, const char *sql);
void pkgdb_debug(int level, sqlite3_stmt *stmt);

bool pkgdb_is_provided(struct pkgdb *db, const char *req);
bool pkgdb_is_shlib_provided(struct pkgdb *db, const char *req);

#endif
