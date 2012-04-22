/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
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

#include <assert.h>
#include <errno.h>
#include <regex.h>
#include <grp.h>
#include <libutil.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <sqlite3.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/utils.h"

#include "private/db_upgrades.h"
#define DBVERSION 12

static struct pkgdb_it * pkgdb_it_new(struct pkgdb *, sqlite3_stmt *, int);
static void pkgdb_regex(sqlite3_context *, int, sqlite3_value **, int);
static void pkgdb_regex_basic(sqlite3_context *, int, sqlite3_value **);
static void pkgdb_regex_extended(sqlite3_context *, int, sqlite3_value **);
static void pkgdb_regex_delete(void *);
static void pkgdb_pkglt(sqlite3_context *, int, sqlite3_value **);
static void pkgdb_pkggt(sqlite3_context *, int, sqlite3_value **);
static int get_pragma(sqlite3 *, const char *, int64_t *);
static int pkgdb_upgrade(struct pkgdb *);
static void populate_pkg(sqlite3_stmt *stmt, struct pkg *pkg);
static int create_temporary_pkgjobs(sqlite3 *);
static void pkgdb_detach_remotes(sqlite3 *);
static bool is_attached(sqlite3 *, const char *);
static void report_already_installed(sqlite3 *);
static int sqlcmd_init(sqlite3 *db, __unused const char **err, __unused const void *noused);

extern int sqlite3_shell(int, char**);

static struct column_mapping {
	const char * const name;
	pkg_attr type;
} columns[] = {
	{ "origin", PKG_ORIGIN},
	{ "name", PKG_NAME },
	{ "version", PKG_VERSION },
	{ "comment", PKG_COMMENT },
	{ "desc", PKG_DESC },
	{ "message", PKG_MESSAGE },
	{ "arch", PKG_ARCH },
	{ "maintainer", PKG_MAINTAINER},
	{ "www", PKG_WWW},
	{ "prefix", PKG_PREFIX},
	{ "cksum", PKG_CKSUM},
	{ "repopath", PKG_REPOPATH},
	{ "dbname", PKG_REPONAME},
	{ "newversion", PKG_NEWVERSION},
	{ "flatsize", PKG_FLATSIZE },
	{ "newflatsize", PKG_NEW_FLATSIZE },
	{ "pkgsize", PKG_NEW_PKGSIZE },
	{ "licenselogic", PKG_LICENSE_LOGIC},
	{ "automatic", PKG_AUTOMATIC},
	{ "time", PKG_TIME},
	{ "infos", PKG_INFOS},
	{ "rowid", PKG_ROWID},
	{ "id", PKG_ROWID },
	{ "weight", -1 },
	{ NULL, -1 }
};

static int
load_val(sqlite3 *db, struct pkg *pkg, const char *sql, int flags, int (*pkg_adddata)(struct pkg *pkg, const char *data), int list)
{
	sqlite3_stmt *stmt;
	int ret;

	assert(db != NULL && pkg != NULL);

	if (pkg->flags & flags)
		return (EPKG_OK);

	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db);
		return (EPKG_FATAL);
	}

	sqlite3_bind_int64(stmt, 1, pkg->rowid);

	while (( ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_adddata(pkg, sqlite3_column_text(stmt, 0));
	}

	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		if (list != -1)
			pkg_list_free(pkg, list);
		ERROR_SQLITE(db);
		return (EPKG_FATAL);
	}

	pkg->flags |= flags;
	return (EPKG_OK);
}

static const char *
pkgdb_get_reponame(struct pkgdb *db, const char *repo)
{
	const char *reponame = NULL;
	bool multirepos_enabled = false;

	assert(db->type == PKGDB_REMOTE);

	/* Working on multiple repositories */
	pkg_config_bool(PKG_CONFIG_MULTIREPOS, &multirepos_enabled);

	if (multirepos_enabled) {
		if (repo != NULL) {
			if (!is_attached(db->sqlite, repo)) {
				pkg_emit_error("repository '%s' does not exist.", repo);
				return (NULL);
			}

			reponame = repo;
		} else {
			/* default repository in multi-repos is 'default' */
			reponame = "default";
		}
	} else {
		if (repo != NULL) {
			pkg_emit_error("PKG_MULTIREPOS is not enabled. -r flag not supported.", repo);
			return (NULL);
		}
		/* default repository in single-repo is 'remote' */
		reponame = "remote";
	}

	return (reponame);
}

static void
populate_pkg(sqlite3_stmt *stmt, struct pkg *pkg) {
	int i, icol = 0;
	const char *colname;

	assert(stmt != NULL);

	for (icol = 0; icol < sqlite3_column_count(stmt); icol++) {
		colname = sqlite3_column_name(stmt, icol);
		switch (sqlite3_column_type(stmt, icol)) {
			case SQLITE_TEXT:
				for (i = 0; columns[i].name != NULL; i++ ) {
					if (!strcmp(columns[i].name, colname)) {
						pkg_set(pkg, columns[i].type, sqlite3_column_text(stmt, icol));
						break;
					}
				}
				if (columns[i].name == NULL)
					pkg_emit_error("Unknown column %s", colname);
				break;
			case SQLITE_INTEGER:
				for (i = 0; columns[i].name != NULL; i++ ) {
					if (!strcmp(columns[i].name, colname)) {
						pkg_set(pkg, columns[i].type, sqlite3_column_int64(stmt, icol));
						break;
					}
				}
				if (columns[i].name == NULL)
					pkg_emit_error("Unknown column %s", colname);
				break;
			case SQLITE_BLOB:
			case SQLITE_FLOAT:
				pkg_emit_error("Wrong type for column: %s", colname);
				/* just ignore currently */
				break;
			case SQLITE_NULL:
				break;
		}
	}
}

static void
pkgdb_regex(sqlite3_context *ctx, int argc, sqlite3_value **argv, int reg_type)
{
	const unsigned char *regex = NULL;
	const unsigned char *str;
	regex_t *re;
	int ret;

	if (argc != 2 || (regex = sqlite3_value_text(argv[0])) == NULL ||
		(str = sqlite3_value_text(argv[1])) == NULL) {
		sqlite3_result_error(ctx, "SQL function regex() called with invalid arguments.\n", -1);
		return;
	}

	re = (regex_t *)sqlite3_get_auxdata(ctx, 0);
	if (re == NULL) {
		re = malloc(sizeof(regex_t));
		if (regcomp(re, regex, reg_type | REG_NOSUB) != 0) {
			sqlite3_result_error(ctx, "Invalid regex\n", -1);
			free(re);
			return;
		}

		sqlite3_set_auxdata(ctx, 0, re, pkgdb_regex_delete);
	}

	ret = regexec(re, str, 0, NULL, 0);
	sqlite3_result_int(ctx, (ret != REG_NOMATCH));
}

static void
pkgdb_regex_basic(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
	pkgdb_regex(ctx, argc, argv, REG_BASIC);
}

static void
pkgdb_regex_extended(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
	pkgdb_regex(ctx, argc, argv, REG_EXTENDED);
}

static void
pkgdb_regex_delete(void *p)
{
	regex_t *re = (regex_t *)p;

	regfree(re);
	free(re);
}

static void
pkgdb_now(sqlite3_context *ctx, int argc, __unused sqlite3_value **argv)
{
	if (argc != 0) {
		sqlite3_result_error(ctx, "Invalid usage of now() no arguments expected\n", -1);
		return;
	}

	sqlite3_result_int64(ctx, (int64_t)time(NULL));
}

static void
pkgdb_myarch(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
	const unsigned char *arch = NULL;
	char myarch[BUFSIZ];

	if (argc > 1) {
		sqlite3_result_error(ctx, "Invalid usage of myarch\n", -1);
		return;
	}

	if (argc == 0 || (arch = sqlite3_value_text(argv[0])) == NULL) {
		pkg_get_myarch(myarch, BUFSIZ);
		sqlite3_result_text(ctx, myarch, strlen(myarch), NULL);
		return;
	}
	sqlite3_result_text(ctx, arch, strlen(arch), NULL);
}

static void
pkgdb_pkgcmp(sqlite3_context *ctx, int argc, sqlite3_value **argv, int sign)
{
	const unsigned char *version1 = NULL;
	const unsigned char *version2 = NULL;
	if (argc != 2 || (version1 = sqlite3_value_text(argv[0])) == NULL
			|| (version2 = sqlite3_value_text(argv[1])) == NULL) {
		sqlite3_result_error(ctx, "Invalid comparison\n", -1);
		return;
	}

	sqlite3_result_int(ctx, (pkg_version_cmp(version1, version2) == sign));
}

static void
pkgdb_pkglt(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
	pkgdb_pkgcmp(ctx, argc, argv, -1);
}

static void
pkgdb_pkggt(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
	pkgdb_pkgcmp(ctx, argc, argv, 1);
}

static int
pkgdb_upgrade(struct pkgdb *db)
{
	int64_t db_version = -1;
	const char *sql_upgrade;
	int i;

	assert(db != NULL);

	if (get_pragma(db->sqlite, "PRAGMA user_version;", &db_version) != EPKG_OK)
		return (EPKG_FATAL);

	if (db_version == DBVERSION)
		return (EPKG_OK);
	else if (db_version > DBVERSION) {
		pkg_emit_error("database version is newer than libpkg(3)");
		return (EPKG_FATAL);
	}

	while (db_version < DBVERSION) {
		if (sqlite3_db_readonly(db->sqlite, "main")) {
			pkg_emit_error("The database is outdated and opened readonly");
			return (EPKG_FATAL);
		}
		db_version++;

		i = 0;
		sql_upgrade = NULL;
		while (db_upgrades[i].version != -1) {
			if (db_upgrades[i].version == db_version) {
				sql_upgrade = db_upgrades[i].sql;
				break;
			}
			i++;
		}

		/*
		 * We can't find the statements to upgrade to the next version,
		 * maybe because the current version is too old and upgrade support has
		 * been removed.
		 */
		if (sql_upgrade == NULL) {
			pkg_emit_error("can not upgrade to db version %" PRId64,
						   db_version);
			return (EPKG_FATAL);
		}

		if (sql_exec(db->sqlite, "BEGIN;") != EPKG_OK)
			return (EPKG_FATAL);

		if (sql_exec(db->sqlite, sql_upgrade) != EPKG_OK)
			return (EPKG_FATAL);

		if (sql_exec(db->sqlite, "PRAGMA user_version = %" PRId64 ";", db_version) != EPKG_OK)
			return (EPKG_FATAL);

		if (sql_exec(db->sqlite, "COMMIT;") != EPKG_OK)
			return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

/*
 * in the database :
 * scripts.type can be:
 * - 0: PRE_INSTALL
 * - 1: POST_INSTALL
 * - 2: PRE_DEINSTALL
 * - 3: POST_DEINSTALL
 * - 4: PRE_UPGRADE
 * - 5: POST_UPGRADE
 * - 6: INSTALL
 * - 7: DEINSTALL
 * - 8: UPGRADE
 */

static int
pkgdb_init(sqlite3 *sdb)
{
	const char sql[] = ""
	"BEGIN;"
	"CREATE TABLE packages ("
		"id INTEGER PRIMARY KEY,"
		"origin TEXT UNIQUE NOT NULL,"
		"name TEXT NOT NULL,"
		"version TEXT NOT NULL,"
		"comment TEXT NOT NULL,"
		"desc TEXT NOT NULL,"
		"mtree_id INTEGER REFERENCES mtree(id) ON DELETE RESTRICT"
			" ON UPDATE CASCADE,"
		"message TEXT,"
		"arch TEXT NOT NULL,"
		"maintainer TEXT NOT NULL, "
		"www TEXT,"
		"prefix TEXT NOT NULL,"
		"flatsize INTEGER NOT NULL,"
		"automatic INTEGER NOT NULL,"
		"licenselogic INTEGER NOT NULL,"
		"infos TEXT, "
		"time INTEGER, "
		"pkg_format_version INTEGER"
	");"
	"CREATE TABLE mtree ("
		"id INTEGER PRIMARY KEY,"
		"content TEXT UNIQUE"
	");"
	"CREATE TABLE scripts ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"script TEXT,"
		"type INTEGER,"
		"PRIMARY KEY (package_id, type)"
	");"
	"CREATE TABLE options ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"option TEXT,"
		"value TEXT,"
		"PRIMARY KEY (package_id,option)"
	");"
	"CREATE TABLE deps ("
		"origin TEXT NOT NULL,"
		"name TEXT NOT NULL,"
		"version TEXT NOT NULL,"
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"PRIMARY KEY (package_id,origin)"
	");"
	"CREATE TABLE files ("
		"path TEXT PRIMARY KEY,"
		"sha256 TEXT,"
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE"
	");"
	"CREATE TABLE directories ("
		"id INTEGER PRIMARY KEY,"
		"path TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE pkg_directories ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"directory_id INTEGER REFERENCES directories(id) ON DELETE RESTRICT"
			" ON UPDATE RESTRICT,"
		"try INTEGER,"
		"PRIMARY KEY (package_id, directory_id)"
	");"
	"CREATE TABLE categories ("
		"id INTEGER PRIMARY KEY,"
		"name TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE pkg_categories ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"category_id INTEGER REFERENCES categories(id) ON DELETE RESTRICT"
			" ON UPDATE RESTRICT,"
		"PRIMARY KEY (package_id, category_id)"
	");"
	"CREATE TABLE licenses ("
		"id INTEGER PRIMARY KEY,"
		"name TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE pkg_licenses ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"license_id INTEGER REFERENCES licenses(id) ON DELETE RESTRICT"
			" ON UPDATE RESTRICT,"
		"PRIMARY KEY (package_id, license_id)"
	");"
	"CREATE TABLE users ("
		"id INTEGER PRIMARY KEY,"
		"name TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE pkg_users ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"user_id INTEGER REFERENCES users(id) ON DELETE RESTRICT"
			" ON UPDATE RESTRICT,"
		"UNIQUE(package_id, user_id)"
	");"
	"CREATE TABLE groups ("
		"id INTEGER PRIMARY KEY,"
		"name TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE pkg_groups ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"group_id INTEGER REFERENCES groups(id) ON DELETE RESTRICT"
			" ON UPDATE RESTRICT,"
		"UNIQUE(package_id, group_id)"
	");"
	"CREATE TABLE shlibs ("
		"id INTEGER PRIMARY KEY,"
		"name TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE pkg_shlibs ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"shlib_id INTEGER REFERENCES shlibs(id) ON DELETE RESTRICT"
			" ON UPDATE RESTRICT,"
		"PRIMARY KEY (package_id, shlib_id)"
	");"

	/* Mark the end of the array */

	"CREATE INDEX deporigini on deps(origin);"
	"CREATE INDEX scripts_package_id ON scripts (package_id);"
	"CREATE INDEX options_package_id ON options (package_id);"
	"CREATE INDEX deps_package_id ON deps (package_id);"
	"CREATE INDEX files_package_id ON files (package_id);"
	"CREATE INDEX pkg_directories_package_id ON pkg_directories (package_id);"
	"CREATE INDEX pkg_categories_package_id ON pkg_categories (package_id);"
	"CREATE INDEX pkg_licenses_package_id ON pkg_licenses (package_id);"
	"CREATE INDEX pkg_users_package_id ON pkg_users (package_id);"
	"CREATE INDEX pkg_groups_package_id ON pkg_groups (package_id);"
	"CREATE INDEX pkg_shlibs_package_id ON pkg_shlibs (package_id);"
	"CREATE INDEX pkg_directories_directory_id ON pkg_directories (directory_id);"

	"PRAGMA user_version = 12;"
	"COMMIT;"
	;

	return (sql_exec(sdb, sql));
}

int
pkgdb_open(struct pkgdb **db_p, pkgdb_t type)
{
	struct pkgdb *db = NULL;
	bool reopen = false;
	char localpath[MAXPATHLEN + 1];
	char remotepath[MAXPATHLEN + 1];
	const char *dbdir = NULL;
	const char *repo_name = NULL;
	bool multirepos_enabled = false;
	bool create = false;
	struct pkg_config_kv *repokv = NULL;

	if (*db_p != NULL) {
		reopen = true;
		db = *db_p;
		if (db->type == type)
			return (EPKG_OK);
	}

	if (pkg_config_string(PKG_CONFIG_DBDIR, &dbdir) != EPKG_OK)
		return (EPKG_FATAL);

	if (!reopen && (db = calloc(1, sizeof(struct pkgdb))) == NULL) {
		pkg_emit_errno("malloc", "pkgdb");
		return EPKG_FATAL;
	}

	db->type = type;

	if (!reopen) {
		snprintf(localpath, sizeof(localpath), "%s/local.sqlite", dbdir);

		if (eaccess(localpath, R_OK) != 0) {
			if (errno != ENOENT) {
				pkg_emit_nolocaldb();
				pkgdb_close(db);
				return (EPKG_ENODB);
			} else if (eaccess(dbdir, W_OK) != 0) {
				/* If we need to create the db but can not write to it, fail early */
				pkg_emit_nolocaldb();
				pkgdb_close(db);
				return (EPKG_ENODB);
			} else {
				create = true;
			}
		}

		sqlite3_initialize();
		if (sqlite3_open(localpath, &db->sqlite) != SQLITE_OK) {
			ERROR_SQLITE(db->sqlite);
			pkgdb_close(db);
			return (EPKG_FATAL);
		}

		/* Wait up to 5 seconds if database is busy */
		sqlite3_busy_timeout(db->sqlite, 5000);

		/* If the database is missing we have to initialize it */
		if (create == true)
			if (pkgdb_init(db->sqlite) != EPKG_OK) {
				pkgdb_close(db);
				return (EPKG_FATAL);
			}

		/* Create our functions */
		sqlcmd_init(db->sqlite, NULL, NULL);

		if (pkgdb_upgrade(db) != EPKG_OK) {
			pkgdb_close(db);
			return (EPKG_FATAL);
		}

		/*
		 * allow foreign key option which will allow to have clean support for
		 * reinstalling
		 */
		if (sql_exec(db->sqlite, "PRAGMA foreign_keys = ON;") != EPKG_OK) {
			pkgdb_close(db);
			return (EPKG_FATAL);
		}
	}

	if (type == PKGDB_REMOTE) {
		pkg_config_bool(PKG_CONFIG_MULTIREPOS, &multirepos_enabled);

		if (multirepos_enabled) {
			fprintf(stderr, "\t/!\\		   WARNING WARNING WARNING		/!\\\n");
			fprintf(stderr, "\t/!\\	     WORKING ON MULTIPLE REPOSITORIES		/!\\\n");
			fprintf(stderr, "\t/!\\  THIS FEATURE IS STILL CONSIDERED EXPERIMENTAL	/!\\\n");
			fprintf(stderr, "\t/!\\		     YOU HAVE BEEN WARNED		/!\\\n\n");

			while (pkg_config_list(PKG_CONFIG_REPOS, &repokv) == EPKG_OK) {
				repo_name = pkg_config_kv_get(repokv, PKG_CONFIG_KV_KEY);

				/* is it a reserved name? */
				if ((strcmp(repo_name, "repo") == 0) ||
				    (strcmp(repo_name, "main") == 0) ||
				    (strcmp(repo_name, "temp") == 0) ||
				    (strcmp(repo_name, "local") == 0))
					continue;

				/* is it already attached? */
				if (is_attached(db->sqlite, repo_name)) {
					pkg_emit_error("repository '%s' is already listed, ignoring", repo_name);
					continue;
				}

				snprintf(remotepath, sizeof(remotepath), "%s/%s.sqlite",
						dbdir, repo_name);

				if (access(remotepath, R_OK) != 0) {
					pkg_emit_noremotedb(repo_name);
					pkgdb_close(db);
					return (EPKG_ENODB);
				}

				if (sql_exec(db->sqlite, "ATTACH '%q' AS '%q';", remotepath, repo_name) != EPKG_OK) {
					pkgdb_close(db);
					return (EPKG_FATAL);
				}

				/* check if default repository exists */
				if (!is_attached(db->sqlite, "default")) {
					pkg_emit_error("no default repository defined");
					pkgdb_close(db);
					return (EPKG_FATAL);
				}
			}
		} else {
			/*
			 * Working on a single remote repository
			 */

			snprintf(remotepath, sizeof(remotepath), "%s/repo.sqlite", dbdir);

			if (access(remotepath, R_OK) != 0) {
				pkg_emit_noremotedb("repo");
				pkgdb_close(db);
				return (EPKG_ENODB);
			}

			if (sql_exec(db->sqlite, "ATTACH '%q' AS 'remote';", remotepath) != EPKG_OK) {
				pkgdb_close(db);
				return (EPKG_FATAL);
			}
		}
	}

	*db_p = db;
	return (EPKG_OK);
}

void
pkgdb_close(struct pkgdb *db)
{
	if (db == NULL)
		return;

	if (db->sqlite != NULL) {
		if (db->type == PKGDB_REMOTE) {
			pkgdb_detach_remotes(db->sqlite);
		}

		sqlite3_close(db->sqlite);
	}

	sqlite3_shutdown();
	free(db);
}

static struct pkgdb_it *
pkgdb_it_new(struct pkgdb *db, sqlite3_stmt *s, int type)
{
	struct pkgdb_it *it;

	assert(db != NULL && s != NULL);

	if ((it = malloc(sizeof(struct pkgdb_it))) == NULL) {
		pkg_emit_errno("malloc", "pkgdb_it");
		sqlite3_finalize(s);
		return (NULL);
	}

	it->db = db;
	it->stmt = s;
	it->type = type;
	return (it);
}


int
pkgdb_it_next(struct pkgdb_it *it, struct pkg **pkg_p, int flags)
{
	struct pkg *pkg;
	int ret;

	assert(it != NULL);

	switch (sqlite3_step(it->stmt)) {
	case SQLITE_ROW:
		if (*pkg_p == NULL)
			pkg_new(pkg_p, it->type);
		else
			pkg_reset(*pkg_p, it->type);
		pkg = *pkg_p;

		populate_pkg(it->stmt, pkg);

		if (flags & PKG_LOAD_DEPS)
			if ((ret = pkgdb_load_deps(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_RDEPS)
			if ((ret = pkgdb_load_rdeps(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_FILES)
			if ((ret = pkgdb_load_files(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_DIRS)
			if ((ret = pkgdb_load_dirs(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_SCRIPTS)
			if ((ret = pkgdb_load_scripts(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_OPTIONS)
			if ((ret = pkgdb_load_options(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_MTREE)
			if ((ret = pkgdb_load_mtree(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_CATEGORIES)
			if ((ret = pkgdb_load_category(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_LICENSES)
			if ((ret = pkgdb_load_license(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_USERS)
			if ((ret = pkgdb_load_user(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_GROUPS)
			if ((ret = pkgdb_load_group(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_SHLIBS)
			if ((ret = pkgdb_load_shlib(it->db, pkg)) != EPKG_OK)
				return (ret);

		return (EPKG_OK);
	case SQLITE_DONE:
		return (EPKG_END);
	default:
		ERROR_SQLITE(it->db->sqlite);
		return (EPKG_FATAL);
	}
}

void
pkgdb_it_free(struct pkgdb_it *it)
{
	if (it == NULL)
		return;

	if (!sqlite3_db_readonly(it->db->sqlite, "main")) {
		sql_exec(it->db->sqlite, "DROP TABLE IF EXISTS autoremove; "
			"DROP TABLE IF EXISTS delete_job; "
			"DROP TABLE IF EXISTS pkgjobs");
	}

	sqlite3_finalize(it->stmt);
	free(it);
}

static const char *
pkgdb_get_pattern_query(const char *pattern, match_t match)
{
	char *checkorigin = NULL;
	const char *comp = NULL;

	if (pattern != NULL) {
		checkorigin = strchr(pattern, '/');
	}

	switch (match) {
	case MATCH_ALL:
		comp = "";
		break;
	case MATCH_EXACT:
		if (checkorigin == NULL)
			comp = " WHERE name = ?1 "
				"OR name || \"-\" || version = ?1";
		else
			comp = " WHERE origin = ?1";
		break;
	case MATCH_GLOB:
		if (checkorigin == NULL)
			comp = " WHERE name GLOB ?1 "
				"OR name || \"-\" || version GLOB ?1";
		else
			comp = " WHERE origin GLOB ?1";
		break;
	case MATCH_REGEX:
		if (checkorigin == NULL)
			comp = " WHERE name REGEXP ?1 "
				"OR name || \"-\" || version REGEXP ?1";
		else
			comp = " WHERE origin REGEXP ?1";
		break;
	case MATCH_EREGEX:
		if (checkorigin == NULL)
			comp = " WHERE EREGEXP(?1, name) "
				"OR EREGEXP(?1, name || \"-\" || version)";
		else
			comp = " WHERE EREGEXP(?1, origin)";
		break;
	case MATCH_CONDITION:
		comp = pattern;
		break;
	}

	return (comp);
}

static const char *
pkgdb_get_match_how(match_t match)
{
	const char *how = NULL;

	switch (match) {
		case MATCH_ALL:
			how = NULL;
			break;
		case MATCH_EXACT:
			how = "%s = ?1";
			break;
		case MATCH_GLOB:
			how = "%s GLOB ?1";
			break;
		case MATCH_REGEX:
			how = "%s REGEXP ?1";
			break;
		case MATCH_EREGEX:
			how = "EREGEXP(?1, %s)";
			break;
		case MATCH_CONDITION:
			/* This case should not be called by pkgdb_get_match_how() */
			assert(0);
			break;
	}

	return (how);
}

struct pkgdb_it *
pkgdb_query(struct pkgdb *db, const char *pattern, match_t match)
{
	char sql[BUFSIZ];
	sqlite3_stmt *stmt;
	const char *comp = NULL;

	assert(db != NULL);
	assert(match == MATCH_ALL || (pattern != NULL && pattern[0] != '\0'));

	comp = pkgdb_get_pattern_query(pattern, match);

	snprintf(sql, sizeof(sql),
			"SELECT id, origin, name, version, comment, desc, "
				"message, arch, maintainer, www, "
				"prefix, flatsize, licenselogic, automatic, "
				"time, infos "
			"FROM packages AS p%s "
			"ORDER BY p.name;", comp);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	if (match != MATCH_ALL && match != MATCH_CONDITION)
		sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_INSTALLED));
}

struct pkgdb_it *
pkgdb_query_which(struct pkgdb *db, const char *path)
{
	sqlite3_stmt *stmt;
	const char sql[] = ""
		"SELECT p.id, p.origin, p.name, p.version, p.comment, p.desc, "
			"p.message, p.arch, p.maintainer, p.www, "
			"p.prefix, p.flatsize, p.time, p.infos "
			"FROM packages AS p, files AS f "
			"WHERE p.id = f.package_id "
				"AND f.path = ?1;";

	assert(db != NULL);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_INSTALLED));
}

struct pkgdb_it *
pkgdb_query_shlib(struct pkgdb *db, const char *shlib)
{
	sqlite3_stmt *stmt;
	const char sql[] = ""
		"SELECT p.id, p.origin, p.name, p.version, p.comment, p.desc, "
			"p.message, p.arch, p.maintainer, p.www, "
			"p.prefix, p.flatsize, p.time, p.infos "
			"FROM packages AS p, pkg_shlibs AS ps, shlibs AS s "
			"WHERE p.id = ps.package_id "
				"AND ps.shlib_id = s.id "
				"AND s.name = ?1;";

	assert(db != NULL);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sqlite3_bind_text(stmt, 1, shlib, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_INSTALLED));
}

int
pkgdb_is_dir_used(struct pkgdb *db, const char *dir, int64_t *res)
{
	sqlite3_stmt *stmt;
	int ret;

	const char sql[] = ""
		"SELECT count(package_id) FROM pkg_directories, directories "
		"WHERE directory_id = directories.id AND directories.path = ?1;";

	assert(db != NULL);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	sqlite3_bind_text(stmt, 1, dir, -1, SQLITE_TRANSIENT);

	ret = sqlite3_step(stmt);

	if (ret == SQLITE_ROW)
		*res = sqlite3_column_int64(stmt, 0);

	sqlite3_finalize(stmt);

	if (ret != SQLITE_ROW) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);


}

int
pkgdb_load_deps(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt = NULL;
	int ret = EPKG_OK;
	char sql[BUFSIZ];
	const char *reponame = NULL;
	const char *basesql = ""
			"SELECT d.name, d.origin, d.version "
			"FROM '%s'.deps AS d "
			"WHERE d.package_id = ?1;";

	assert(db != NULL && pkg != NULL);

	if (pkg->flags & PKG_LOAD_DEPS)
		return (EPKG_OK);

	if (pkg->type == PKG_REMOTE) {
		assert(db->type == PKGDB_REMOTE);
		pkg_get(pkg, PKG_REPONAME, &reponame);
		snprintf(sql, sizeof(sql), basesql, reponame);
	} else
		snprintf(sql, sizeof(sql), basesql, "main");

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	sqlite3_bind_int64(stmt, 1, pkg->rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_adddep(pkg, sqlite3_column_text(stmt, 0), sqlite3_column_text(stmt, 1),
				   sqlite3_column_text(stmt, 2));
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		pkg_list_free(pkg, PKG_DEPS);
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_DEPS;
	return (EPKG_OK);
}

int
pkgdb_load_rdeps(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt = NULL;
	int ret;
	const char *origin;
	const char *reponame = NULL;
	char sql[BUFSIZ];
	const char *basesql = ""
		"SELECT p.name, p.origin, p.version "
		"FROM '%s'.packages AS p, '%s'.deps AS d "
		"WHERE p.id = d.package_id "
			"AND d.origin = ?1;";

	assert(db != NULL && pkg != NULL);

	if (pkg->flags & PKG_LOAD_RDEPS)
		return (EPKG_OK);

	if (pkg->type == PKG_REMOTE) {
		assert(db->type == PKGDB_REMOTE);
		pkg_get(pkg, PKG_REPONAME, &reponame);
		snprintf(sql, sizeof(sql), basesql, reponame, reponame);
	} else
		snprintf(sql, sizeof(sql), basesql, "main", "main");

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg_get(pkg, PKG_ORIGIN, &origin);
	sqlite3_bind_text(stmt, 1, origin, -1, SQLITE_STATIC);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addrdep(pkg, sqlite3_column_text(stmt, 0), sqlite3_column_text(stmt, 1),
				   sqlite3_column_text(stmt, 2));
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		pkg_list_free(pkg, PKG_RDEPS);
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_RDEPS;
	return (EPKG_OK);
}

int
pkgdb_load_files(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt = NULL;
	int ret;
	const char sql[] = ""
		"SELECT path, sha256 "
		"FROM files "
		"WHERE package_id = ?1 "
		"ORDER BY PATH ASC";

	assert(db != NULL && pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_FILES)
		return (EPKG_OK);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	sqlite3_bind_int64(stmt, 1, pkg->rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addfile(pkg, sqlite3_column_text(stmt, 0), sqlite3_column_text(stmt, 1), false);
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		pkg_list_free(pkg, PKG_FILES);
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_FILES;
	return (EPKG_OK);
}

int
pkgdb_load_dirs(struct pkgdb *db, struct pkg *pkg)
{
	const char sql[] = ""
		"SELECT path, try "
		"FROM pkg_directories, directories "
		"WHERE package_id = ?1 "
		"AND directory_id = directories.id "
		"ORDER by path DESC";
	sqlite3_stmt *stmt;
	int ret;

	assert(db != NULL && pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_DIRS)
		return (EPKG_OK);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	sqlite3_bind_int64(stmt, 1, pkg->rowid);

	while (( ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_adddir(pkg, sqlite3_column_text(stmt, 0), sqlite3_column_int(stmt, 1));
	}

	sqlite3_finalize(stmt);
	if (ret != SQLITE_DONE) {
		pkg_list_free(pkg, PKG_DIRS);
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_DIRS;

	return (EPKG_OK);
}

int
pkgdb_load_license(struct pkgdb *db, struct pkg *pkg)
{
	char sql[BUFSIZ];
	const char *reponame = NULL;
	const char *basesql = ""
			"SELECT name "
			"FROM '%s'.pkg_licenses, '%s'.licenses AS l "
			"WHERE package_id = ?1 "
			"AND license_id = l.id "
			"ORDER by name DESC";

	assert(db != NULL && pkg != NULL);

	if (pkg->type == PKG_REMOTE) {
		assert(db->type == PKGDB_REMOTE);
		pkg_get(pkg, PKG_REPONAME, &reponame);
		snprintf(sql, sizeof(sql), basesql, reponame, reponame);
	} else
		snprintf(sql, sizeof(sql), basesql, "main", "main");

	return (load_val(db->sqlite, pkg, sql, PKG_LOAD_LICENSES, pkg_addlicense, PKG_LICENSES));
}

int
pkgdb_load_category(struct pkgdb *db, struct pkg *pkg)
{
	char sql[BUFSIZ];
	const char *reponame = NULL;
	const char *basesql = ""
			"SELECT name "
			"FROM '%s'.pkg_categories, '%s'.categories AS c "
			"WHERE package_id = ?1 "
			"AND category_id = c.id "
			"ORDER by name DESC";

	assert(db != NULL && pkg != NULL);

	if (pkg->type == PKG_REMOTE) {
		assert(db->type == PKGDB_REMOTE);
		pkg_get(pkg, PKG_REPONAME, &reponame);
		snprintf(sql, sizeof(sql), basesql, reponame, reponame);
	} else
		snprintf(sql, sizeof(sql), basesql, "main", "main");

	return (load_val(db->sqlite, pkg, sql, PKG_LOAD_CATEGORIES, pkg_addcategory, PKG_CATEGORIES));
}

int
pkgdb_load_user(struct pkgdb *db, struct pkg *pkg)
{
	/*struct pkg_user *u = NULL;
	struct passwd *pwd = NULL;*/
	int ret;

	const char sql[] = ""
		"SELECT users.name "
		"FROM pkg_users, users "
		"WHERE package_id = ?1 "
		"AND user_id = users.id "
		"ORDER by name DESC";

	assert(db != NULL && pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	ret = load_val(db->sqlite, pkg, sql, PKG_LOAD_USERS, pkg_adduser, PKG_USERS);

	/* TODO get user uidstr from local database */
/*	while (pkg_users(pkg, &u) == EPKG_OK) {
		pwd = getpwnam(pkg_user_name(u));
		if (pwd == NULL)
			continue;
		strlcpy(u->uidstr, pw_make(pwd), sizeof(u->uidstr));
	}*/

	return (ret);
}

int
pkgdb_load_group(struct pkgdb *db, struct pkg *pkg)
{
	struct pkg_group *g = NULL;
	struct group * grp = NULL;
	int ret;

	const char sql[] = ""
		"SELECT groups.name "
		"FROM pkg_groups, groups "
		"WHERE package_id = ?1 "
		"AND group_id = groups.id "
		"ORDER by name DESC";

	assert(db != NULL && pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	ret = load_val(db->sqlite, pkg, sql, PKG_LOAD_GROUPS, pkg_addgroup, PKG_GROUPS);

	while (pkg_groups(pkg, &g) == EPKG_OK) {
		grp = getgrnam(pkg_group_name(g));
		if (grp == NULL)
			continue;
		strlcpy(g->gidstr, gr_make(grp), sizeof(g->gidstr));
	}

	return (ret);
}

int
pkgdb_load_shlib(struct pkgdb *db, struct pkg *pkg)
{
	char sql[BUFSIZ];
	const char *reponame = NULL;
	const char *basesql = ""
			"SELECT name "
			"FROM '%s'.pkg_shlibs, '%s'.shlibs AS s "
			"WHERE package_id = ?1 "
			"AND shlib_id = s.id "
			"ORDER by name DESC";

	assert(db != NULL && pkg != NULL);

	if (pkg->type == PKG_REMOTE) {
		assert(db->type == PKGDB_REMOTE);
		pkg_get(pkg, PKG_REPONAME, &reponame);
		snprintf(sql, sizeof(sql), basesql, reponame, reponame);
	} else
		snprintf(sql, sizeof(sql), basesql, "main", "main");

	return (load_val(db->sqlite, pkg, sql, PKG_LOAD_SHLIBS, pkg_addshlib, PKG_SHLIBS));
}

int
pkgdb_load_scripts(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt = NULL;
	int ret;
	const char sql[] = ""
		"SELECT script, type "
		"FROM scripts "
		"WHERE package_id = ?1";

	assert(db != NULL && pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_SCRIPTS)
		return (EPKG_OK);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	sqlite3_bind_int64(stmt, 1, pkg->rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addscript(pkg, sqlite3_column_text(stmt, 0), sqlite3_column_int(stmt, 1));
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		pkg_list_free(pkg, PKG_SCRIPTS);
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_SCRIPTS;
	return (EPKG_OK);
}

int
pkgdb_load_options(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt = NULL;
	int ret = EPKG_OK;
	const char *reponame;
	char sql[BUFSIZ];
	const char *basesql = ""
		"SELECT option, value "
		"FROM '%s'.options "
		"WHERE package_id = ?1";

	assert(db != NULL && pkg != NULL);

	if (pkg->flags & PKG_LOAD_OPTIONS)
		return (EPKG_OK);

	if (pkg->type == PKG_REMOTE) {
		assert(db->type == PKGDB_REMOTE);
		pkg_get(pkg, PKG_REPONAME, &reponame);
		snprintf(sql, sizeof(sql), basesql, reponame);
	} else {
		snprintf(sql, sizeof(sql), basesql, "main");
	}

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	sqlite3_bind_int64(stmt, 1, pkg->rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addoption(pkg, sqlite3_column_text(stmt, 0),
					  sqlite3_column_text(stmt, 1));
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		pkg_list_free(pkg, PKG_OPTIONS);
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_OPTIONS;
	return (EPKG_OK);
}

int
pkgdb_load_mtree(struct pkgdb *db, struct pkg *pkg)
{
	const char sql[] = ""
		"SELECT m.content "
		"FROM mtree AS m, packages AS p "
		"WHERE m.id = p.mtree_id "
			" AND p.id = ?1;";

	assert(db != NULL && pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	return (load_val(db->sqlite, pkg, sql, PKG_LOAD_MTREE, pkg_set_mtree, -1));
}

int
pkgdb_register_pkg(struct pkgdb *db, struct pkg *pkg, int complete)
{
	struct pkg *pkg2 = NULL;
	struct pkg_dep *dep = NULL;
	struct pkg_file *file = NULL;
	struct pkg_dir *dir = NULL;
	struct pkg_script *script = NULL;
	struct pkg_option *option = NULL;
	struct pkg_category *category = NULL;
	struct pkg_license *license = NULL;
	struct pkg_user *user = NULL;
	struct pkg_group *group = NULL;
	struct pkg_shlib *shlib = NULL;
	struct pkgdb_it *it = NULL;

	sqlite3 *s;
	sqlite3_stmt *stmt = NULL;
	sqlite3_stmt *stmt2 = NULL;

	int ret;
	int retcode = EPKG_FATAL;
	int64_t package_id;

	const char sql_mtree[] = "INSERT OR IGNORE INTO mtree(content) VALUES(?1);";
	const char sql_dirs[] = "INSERT OR IGNORE INTO directories(path) VALUES(?1);";
	const char sql_pkg[] = ""
		"INSERT OR REPLACE INTO packages( "
			"origin, name, version, comment, desc, message, arch, "
			"maintainer, www, prefix, flatsize, automatic, licenselogic, "
			"mtree_id, infos, time) "
		"VALUES( ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, "
		"(SELECT id from mtree where content = ?14), ?15, now());";
	const char sql_dep[] = ""
		"INSERT OR ROLLBACK INTO deps (origin, name, version, package_id) "
		"VALUES (?1, ?2, ?3, ?4);";
	const char sql_file[] = ""
		"INSERT OR ROLLBACK INTO files (path, sha256, package_id) "
		"VALUES (?1, ?2, ?3);";
	const char sql_script[] = ""
		"INSERT OR ROLLBACK INTO scripts (script, type, package_id) "
		"VALUES (?1, ?2, ?3);";
	const char sql_option[] = ""
		"INSERT OR ROLLBACK INTO options (option, value, package_id) "
		"VALUES (?1, ?2, ?3);";
	const char sql_dir[] = ""
		"INSERT OR ROLLBACK INTO pkg_directories(package_id, directory_id, try) "
		"VALUES (?1, "
		"(SELECT id FROM directories WHERE path = ?2), ?3);";
	const char sql_cat[] = "INSERT OR IGNORE INTO categories(name) VALUES(?1);";
	const char sql_category[] = ""
		"INSERT OR ROLLBACK INTO pkg_categories(package_id, category_id) "
		"VALUES (?1, (SELECT id FROM categories WHERE name = ?2));";
	const char sql_lic[] = "INSERT OR IGNORE INTO licenses(name) VALUES(?1);";
	const char sql_license[] = ""
		"INSERT OR ROLLBACK INTO pkg_licenses(package_id, license_id) "
		"VALUES (?1, (SELECT id FROM licenses WHERE name = ?2));";
	const char sql_user[] = "INSERT OR IGNORE INTO users(name) VALUES(?1);";
	const char sql_users[] = ""
		"INSERT OR ROLLBACK INTO pkg_users(package_id, user_id) "
		"VALUES (?1, (SELECT id FROM users WHERE name = ?2));";
	const char sql_group[] = "INSERT OR IGNORE INTO groups(name) VALUES(?1);";
	const char sql_groups[] = ""
		"INSERT OR ROLLBACK INTO pkg_groups(package_id, group_id) "
		"VALUES (?1, (SELECT id FROM groups WHERE name = ?2));";
	const char sql_shlib[] = "INSERT OR IGNORE INTO shlibs(name) VALUES(?1);";
	const char sql_shlibs[] = ""
		"INSERT OR ROLLBACK INTO pkg_shlibs(package_id, shlib_id) "
		"VALUES (?1, (SELECT id FROM shlibs WHERE name = ?2))";
	const char sql_deps_update[] = ""
		"UPDATE deps SET NAME=?1 , VERSION=?2 WHERE ORIGIN=?3;";

	const char *mtree, *origin, *name, *version, *name2, *version2;
	const char *comment, *desc, *message, *infos;
	const char *arch, *maintainer, *www, *prefix;

	bool automatic;
	int64_t flatsize;
	lic_t licenselogic;

	assert(db != NULL);

	s = db->sqlite;

	if (!complete && sql_exec(s, "BEGIN;") != EPKG_OK)
		return (EPKG_FATAL);

	/* insert mtree record if any */
	if (sqlite3_prepare_v2(s, sql_mtree, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	pkg_get(pkg, PKG_MTREE, &mtree, PKG_ORIGIN, &origin, PKG_VERSION, &version,
	    PKG_COMMENT, &comment, PKG_DESC, &desc, PKG_MESSAGE, &message,
	    PKG_ARCH, &arch, PKG_MAINTAINER, &maintainer,
	    PKG_WWW, &www, PKG_PREFIX, &prefix, PKG_FLATSIZE, &flatsize,
	    PKG_AUTOMATIC, &automatic, PKG_LICENSE_LOGIC, &licenselogic,
	    PKG_NAME, &name, PKG_INFOS, &infos);

	sqlite3_bind_text(stmt, 1, mtree, -1, SQLITE_STATIC);

	if ((ret = sqlite3_step(stmt)) != SQLITE_DONE) {
		ERROR_SQLITE(s);
		goto cleanup;
	}
	sqlite3_finalize(stmt);
	stmt = NULL;

	/* Insert package record */
	if (sqlite3_prepare_v2(s, sql_pkg, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}
	sqlite3_bind_text(stmt, 1, origin, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, version, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 4, comment, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 5, desc, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 6, message, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 7, arch, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 8, maintainer, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 9, www,  -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 10, prefix, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 11, flatsize);
	sqlite3_bind_int(stmt, 12, automatic);
	sqlite3_bind_int64(stmt, 13, licenselogic);
	sqlite3_bind_text(stmt, 14, mtree, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 15, infos, -1, SQLITE_STATIC);

	if ((ret = sqlite3_step(stmt)) != SQLITE_DONE) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	package_id = sqlite3_last_insert_rowid(s);
	sqlite3_finalize(stmt);
	stmt = NULL;

	/*
	 * update dep informations on packages that depends on the insert
	 * package
	 */

	if (sqlite3_prepare_v2(s, sql_deps_update, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, version, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, origin, -1, SQLITE_STATIC);

	if ((ret = sqlite3_step(stmt)) != SQLITE_DONE) {
		ERROR_SQLITE(s);
		goto cleanup;
	}
	sqlite3_finalize(stmt);
	stmt = NULL;

	/*
	 * Insert dependencies list
	 */

	if (sqlite3_prepare_v2(s, sql_dep, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		sqlite3_bind_text(stmt, 1, pkg_dep_get(dep, PKG_DEP_ORIGIN), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, pkg_dep_get(dep, PKG_DEP_NAME), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 3, pkg_dep_get(dep, PKG_DEP_VERSION), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt, 4, package_id);

		if ((ret = sqlite3_step(stmt)) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt);
	}
	sqlite3_finalize(stmt);
	stmt = NULL;

	/*
	 * Insert files.
	 */

	if (sqlite3_prepare_v2(s, sql_file, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_files(pkg, &file) == EPKG_OK) {
		sqlite3_bind_text(stmt, 1, pkg_file_get(file, PKG_FILE_PATH), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, pkg_file_get(file, PKG_FILE_SUM), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt, 3, package_id);

		if ((ret = sqlite3_step(stmt)) != SQLITE_DONE) {
			if (ret == SQLITE_CONSTRAINT) {
				if ((it = pkgdb_query_which(db, pkg_file_get(file, PKG_FILE_PATH))) == NULL) {
					ERROR_SQLITE(s);
				}
				if (( ret = pkgdb_it_next(it, &pkg2, PKG_LOAD_BASIC)) == EPKG_OK) {
					pkg_get(pkg2, PKG_NAME, &name2, PKG_VERSION, &version2);
					pkg_emit_error("%s-%s conflicts with %s-%s"
					    " (installs files into the same place). "
					    " Problematic file: %s",
					    name, version, name2, version2,
					    pkg_file_get(file, PKG_FILE_PATH));
					pkg_free(pkg2);
				} else {
					ERROR_SQLITE(s);
				}
				pkgdb_it_free(it);
			} else {
				ERROR_SQLITE(s);
			}
			goto cleanup;
		}
		sqlite3_reset(stmt);
	}
	sqlite3_finalize(stmt);
	stmt = NULL;

	/*
	 * Insert dirs.
	 */

	if (sqlite3_prepare_v2(s, sql_dirs, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	if (sqlite3_prepare_v2(s, sql_dir, -1, &stmt2, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		sqlite3_bind_text(stmt, 1, pkg_dir_path(dir), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt2, 1, package_id);
		sqlite3_bind_text(stmt2, 2, pkg_dir_path(dir), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt2, 3, pkg_dir_try(dir));

		if ((ret = sqlite3_step(stmt)) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		if ((ret = sqlite3_step(stmt2)) != SQLITE_DONE) {
			if ( ret == SQLITE_CONSTRAINT) {
				pkg_emit_error("A package is already providing this directory: %s", pkg_dir_path(dir));
			} else
				ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt2);
		sqlite3_reset(stmt);
	}
	sqlite3_finalize(stmt2);
	sqlite3_finalize(stmt);
	stmt = NULL;
	stmt2 = NULL;

	/*
	 * Insert categories
	 */

	if (sqlite3_prepare_v2(s, sql_category, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}
	if (sqlite3_prepare_v2(s, sql_cat, -1, &stmt2, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_categories(pkg, &category) == EPKG_OK) {
		sqlite3_bind_text(stmt2, 1, pkg_category_name(category), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt, 1, package_id);
		sqlite3_bind_text(stmt, 2, pkg_category_name(category), -1, SQLITE_STATIC);

		if ((ret = sqlite3_step(stmt2)) != SQLITE_DONE) {
			if (ret == SQLITE_CONSTRAINT) {
				pkg_emit_error("sqlite: constraint violation on categories.name: %s",
						pkg_category_name(category));
			} else
				ERROR_SQLITE(s);
			goto cleanup;
		}
		if (( ret = sqlite3_step(stmt)) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt);
		sqlite3_reset(stmt2);
	}
	sqlite3_finalize(stmt2);
	sqlite3_finalize(stmt);
	stmt = NULL;
	stmt2 = NULL;

	/*
	 * Insert licenses
	 */
	if (sqlite3_prepare_v2(s, sql_lic, -1, &stmt2, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}
	if (sqlite3_prepare_v2(s, sql_license, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_licenses(pkg, &license) == EPKG_OK) {
		sqlite3_bind_text(stmt2, 1, pkg_license_name(license), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt, 1, package_id);
		sqlite3_bind_text(stmt, 2, pkg_license_name(license), -1, SQLITE_STATIC);

		if ((ret = sqlite3_step(stmt2)) != SQLITE_DONE) {
			if (ret == SQLITE_CONSTRAINT) {
				pkg_emit_error("sqlite: constraint violation on licenses.name: %s",
						pkg_license_name(license));
			} else
				ERROR_SQLITE(s);
			goto cleanup;
		}
		if (( ret = sqlite3_step(stmt)) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt);
		sqlite3_reset(stmt2);
	}
	sqlite3_finalize(stmt2);
	sqlite3_finalize(stmt);
	stmt = NULL;
	stmt2 = NULL;

	/*
	 * Insert users
	 */
	if (sqlite3_prepare_v2(s, sql_user, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}
	if (sqlite3_prepare_v2(s, sql_users, -1, &stmt2, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_users(pkg, &user) == EPKG_OK) {
		sqlite3_bind_text(stmt, 1, pkg_user_name(user), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt2, 1, package_id);
		sqlite3_bind_text(stmt2, 2, pkg_user_name(user), -1, SQLITE_STATIC);

		if ((ret = sqlite3_step(stmt)) != SQLITE_DONE) {
			if (ret == SQLITE_CONSTRAINT) {
				pkg_emit_error("sqlite: constraint violation on users.name: %s",
						pkg_user_name(user));
			} else
				ERROR_SQLITE(s);
			goto cleanup;
		}
		if (( ret = sqlite3_step(stmt2)) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt);
		sqlite3_reset(stmt2);
	}
	sqlite3_finalize(stmt2);
	sqlite3_finalize(stmt);
	stmt = NULL;
	stmt2 = NULL;

	/*
	 * Insert groups
	 */
	if (sqlite3_prepare_v2(s, sql_group, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}
	if (sqlite3_prepare_v2(s, sql_groups, -1, &stmt2, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_groups(pkg, &group) == EPKG_OK) {
		sqlite3_bind_text(stmt, 1, pkg_group_name(group), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt2, 1, package_id);
		sqlite3_bind_text(stmt2, 2, pkg_group_name(group), -1, SQLITE_STATIC);

		if ((ret = sqlite3_step(stmt)) != SQLITE_DONE) {
			if (ret == SQLITE_CONSTRAINT) {
				pkg_emit_error("sqlite: constraint violation on groups.name: %s",
						pkg_group_name(group));
			} else
				ERROR_SQLITE(s);
			goto cleanup;
		}
		if (( ret = sqlite3_step(stmt2)) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt);
		sqlite3_reset(stmt2);
	}
	sqlite3_finalize(stmt2);
	sqlite3_finalize(stmt);
	stmt = NULL;
	stmt2 = NULL;

	/*
	 * Insert scripts
	 */

	if (sqlite3_prepare_v2(s, sql_script, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_scripts(pkg, &script) == EPKG_OK) {
		sqlite3_bind_text(stmt, 1, pkg_script_data(script), -1, SQLITE_STATIC);
		sqlite3_bind_int(stmt, 2, pkg_script_type(script));
		sqlite3_bind_int64(stmt, 3, package_id);

		if (sqlite3_step(stmt) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt);
	}
	sqlite3_finalize(stmt);
	stmt = NULL;

	/*
	 * Insert options
	 */

	if (sqlite3_prepare_v2(s, sql_option, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_options(pkg, &option) == EPKG_OK) {
		sqlite3_bind_text(stmt, 1, pkg_option_opt(option), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, pkg_option_value(option), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt, 3, package_id);

		if (sqlite3_step(stmt) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt);
	}
	sqlite3_finalize(stmt);
	stmt = NULL;

	/*
	 * Insert shlibs
	 */

	if (sqlite3_prepare_v2(s, sql_shlib, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}
	if (sqlite3_prepare_v2(s, sql_shlibs, -1, &stmt2, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_shlibs(pkg, &shlib) == EPKG_OK) {
		sqlite3_bind_text(stmt, 1, pkg_shlib_name(shlib), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt2, 1, package_id);
		sqlite3_bind_text(stmt2, 2, pkg_shlib_name(shlib), -1, SQLITE_STATIC);

		if ((ret = sqlite3_step(stmt)) != SQLITE_DONE) {
			if (ret == SQLITE_CONSTRAINT) {
				pkg_emit_error("sqlite: constraint violation on shlibs.name: %s",
						pkg_shlib_name(shlib));
			} else
				ERROR_SQLITE(s);
			goto cleanup;
		}
		if (( ret = sqlite3_step(stmt2)) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt);
		sqlite3_reset(stmt2);
	}
	sqlite3_finalize(stmt);
	sqlite3_finalize(stmt2);
	stmt = NULL;
	stmt2 = NULL;

	retcode = EPKG_OK;

	cleanup:

	if (stmt != NULL)
		sqlite3_finalize(stmt);
	if (stmt2 != NULL)
		sqlite3_finalize(stmt2);

	return (retcode);
}

int
pkgdb_register_finale(struct pkgdb *db, int retcode)
{
	int ret = EPKG_OK;
	const char *cmd;

	assert(db != NULL);

	cmd = (retcode == EPKG_OK) ? "COMMIT;" : "ROLLBACK;";
	ret = sql_exec(db->sqlite, cmd);

	return (ret);
}

int
pkgdb_register_ports(struct pkgdb *db, struct pkg *pkg)
{
	int ret;
	pkg_emit_install_begin(pkg);

	ret = pkgdb_register_pkg(db, pkg, 0);
	if (ret == EPKG_OK)
		pkg_emit_install_finished(pkg);

	pkgdb_register_finale(db, ret);
	return (ret);
}

int
pkgdb_unregister_pkg(struct pkgdb *db, const char *origin)
{
	sqlite3_stmt *stmt_del;
	int ret;
	const char sql[] = "DELETE FROM packages WHERE origin = ?1;";

	assert(db != NULL);
	assert(origin != NULL);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt_del, NULL) != SQLITE_OK){
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	sqlite3_bind_text(stmt_del, 1, origin, -1, SQLITE_STATIC);

	ret = sqlite3_step(stmt_del);
	sqlite3_finalize(stmt_del);

	if (ret != SQLITE_DONE) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	/* cleanup directories */
	if (sql_exec(db->sqlite, "DELETE from directories WHERE id NOT IN (SELECT DISTINCT directory_id FROM pkg_directories);") != EPKG_OK)
		return (EPKG_FATAL);

	if (sql_exec(db->sqlite, "DELETE from categories WHERE id NOT IN (SELECT DISTINCT category_id FROM pkg_categories);") != EPKG_OK)
		return (EPKG_FATAL);

	if (sql_exec(db->sqlite, "DELETE from licenses WHERE id NOT IN (SELECT DISTINCT license_id FROM pkg_licenses);") != EPKG_OK)
		return (EPKG_FATAL);

	if (sql_exec(db->sqlite, "DELETE FROM mtree WHERE id NOT IN (SELECT DISTINCT mtree_id FROM packages);") != EPKG_OK)
		return (EPKG_FATAL);

	/* TODO print the users that are not used anymore */
	if (sql_exec(db->sqlite, "DELETE FROM users WHERE id NOT IN (SELECT DISTINCT user_id FROM pkg_users);") != EPKG_OK)
		return (EPKG_FATAL);

	/* TODO print the groups trhat are not used anymore */
	if (sql_exec(db->sqlite, "DELETE FROM groups WHERE id NOT IN (SELECT DISTINCT group_id FROM pkg_groups);") != EPKG_OK)
		return (EPKG_FATAL);

	if (sql_exec(db->sqlite, "DELETE FROM shlibs WHERE id NOT IN (SELECT DISTINCT shlib_id FROM pkg_shlibs);") != EPKG_OK)
		return (EPKG_FATAL);

	return (EPKG_OK);
}

int
sql_exec(sqlite3 *s, const char *sql, ...)
{
	va_list ap;
	const char *sql_to_exec;
	char *sqlbuf = NULL;
	char *errmsg;
	int ret = EPKG_FATAL;

	assert(s != NULL);
	assert(sql != NULL);

	if (strchr(sql, '%') != NULL) {
		va_start(ap, sql);
		sqlbuf = sqlite3_vmprintf(sql, ap);
		va_end(ap);
		sql_to_exec = sqlbuf;
	} else {
		sql_to_exec = sql;
	}

	if (sqlite3_exec(s, sql_to_exec, NULL, NULL, &errmsg) != SQLITE_OK) {
		pkg_emit_error("sqlite: %s", errmsg);
		sqlite3_free(errmsg);
		goto cleanup;
	}

	ret = EPKG_OK;

	cleanup:
	if (sqlbuf != NULL)
		sqlite3_free(sqlbuf);

	return (ret);
}

static bool
is_attached(sqlite3 *s, const char *name)
{
	sqlite3_stmt *stmt;
	const char *dbname;

	assert(s != NULL);

	if (sqlite3_prepare_v2(s, "PRAGMA database_list;", -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		return false;
	}

	while (sqlite3_step(stmt) != SQLITE_DONE) {
		dbname = sqlite3_column_text(stmt, 1);
		if (!strcmp(dbname, name)) {
			sqlite3_finalize(stmt);
			return (true);
		}
	}

	sqlite3_finalize(stmt);

	return (false);
}

static void
report_already_installed(sqlite3 *s)
{
	sqlite3_stmt *stmt = NULL;
	const char *origin = NULL;
	const char *sql = "SELECT origin FROM pkgjobs WHERE "
		"(SELECT p.origin FROM main.packages AS p WHERE "
		"p.origin=pkgjobs.origin AND p.version=pkgjobs.version) IS NOT NULL;";

	assert(s != NULL);

	if (sqlite3_prepare_v2(s, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		return;
	}

	while (sqlite3_step(stmt) != SQLITE_DONE) {
		origin = sqlite3_column_text(stmt, 0);
		pkg_emit_error("%s is already installed and at the latest version", origin);
	}

	sqlite3_finalize(stmt);
}

static int
sql_on_all_attached_db(sqlite3 *s, struct sbuf *sql, const char *multireposql) {
	sqlite3_stmt *stmt;
	const char *dbname;
	bool first = true;

	assert(s != NULL);

	if (sqlite3_prepare_v2(s, "PRAGMA database_list;", -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		return (EPKG_FATAL);
	}

	while (sqlite3_step(stmt) != SQLITE_DONE) {
		dbname = sqlite3_column_text(stmt, 1);
		if ((strcmp(dbname, "main") == 0) || (strcmp(dbname, "temp") == 0))
			continue;

		if (!first) {
			sbuf_cat(sql, " UNION ALL ");
		} else {
			first = false;
		}
		sbuf_printf(sql, multireposql, dbname, dbname);
	}

	sqlite3_finalize(stmt);

	return (EPKG_OK);
}

static void
pkgdb_detach_remotes(sqlite3 *s)
{
	sqlite3_stmt *stmt;
	struct sbuf *sql = NULL;
	const char *dbname;

	assert(s != NULL);

	if (sqlite3_prepare_v2(s, "PRAGMA database_list;", -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		return;
	}

	sql = sbuf_new_auto();

	while (sqlite3_step(stmt) != SQLITE_DONE) {
		dbname = sqlite3_column_text(stmt, 1);
		if ((strcmp(dbname, "main") == 0) ||
		    (strcmp(dbname, "temp") == 0))
			continue;

		sbuf_clear(sql);
		sbuf_printf(sql, "DETACH '%s';", dbname);
		sbuf_finish(sql);
		sql_exec(s, sbuf_get(sql));
	}

	sqlite3_finalize(stmt);

	sbuf_delete(sql);
}

static int
get_pragma(sqlite3 *s, const char *sql, int64_t *res)
{
	sqlite3_stmt *stmt;
	int ret;

	assert(s != NULL && sql != NULL);

	if (sqlite3_prepare_v2(s, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		return (EPKG_OK);
	}

	ret = sqlite3_step(stmt);

	if (ret == SQLITE_ROW)
		*res = sqlite3_column_int64(stmt, 0);

	sqlite3_finalize(stmt);

	if (ret != SQLITE_ROW) {
		ERROR_SQLITE(s);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

int
pkgdb_compact(struct pkgdb *db)
{
	int64_t page_count = 0;
	int64_t freelist_count = 0;

	assert(db != NULL);

	if (get_pragma(db->sqlite, "PRAGMA page_count;", &page_count) != EPKG_OK)
		return (EPKG_FATAL);

	if (get_pragma(db->sqlite, "PRAGMA freelist_count;", &freelist_count) !=
		EPKG_OK)
		return (EPKG_FATAL);

	/*
	 * Only compact if we will save 25% (or more) of the current used space.
	 */
	if (freelist_count / (float)page_count < 0.25)
		return (EPKG_OK);

	return (sql_exec(db->sqlite, "VACUUM;"));
}

static int
create_temporary_pkgjobs(sqlite3 *s)
{
	int ret;

	assert(s != NULL);

	ret = sql_exec(s, "DROP TABLE IF EXISTS pkgjobs;"
			"CREATE TEMPORARY TABLE IF NOT EXISTS pkgjobs (pkgid INTEGER, "
			"origin TEXT UNIQUE NOT NULL, name TEXT, version TEXT, "
			"comment TEXT, desc TEXT, message TEXT, "
			"arch TEXT, maintainer TEXT, "
			"www TEXT, prefix TEXT, flatsize INTEGER, newversion TEXT, "
			"newflatsize INTEGER, pkgsize INTEGER, cksum TEXT, repopath TEXT, automatic INTEGER, weight INTEGER"
			"dbname TEXT);");

	return (ret);
}

static struct pkgdb_it *
pkgdb_query_newpkgversion(struct pkgdb *db, const char *repo)
{
	struct sbuf *sql = sbuf_new_auto();
	const char *reponame = NULL;
	sqlite3_stmt *stmt = NULL;

	const char finalsql[] = "SELECT pkgid AS id, origin, name, version, "
		"comment, desc, message, arch, maintainer, "
		"www, prefix, flatsize, newversion, newflatsize, pkgsize, "
		"cksum, repopath, automatic, weight, "
		"'%s' AS dbname FROM pkgjobs;";

	const char main_sql[] = "INSERT OR IGNORE INTO pkgjobs (pkgid, origin, name, newversion, comment, desc, arch, "
			"maintainer, www, prefix, newflatsize, pkgsize, "
			"version, flatsize, cksum, repopath, automatic) "
			"SELECT p.id, p.origin, p.name, p.version as newversion, p.comment, p.desc, "
			"p.arch, p.maintainer, p.www, p.prefix, p.flatsize as newflatsize, p.pkgsize, "
			"l.version as version, l.flatsize as flatsize, "
			"p.cksum, p.path, 0 FROM '%s'.packages as p, packages as l WHERE p.origin='ports-mgmt/pkg' AND l.origin='ports-mgmt/pkg';";

	assert(db != NULL);
	assert(db->type == PKGDB_REMOTE);

	if ((reponame = pkgdb_get_reponame(db, repo)) == NULL) {
		return (NULL);
	}

	sbuf_printf(sql, main_sql, reponame);

	create_temporary_pkgjobs(db->sqlite);
	sbuf_finish(sql);
	sql_exec(db->sqlite, sbuf_get(sql));

	sql_exec(db->sqlite, "DELETE FROM pkgjobs WHERE PKGLT(version, newversion) OR version == newversion;");

	if (sqlite3_changes(db->sqlite) > 0) {
		sbuf_delete(sql);
		return NULL;
	}
	/* Final SQL */
	sbuf_reset(sql);
	sbuf_printf(sql, finalsql, reponame);
	sbuf_finish(sql);

	if (sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sbuf_delete(sql);

	return (pkgdb_it_new(db, stmt, PKG_REMOTE));
}

struct pkgdb_it *
pkgdb_query_installs(struct pkgdb *db, match_t match, int nbpkgs, char **pkgs, const char *repo, bool force)
{
	sqlite3_stmt *stmt = NULL;
	struct pkgdb_it *it;
	int i = 0;
	struct sbuf *sql = sbuf_new_auto();
	const char *how = NULL;
	const char *reponame = NULL;

	if ((it = pkgdb_query_newpkgversion(db, repo)) != NULL) {
		pkg_emit_newpkgversion();
		return (it);
	}

	const char finalsql[] = "SELECT pkgid AS id, origin, name, version, "
		"comment, desc, message, arch, maintainer, "
		"www, prefix, flatsize, newversion, newflatsize, pkgsize, "
		"cksum, repopath, automatic, weight, "
		"'%s' AS dbname FROM pkgjobs ORDER BY weight DESC;";

	const char main_sql[] = "INSERT OR IGNORE INTO pkgjobs (pkgid, origin, name, version, comment, desc, arch, "
			"maintainer, www, prefix, flatsize, pkgsize, "
			"cksum, repopath, automatic) "
			"SELECT id, origin, name, version, comment, desc, "
			"arch, maintainer, www, prefix, flatsize, pkgsize, "
			"cksum, path, 0 FROM '%s'.packages WHERE ";

	const char deps_sql[] = "INSERT OR IGNORE INTO pkgjobs (pkgid, origin, name, version, comment, desc, arch, "
				"maintainer, www, prefix, flatsize, pkgsize, "
				"cksum, repopath, automatic) "
				"SELECT DISTINCT r.id, r.origin, r.name, r.version, r.comment, r.desc, "
				"r.arch, r.maintainer, r.www, r.prefix, r.flatsize, r.pkgsize, "
				"r.cksum, r.path, 1 "
				"FROM '%s'.packages AS r where r.origin IN "
				"(SELECT d.origin FROM '%s'.deps AS d, pkgjobs AS j WHERE d.package_id = j.pkgid) "
				"AND (SELECT origin FROM main.packages WHERE origin=r.origin AND version=r.version) IS NULL;";

	const char weight_sql[] = "UPDATE pkgjobs set weight=(select count(*) from '%s'.deps as d where d.origin=pkgjobs.origin)";

	assert(db != NULL);
	assert(db->type == PKGDB_REMOTE);

	if ((reponame = pkgdb_get_reponame(db, repo)) == NULL) {
		return (NULL);
	}

	sbuf_printf(sql, main_sql, reponame);

	how = pkgdb_get_match_how(match);

	create_temporary_pkgjobs(db->sqlite);

	sbuf_printf(sql, how, "name");
	sbuf_cat(sql, " OR ");
	sbuf_printf(sql, how, "origin");
	sbuf_cat(sql, " OR ");
	sbuf_printf(sql, how, "name || \"-\" || version");
	sbuf_finish(sql);

	for (i = 0; i < nbpkgs; i++) {
		if (sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL) != SQLITE_OK) {
			ERROR_SQLITE(db->sqlite);
			return (NULL);
		}
		sqlite3_bind_text(stmt, 1, pkgs[i], -1, SQLITE_STATIC);
		while (sqlite3_step(stmt) != SQLITE_DONE);

		/* report if package was not found in the database */
		if (sqlite3_changes(db->sqlite) == 0)
			pkg_emit_error("Package '%s' was not found in the repositories", pkgs[i]);
	}

	sqlite3_finalize(stmt);
	sbuf_clear(sql);

	/* Report and remove packages already installed and at the latest version */
	report_already_installed(db->sqlite);
	if (!force)
		sql_exec(db->sqlite, "DELETE from pkgjobs where (select p.origin from main.packages as p where p.origin=pkgjobs.origin and p.version=pkgjobs.version) IS NOT NULL;");

	/* Append dependencies */
	sbuf_reset(sql);
	sbuf_printf(sql, deps_sql, reponame, reponame);
	sbuf_finish(sql);

	do {
		sql_exec(db->sqlite, sbuf_get(sql));
	} while (sqlite3_changes(db->sqlite) != 0);


	/* Determine if there is an upgrade needed */
	sql_exec(db->sqlite, "INSERT OR REPLACE INTO pkgjobs (pkgid, origin, name, version, comment, desc, message, arch, "
			"maintainer, www, prefix, flatsize, newversion, newflatsize, pkgsize, "
			"cksum, repopath, automatic) "
			"SELECT l.id, l.origin, l.name, l.version, l.comment, l.desc, l.message, l.arch, "
			"l.maintainer, l.www, l.prefix, l.flatsize, r.version AS newversion, "
			"r.flatsize AS newflatsize, r.pkgsize, r.cksum, r.repopath, r.automatic "
			"FROM main.packages AS l, pkgjobs AS r WHERE l.origin = r.origin ");

	sbuf_reset(sql);
	sbuf_printf(sql, weight_sql, reponame);
	sbuf_finish(sql);

	sql_exec(db->sqlite, sbuf_get(sql));

	sql_exec(db->sqlite, "UPDATE pkgjobs set weight=100000 where origin=\"ports-mgmt/pkg\"");

	sbuf_reset(sql);
	sbuf_printf(sql, finalsql, reponame);
	sbuf_finish(sql);

	if (sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sbuf_finish(sql);
	sbuf_delete(sql);

	return (pkgdb_it_new(db, stmt, PKG_REMOTE));
}

struct pkgdb_it *
pkgdb_query_upgrades(struct pkgdb *db, const char *repo, bool all)
{
	sqlite3_stmt *stmt = NULL;
	struct sbuf *sql = sbuf_new_auto();
	const char *reponame = NULL;
	struct pkgdb_it *it;

	if ((it = pkgdb_query_newpkgversion(db, repo)) != NULL) {
		pkg_emit_newpkgversion();
		return (it);
	}

	assert(db != NULL);
	assert(db->type == PKGDB_REMOTE);

	const char finalsql[] = "select pkgid as id, origin, name, version, "
		"comment, desc, message, arch, maintainer, "
		"www, prefix, flatsize, newversion, newflatsize, pkgsize, "
		"cksum, repopath, automatic, weight, "
		"'%s' AS dbname FROM pkgjobs order by weight DESC;";

	const char pkgjobs_sql_1[] = "INSERT OR IGNORE INTO pkgjobs (pkgid, origin, name, version, comment, desc, arch, "
			"maintainer, www, prefix, flatsize, pkgsize, "
			"cksum, repopath, automatic) "
			"SELECT id, origin, name, version, comment, desc, "
			"arch, maintainer, www, prefix, flatsize, pkgsize, "
			"cksum, path, 0 FROM '%s'.packages WHERE origin IN (select origin from main.packages)";

	const char pkgjobs_sql_2[] = "INSERT OR IGNORE INTO pkgjobs (pkgid, origin, name, version, comment, desc, arch, "
				"maintainer, www, prefix, flatsize, pkgsize, "
				"cksum, repopath, automatic) "
				"SELECT DISTINCT r.id, r.origin, r.name, r.version, r.comment, r.desc, "
				"r.arch, r.maintainer, r.www, r.prefix, r.flatsize, r.pkgsize, "
				"r.cksum, r.path, 1 "
				"FROM '%s'.packages AS r where r.origin IN "
				"(SELECT d.origin from '%s'.deps AS d, pkgjobs as j WHERE d.package_id = j.pkgid) "
				"AND (SELECT p.origin from main.packages as p WHERE p.origin=r.origin AND version=r.version) IS NULL;";

	const char *pkgjobs_sql_3;
	if (!all) {
		pkgjobs_sql_3 = "INSERT OR REPLACE INTO pkgjobs (pkgid, origin, name, version, comment, desc, message, arch, "
			"maintainer, www, prefix, flatsize, newversion, newflatsize, pkgsize, "
			"cksum, repopath, automatic) "
			"SELECT l.id, l.origin, l.name, l.version, l.comment, l.desc, l.message, l.arch, "
			"l.maintainer, l.www, l.prefix, l.flatsize, r.version AS newversion, "
			"r.flatsize AS newflatsize, r.pkgsize, r.cksum, r.repopath, r.automatic "
			"FROM main.packages AS l, pkgjobs AS r WHERE l.origin = r.origin "
			"AND (PKGLT(l.version, r.version) OR (l.name != r.name))";
	} else {
		pkgjobs_sql_3 = "INSERT OR REPLACE INTO pkgjobs (pkgid, origin, name, version, comment, desc, message, arch, "
			"maintainer, www, prefix, flatsize, newversion, newflatsize, pkgsize, "
			"cksum, repopath, automatic) "
			"SELECT l.id, l.origin, l.name, l.version, l.comment, l.desc, l.message, l.arch, "
			"l.maintainer, l.www, l.prefix, l.flatsize, r.version AS newversion, "
			"r.flatsize AS newflatsize, r.pkgsize, r.cksum, r.repopath, r.automatic "
			"FROM main.packages AS l, pkgjobs AS r WHERE l.origin = r.origin";
	}

	const char weight_sql[] = "UPDATE pkgjobs set weight=(select count(*) from '%s'.deps as d where d.origin=pkgjobs.origin)";

	if ((reponame = pkgdb_get_reponame(db, repo)) == NULL) {
		return (NULL);
	}

	create_temporary_pkgjobs(db->sqlite);

	sbuf_printf(sql, pkgjobs_sql_1, reponame);
	sbuf_finish(sql);
	sql_exec(db->sqlite, sbuf_get(sql));

	/* Remove packages already installed and in the latest version */
	if (!all)
		sql_exec(db->sqlite, "DELETE from pkgjobs where (select p.origin from main.packages as p where p.origin=pkgjobs.origin and version=pkgjobs.version) IS NOT NULL;");

	sbuf_reset(sql);
	sbuf_printf(sql, pkgjobs_sql_2, reponame, reponame);
	sbuf_finish(sql);

	do {
		sql_exec(db->sqlite, sbuf_get(sql));
	} while (sqlite3_changes(db->sqlite) != 0);

	/* Determine if there is an upgrade needed */
	sql_exec(db->sqlite, pkgjobs_sql_3);

	sbuf_reset(sql);
	sbuf_printf(sql, weight_sql, reponame);
	sbuf_finish(sql);

	sql_exec(db->sqlite, sbuf_get(sql));

	sql_exec(db->sqlite, "UPDATE pkgjobs set weight=100000 where origin=\"ports-mgmt/pkg\"");

	sbuf_reset(sql);
	sbuf_printf(sql, finalsql, reponame);
	sbuf_finish(sql);

	if (sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sbuf_delete(sql);

	return (pkgdb_it_new(db, stmt, PKG_REMOTE));
}

struct pkgdb_it *
pkgdb_query_downgrades(struct pkgdb *db, const char *repo)
{
	struct sbuf *sql = sbuf_new_auto();
	const char *reponame = NULL;
	sqlite3_stmt *stmt = NULL;

	assert(db != NULL);
	assert(db->type == PKGDB_REMOTE);

	const char finalsql[] = ""
		"SELECT l.id, l.origin AS origin, l.name AS name, l.version AS version, l.comment AS comment, l.desc AS desc, "
		"l.message AS message, l.arch AS arch, l.maintainer AS maintainer, "
		"l.www AS www, l.prefix AS prefix, l.flatsize AS flatsize, r.version AS version, r.flatsize AS newflatsize, "
		"r.pkgsize AS pkgsize, r.path AS repopath, '%s' AS dbname "
		"FROM main.packages AS l, "
		"'%s'.packages AS r "
		"WHERE l.origin = r.origin "
		"AND PKGGT(l.version, r.version)";

	if ((reponame = pkgdb_get_reponame(db, repo)) == NULL) {
		return (NULL);
	}

	sbuf_printf(sql, finalsql, reponame, reponame);
	sbuf_finish(sql);

	if (sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	return (pkgdb_it_new(db, stmt, PKG_REMOTE));
}

struct pkgdb_it *
pkgdb_query_autoremove(struct pkgdb *db)
{
	sqlite3_stmt *stmt = NULL;
	int weight = 0;

	assert(db != NULL);

	const char sql[] = ""
		"SELECT id, p.origin, name, version, comment, desc, "
		"message, arch, maintainer, www, prefix, "
		"flatsize FROM packages as p, autoremove where id = pkgid ORDER BY weight ASC;";

	sql_exec(db->sqlite, "DROP TABLE IF EXISTS autoremove; "
			"CREATE TEMPORARY TABLE IF NOT EXISTS autoremove ("
			"origin TEXT UNIQUE NOT NULL, pkgid INTEGER, weight INTEGER);");

	do {
		sql_exec(db->sqlite, "INSERT OR IGNORE into autoremove(origin, pkgid, weight) "
				"SELECT distinct origin, id, %d FROM packages WHERE automatic=1 AND "
				"origin NOT IN (SELECT DISTINCT deps.origin FROM deps WHERE "
				" deps.origin = packages.origin AND package_id NOT IN "
				" (select pkgid from autoremove));"
				, weight);
	} while (sqlite3_changes(db->sqlite) != 0);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	return (pkgdb_it_new(db, stmt, PKG_INSTALLED));
}

struct pkgdb_it *
pkgdb_query_delete(struct pkgdb *db, match_t match, int nbpkgs, char **pkgs, int recursive)
{
	sqlite3_stmt *stmt = NULL;

	struct sbuf *sql = sbuf_new_auto();
	const char *how = NULL;
	int i = 0;

	assert(db != NULL);

	const char sqlsel[] = ""
		"SELECT id, p.origin, name, version, comment, desc, "
		"message, arch, maintainer, www, prefix, "
		"flatsize, (select count(*) from deps AS d where d.origin=del.origin) as weight FROM packages as p, delete_job as del where id = pkgid "
		"ORDER BY weight ASC;";

	sbuf_cat(sql, "INSERT OR IGNORE INTO delete_job (origin, pkgid) "
			"SELECT p.origin, p.id FROM packages as p ");

	how = pkgdb_get_match_how(match);

	sql_exec(db->sqlite, "DROP TABLE IF EXISTS delete_job; "
			"CREATE TEMPORARY TABLE IF NOT EXISTS delete_job ("
			"origin TEXT UNIQUE NOT NULL, pkgid INTEGER);"
			);

	if (how != NULL) {
		sbuf_cat(sql, " WHERE ");
		sbuf_printf(sql, how, "p.name");
		sbuf_cat(sql, " OR ");
		sbuf_printf(sql, how, "p.origin");
		sbuf_cat(sql, " OR ");
		sbuf_printf(sql, how, "p.name || \"-\" || p.version");
		sbuf_finish(sql);

		for (i = 0; i < nbpkgs; i++) {
			if (sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL) != SQLITE_OK) {
				ERROR_SQLITE(db->sqlite);
				return (NULL);
			}
			sqlite3_bind_text(stmt, 1, pkgs[i], -1, SQLITE_STATIC);
			while (sqlite3_step(stmt) != SQLITE_DONE);
		}
	} else {
		sbuf_finish(sql);
		if (sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL) != SQLITE_OK) {
			ERROR_SQLITE(db->sqlite);
			return (NULL);
		}
		sqlite3_bind_text(stmt, 1, pkgs[i], -1, SQLITE_STATIC);
		while (sqlite3_step(stmt) != SQLITE_DONE);
	}

	sqlite3_finalize(stmt);

	if (recursive) {
		do {
			sql_exec(db->sqlite, "INSERT OR IGNORE INTO delete_job(origin, pkgid) "
					"SELECT p.origin, p.id FROM deps AS d, packages AS p, delete_job AS del WHERE "
					"d.origin=del.origin AND p.id = d.package_id");
		} while (sqlite3_changes(db->sqlite) != 0);
	}

	if (sqlite3_prepare_v2(db->sqlite, sqlsel, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sbuf_finish(sql);
	sbuf_delete(sql);

	return (pkgdb_it_new(db, stmt, PKG_INSTALLED));
}

struct pkgdb_it *
pkgdb_rquery(struct pkgdb *db, const char *pattern, match_t match, const char *repo)
{
	sqlite3_stmt *stmt = NULL;
	struct sbuf *sql = NULL;
	bool multirepos_enabled = false;
	const char *reponame = NULL;
	const char *comp = NULL;
	char basesql[BUFSIZ] = ""
				"SELECT id, origin, name, version, comment, "
				"prefix, desc, arch, maintainer, www, "
				"licenselogic, flatsize AS newflatsize, pkgsize, "
				"cksum, path AS repopath, '%s' AS dbname "
				"FROM '%s'.packages p";

	assert(db != NULL);
	assert(match == MATCH_ALL || (pattern != NULL && pattern[0] != '\0'));

	if ((reponame = pkgdb_get_reponame(db, repo)) == NULL) {
		return (NULL);
	}

	sql = sbuf_new_auto();
	comp = pkgdb_get_pattern_query(pattern, match);
	if (comp && comp[0]) {
		strlcat(basesql, comp, sizeof(basesql));
	}

	pkg_config_bool(PKG_CONFIG_MULTIREPOS, &multirepos_enabled);

	/*
	 * Working on multiple remote repositories
	 */
	if (multirepos_enabled && !strcmp(reponame, "default")) {
		/* duplicate the query via UNION for all the attached databases */
		if (sql_on_all_attached_db(db->sqlite, sql, basesql) != EPKG_OK)
			return (NULL);
	} else {
		sbuf_printf(sql, basesql, reponame, reponame);
	}

	sbuf_cat(sql, " ORDER BY name;");
	sbuf_finish(sql);

	if (sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sbuf_delete(sql);

	if (match != MATCH_ALL && match != MATCH_CONDITION)
		sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_REMOTE));
}


static int
pkgdb_search_build_search_query(struct sbuf *sql, match_t match, unsigned int field)
{
	const char *how = NULL;
	const char *what = NULL;

	how = pkgdb_get_match_how(match);

	switch(field) {
		case FIELD_NONE:
			what = NULL;
			break;
		case FIELD_ORIGIN:
			what = "origin";
			break;
		case FIELD_NAME:
			what = "name";
			break;
		case FIELD_NAMEVER:
			what = "name || \"-\" || version";
			break;
		case FIELD_COMMENT:
			what = "comment";
			break;
		case FIELD_DESC:
			what = "desc";
			break;
	}

	if (what != NULL && how != NULL)
		sbuf_printf(sql, how, what);

	return (EPKG_OK);
}

struct pkgdb_it *
pkgdb_search(struct pkgdb *db, const char *pattern, match_t match, unsigned int field, const char *reponame)
{
	sqlite3_stmt *stmt = NULL;
	struct sbuf *sql = NULL;
	bool multirepos_enabled = false;
	const char *basesql = ""
				"SELECT id, origin, name, version, comment, "
					"prefix, desc, arch, maintainer, www, "
					"licenselogic, flatsize AS newflatsize, pkgsize, "
					"cksum, path AS repopath ";
	const char *multireposql = ""
				"SELECT id, origin, name, version, comment, "
					"prefix, desc, arch, maintainer, www, "
					"licenselogic, flatsize, pkgsize, "
					"cksum, path, '%s' AS dbname "
					"FROM '%s'.packages ";

	assert(db != NULL);
	assert(pattern != NULL && pattern[0] != '\0');
	assert(db->type == PKGDB_REMOTE);


	sql = sbuf_new_auto();
	sbuf_cat(sql, basesql);

	pkg_config_bool(PKG_CONFIG_MULTIREPOS, &multirepos_enabled);

	if (multirepos_enabled) {
		/*
		 * Working on multiple remote repositories
		 */

		/* add the dbname column to the SELECT */
		sbuf_cat(sql, ", dbname FROM (");

		if (reponame != NULL) {
			if (is_attached(db->sqlite, reponame)) {
				sbuf_printf(sql, multireposql, reponame, reponame);
			} else {
				pkg_emit_error("Repository %s can't be loaded", reponame);
				return (NULL);
			}
		} else {
			/* test on all the attached databases */
			if (sql_on_all_attached_db(db->sqlite, sql, multireposql) != EPKG_OK)
				return (NULL);
		}

		/* close the UNIONs and build the search query */
		sbuf_cat(sql, ") WHERE ");
	} else {
		/*
		 * Working on a single remote repository
		 */

		sbuf_cat(sql, ", 'remote' AS dbname FROM remote.packages WHERE ");
	}

	pkgdb_search_build_search_query(sql, match, field);
	sbuf_cat(sql, ";");
	sbuf_finish(sql);

	if (sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sbuf_delete(sql);

	sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_REMOTE));
}

int
pkgdb_integrity_append(struct pkgdb *db, struct pkg *p)
{
	int ret = EPKG_OK;

	sqlite3_stmt *stmt = NULL;
	sqlite3_stmt *stmt_conflicts = NULL;
	struct pkg_file *file = NULL;
	struct sbuf *conflictmsg = sbuf_new_auto();

	const char sql[] = "INSERT INTO integritycheck (name, origin, version, path)"
		"values (?1, ?2, ?3, ?4);";
	const char sql_conflicts[] = "SELECT name, version from integritycheck where path=?1;";

	assert( db != NULL && p != NULL);

	sql_exec(db->sqlite, "CREATE TEMP TABLE IF NOT EXISTS integritycheck ( "
			"name TEXT, "
			"origin TEXT, "
			"version TEXT, "
			"path TEXT UNIQUE);"
		);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}
	while (pkg_files(p, &file) == EPKG_OK) {
		const char *name, *origin, *version;

		pkg_get(p, PKG_NAME, &name, PKG_ORIGIN, &origin, PKG_VERSION, &version);
		sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, origin, -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 3, version, -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 4, pkg_file_get(file, PKG_FILE_PATH), -1, SQLITE_STATIC);

		if (sqlite3_step(stmt) != SQLITE_DONE) {
			sbuf_clear(conflictmsg);
			sbuf_printf(conflictmsg, "WARNING: %s-%s conflict on %s with: \n",
			    name, version,
			    pkg_file_get(file, PKG_FILE_PATH));

			if (sqlite3_prepare_v2(db->sqlite, sql_conflicts, -1, &stmt_conflicts, NULL) != SQLITE_OK) {
				ERROR_SQLITE(db->sqlite);
				sqlite3_finalize(stmt);
				sbuf_delete(conflictmsg);
				return (EPKG_FATAL);
			}

			sqlite3_bind_text(stmt_conflicts, 1, pkg_file_get(file, PKG_FILE_PATH), -1, SQLITE_STATIC);

			while (sqlite3_step(stmt_conflicts) != SQLITE_DONE) {
				sbuf_printf(conflictmsg, "\t- %s-%s\n",
						sqlite3_column_text(stmt_conflicts, 0),
						sqlite3_column_text(stmt_conflicts, 1));
			}
			sqlite3_finalize(stmt_conflicts);
			sbuf_finish(conflictmsg);
			pkg_emit_error("%s", sbuf_get(conflictmsg));
			ret = EPKG_FATAL;
		}
		sqlite3_reset(stmt);
	}
	sqlite3_finalize(stmt);
	sbuf_delete(conflictmsg);

	return (ret);
}

int
pkgdb_integrity_check(struct pkgdb *db)
{
	int ret = EPKG_OK;
	sqlite3_stmt *stmt;
	sqlite3_stmt *stmt_conflicts;
	struct sbuf *conflictmsg = sbuf_new_auto();
	assert (db != NULL);

	const char sql_local_conflict[] = "SELECT p.name, p.version FROM packages AS p, files AS f "
		"WHERE p.id = f.package_id AND f.path = ?1;";

	const char sql_conflicts[] = "SELECT name, version from integritycheck where path=?1;";

	if (sqlite3_prepare_v2(db->sqlite,
		"SELECT path, COUNT(path) from ("
		"SELECT path from integritycheck UNION ALL "
		"SELECT path from files, main.packages AS p where p.id=package_id and p.origin NOT IN (SELECT origin from integritycheck)"
		") GROUP BY path HAVING (COUNT(path) > 1 );",
		-1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	while (sqlite3_step(stmt) != SQLITE_DONE) {
		sbuf_clear(conflictmsg);

		if (sqlite3_prepare_v2(db->sqlite, sql_local_conflict, -1, &stmt_conflicts, NULL) != SQLITE_OK) {
			ERROR_SQLITE(db->sqlite);
			sqlite3_finalize(stmt);
			sbuf_delete(conflictmsg);
			return (EPKG_FATAL);
		}

		sqlite3_bind_text(stmt_conflicts, 1, sqlite3_column_text(stmt, 0), -1, SQLITE_STATIC);

		sqlite3_step(stmt_conflicts);

		sbuf_printf(conflictmsg, "WARNING: locally installed %s-%s conflicts on %s with:\n",
				sqlite3_column_text(stmt_conflicts, 0),
				sqlite3_column_text(stmt_conflicts, 1),
				sqlite3_column_text(stmt, 0)
				);

		sqlite3_finalize(stmt_conflicts);

		if (sqlite3_prepare_v2(db->sqlite, sql_conflicts, -1, &stmt_conflicts, NULL) != SQLITE_OK) {
			ERROR_SQLITE(db->sqlite);
			sqlite3_finalize(stmt);
			sbuf_delete(conflictmsg);
			return (EPKG_FATAL);
		}

		sqlite3_bind_text(stmt_conflicts, 1, sqlite3_column_text(stmt, 0), -1, SQLITE_STATIC);

		while (sqlite3_step(stmt_conflicts) != SQLITE_DONE) {
			sbuf_printf(conflictmsg, "\t- %s-%s\n",
					sqlite3_column_text(stmt_conflicts, 0),
					sqlite3_column_text(stmt_conflicts, 1));
		}
		sqlite3_finalize(stmt_conflicts);
		sbuf_finish(conflictmsg);
		pkg_emit_error("%s", sbuf_get(conflictmsg));
		ret = EPKG_FATAL;
	}

	sqlite3_finalize(stmt);
	sbuf_delete(conflictmsg);

/*	sql_exec(db->sqlite, "DROP TABLE IF EXISTS integritycheck");*/

	return (ret);
}

struct pkgdb_it *
pkgdb_integrity_conflict_local(struct pkgdb *db, const char *origin)
{
	sqlite3_stmt *stmt;

	assert(db != NULL && origin != NULL);

	const char sql_conflicts [] = "SELECT DISTINCT p.id as rowid, p.origin, p.name, p.version, p.prefix "
		"FROM packages AS p, files AS f, integritycheck AS i "
		"WHERE p.id = f.package_id AND f.path = i.path AND i.origin = ?1";

	if (sqlite3_prepare_v2(db->sqlite, sql_conflicts, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sqlite3_bind_text(stmt, 1, origin, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_INSTALLED));
}

static int
pkgdb_vset(struct pkgdb *db, int64_t id, va_list ap)
{
	int attr;
	char sql[BUFSIZ];
	int automatic;
	char *oldorigin;
	char *neworigin;

	while ((attr = va_arg(ap, int)) > 0) {
		switch (attr) {
			case PKG_FLATSIZE:
				snprintf(sql, BUFSIZ, "update packages set flatsize=%"PRId64" where id=%"PRId64";",
				    va_arg(ap, int64_t), id);
				sql_exec(db->sqlite, sql);
				break;
			case PKG_AUTOMATIC:
				automatic = va_arg(ap, int);
				if (automatic != 0 && automatic != 1)
					continue;
				snprintf(sql, BUFSIZ, "update packages set automatic=%d where id=%"PRId64";", automatic, id);
				sql_exec(db->sqlite, sql);
				break;
			case PKG_DEP_ORIGIN:
				oldorigin = va_arg(ap, char *);
				neworigin = va_arg(ap, char *);
				sqlite3_snprintf(BUFSIZ, sql, "update deps set origin='%q', "
				    "name=(select name from packages where origin='%q'), "
				    "version=(select version from packages where origin='%q') "
				    "WHERE package_id=%d AND origin='%q';",
				    neworigin, neworigin, neworigin, id, oldorigin);
				sql_exec(db->sqlite, sql);
				break;
		}
	}
	return (EPKG_OK);
}

int
pkgdb_set2(struct pkgdb *db, struct pkg *pkg, ...)
{
	int ret = EPKG_OK;
	int64_t id;

	va_list ap;

	assert(pkg != NULL);

	va_start(ap, pkg);
	pkg_get(pkg, PKG_ROWID, &id);
	ret = pkgdb_vset(db, id, ap);
	va_end(ap);

	return (ret);
}

struct pkgdb_it *
pkgdb_query_fetch(struct pkgdb *db, match_t match, int nbpkgs, char **pkgs, const char *repo)
{
	sqlite3_stmt *stmt = NULL;
	int i = 0;
	struct sbuf *sql = sbuf_new_auto();
	const char *how = NULL;
	const char *reponame = NULL;

	const char finalsql[] = "SELECT pkgid AS id, origin, name, version, "
		"flatsize, newversion, newflatsize, pkgsize, cksum, repopath, "
		"'%s' AS dbname FROM pkgjobs ORDER BY weight DESC;";

	const char main_sql[] = "INSERT OR IGNORE INTO pkgjobs (pkgid, origin, name, version, "
			"flatsize, pkgsize, cksum, repopath) "
			"SELECT id, origin, name, version, flatsize, pkgsize, "
			"cksum, path FROM '%s'.packages ";

	const char deps_sql[] = "INSERT OR IGNORE INTO pkgjobs (pkgid, origin, name, version, "
				"flatsize, pkgsize, cksum, repopath) "
				"SELECT DISTINCT r.id, r.origin, r.name, r.version, "
				"r.flatsize, r.pkgsize, r.cksum, r.path "
				"FROM '%s'.packages AS r where r.origin IN "
				"(SELECT d.origin FROM '%s'.deps AS d, pkgjobs AS j WHERE d.package_id = j.pkgid) "
				"AND (SELECT origin FROM main.packages WHERE origin=r.origin AND version=r.version) IS NULL;";

	const char weight_sql[] = "UPDATE pkgjobs SET weight=(SELECT count(*) FROM '%s'.deps AS d WHERE d.origin=pkgjobs.origin)";

	assert(db != NULL);
	assert(db->type == PKGDB_REMOTE);

	if ((reponame = pkgdb_get_reponame(db, repo)) == NULL) {
		return (NULL);
	}

	sbuf_printf(sql, main_sql, reponame);

	how = pkgdb_get_match_how(match);

	create_temporary_pkgjobs(db->sqlite);

	if (how != NULL) {
		sbuf_cat(sql, " WHERE ");
		sbuf_printf(sql, how, "name");
		sbuf_cat(sql, " OR ");
		sbuf_printf(sql, how, "origin");
		sbuf_cat(sql, " OR ");
		sbuf_printf(sql, how, "name || \"-\" || version");
		sbuf_finish(sql);

		for (i = 0; i < nbpkgs; i++) {
			if (sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL) != SQLITE_OK) {
				ERROR_SQLITE(db->sqlite);
				return (NULL);
			}
			sqlite3_bind_text(stmt, 1, pkgs[i], -1, SQLITE_STATIC);
			while (sqlite3_step(stmt) != SQLITE_DONE);

			/* report if package was not found in the database */
			if (sqlite3_changes(db->sqlite) == 0)
				pkg_emit_error("Package '%s' was not found in the repositories", pkgs[i]);
		}
	} else {
		sbuf_finish(sql);
		if (sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL) != SQLITE_OK) {
			ERROR_SQLITE(db->sqlite);
			return (NULL);
		}
		sqlite3_bind_text(stmt, 1, pkgs[i], -1, SQLITE_STATIC);
		while (sqlite3_step(stmt) != SQLITE_DONE);
	}

	sqlite3_finalize(stmt);
	sbuf_clear(sql);

	/* Append dependencies */
	sbuf_reset(sql);
	sbuf_printf(sql, deps_sql, reponame, reponame);
	sbuf_finish(sql);

	do {
		sql_exec(db->sqlite, sbuf_get(sql));
	} while (sqlite3_changes(db->sqlite) != 0);

	sbuf_reset(sql);
	sbuf_printf(sql, weight_sql, reponame);
	sbuf_finish(sql);

	/* Set weight */
	sql_exec(db->sqlite, sbuf_get(sql));

	/* Execute final SQL */
	sbuf_reset(sql);
	sbuf_printf(sql, finalsql, reponame);
	sbuf_finish(sql);

	if (sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sbuf_finish(sql);
	sbuf_delete(sql);

	return (pkgdb_it_new(db, stmt, PKG_REMOTE));
}

/* create our custom functions in the sqlite3 connection. Used both in the shell and pkgdb_open */
static int
sqlcmd_init(sqlite3 *db, __unused const char **err, __unused const void *noused)
{
		sqlite3_create_function(db, "now", 0, SQLITE_ANY, NULL,
				pkgdb_now, NULL, NULL);
		sqlite3_create_function(db, "myarch", 0, SQLITE_ANY, NULL,
				pkgdb_myarch, NULL, NULL);
		sqlite3_create_function(db, "myarch", 1, SQLITE_ANY, NULL,
				pkgdb_myarch, NULL, NULL);
		sqlite3_create_function(db, "regexp", 2, SQLITE_ANY, NULL,
				pkgdb_regex_basic, NULL, NULL);
		sqlite3_create_function(db, "eregexp", 2, SQLITE_ANY, NULL,
				pkgdb_regex_extended, NULL, NULL);
		sqlite3_create_function(db, "pkglt", 2, SQLITE_ANY, NULL,
				pkgdb_pkglt, NULL, NULL);
		sqlite3_create_function(db, "pkggt", 2, SQLITE_ANY, NULL,
				pkgdb_pkggt, NULL, NULL);

		return SQLITE_OK;
}

void
pkgdb_cmd(int argc, char **argv)
{
	sqlite3_initialize();
	sqlite3_shell(argc, argv);
}

void
pkgshell_open(const char **reponame)
{
	char localpath[MAXPATHLEN + 1];
	const char *dbdir;

	sqlite3_auto_extension((void(*)(void))sqlcmd_init);

	if (pkg_config_string(PKG_CONFIG_DBDIR, &dbdir) != EPKG_OK)
		return;

	snprintf(localpath, sizeof(localpath), "%s/local.sqlite", dbdir);
	*reponame = strdup(localpath);
}

int
pkgdb_lock(struct pkgdb *db)
{
	return sql_exec(db->sqlite, "PRAGMA main.locking_mode=EXCLUSIVE;BEGIN IMMEDIATE;COMMIT;");
}

int
pkgdb_unlock(struct pkgdb *db)
{
	return sql_exec(db->sqlite, "PRAGMA main.locking_mode=NORMAL;BEGIN IMMEDIATE;COMMIT;");
}
