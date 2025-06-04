/*-
 * Copyright (c) 2011-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
 * Copyright (c) 2013 Gerald Pfeifer <gerald@pfeifer.com>
 * Copyright (c) 2013-2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2023 Serenity Cyber Security, LLC
 *                    Author: Gleb Popov <arrowd@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <bsd_compat.h>

#include <sys/param.h>
#include <sys/mount.h>

#include <assert.h>
#include <errno.h>
#include <regex.h>
#include <grp.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include <sqlite3.h>

#if defined(HAVE_SYS_STATVFS_H)
#include <sys/statvfs.h>
#endif

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/utils.h"
#include "private/pkg_deps.h"
#include "pkg/vec.h"

#include "private/db_upgrades.h"

extern struct pkg_ctx ctx;

#define dbg(x, ...) pkg_dbg(PKG_DBG_DB, x, __VA_ARGS__)

/* An application using a libpkg() DBVERSION is assumed to be compatible
   with:

   * Any lower schema version of the DB, by updating the schema to DBVERSION
   * Any equal schema version of the DB
   * Any greater schema version of the DB with the same DB_SCHEMA_MAJOR
     -- In general, it is OK to add new tables, but modifying or removing old
        tables must be avoided.  If necessary, this may be achieved by creating
	appropriate VIEWS and TRIGGERS to mimic the older structure.

   Anyone wishing to make a schema change that necessitates incrementing
   DB_SCHEMA_MAJOR must first present every other pkgng developer with one
   of the Golden Apples of the Hesperides
*/

#define DB_SCHEMA_MAJOR	0
#define DB_SCHEMA_MINOR	36

#define DBVERSION (DB_SCHEMA_MAJOR * 1000 + DB_SCHEMA_MINOR)

static int pkgdb_upgrade(struct pkgdb *);
static int prstmt_initialize(struct pkgdb *db);
/* static int run_prstmt(sql_prstmt_index s, ...); */
static void prstmt_finalize(struct pkgdb *db);
static int pkgdb_insert_scripts(struct pkg *pkg, int64_t package_id, sqlite3 *s);
static int pkgdb_insert_lua_scripts(struct pkg *pkg, int64_t package_id, sqlite3 *s);

extern int sqlite3_shell(int, char**);

struct sqlite3_stmt *
prepare_sql(sqlite3 *s, const char *sql)
{
	int ret;
	sqlite3_stmt *stmt;

	ret = sqlite3_prepare_v2(s, sql, strlen(sql), &stmt,
	    NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(s, sql);
		return (NULL);
	}
	return (stmt);
}

void
pkgdb_regex(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
	const unsigned char	*regex = NULL;
	const unsigned char	*str;
	regex_t			*re;
	int			 ret;

	if (argc != 2) {
		sqlite3_result_error(ctx, "SQL function regex() called "
		    "with invalid number of arguments.\n", -1);
		return;
	}
	if ((regex = sqlite3_value_text(argv[0])) == NULL) {
		sqlite3_result_error(ctx, "SQL function regex() called "
		    "without a regular expression.\n", -1);
		return;
	}

	re = (regex_t *)sqlite3_get_auxdata(ctx, 0);
	if (re == NULL) {
		int cflags;

		if (pkgdb_case_sensitive())
			cflags = REG_EXTENDED | REG_NOSUB;
		else
			cflags = REG_EXTENDED | REG_NOSUB | REG_ICASE;

		re = xmalloc(sizeof(regex_t));
		if (regcomp(re, regex, cflags) != 0) {
			sqlite3_result_error(ctx, "Invalid regex\n", -1);
			free(re);
			return;
		}

		sqlite3_set_auxdata(ctx, 0, re, pkgdb_regex_delete);
	}

	if ((str = sqlite3_value_text(argv[1])) != NULL) {
		ret = regexec(re, str, 0, NULL, 0);
		sqlite3_result_int(ctx, (ret != REG_NOMATCH));
	}
}

void
pkgdb_regex_delete(void *p)
{
	regex_t	*re = (regex_t *)p;

	regfree(re);
	free(re);
}

void
pkgdb_now(sqlite3_context *ctx, int argc, __unused sqlite3_value **argv)
{
	int64_t t = (int64_t)time(NULL);
	const char *date_env;

	if (argc != 0) {
		sqlite3_result_error(ctx, "Invalid usage of now() "
		    "no arguments expected\n", -1);
		return;
	}

	if ((date_env = getenv("PKG_INSTALL_EPOCH")) != NULL)
	{
		const char *errstr = NULL;
		int64_t temp_t = strtonum(date_env, 0, INT64_MAX, &errstr);
		if (errstr == NULL) {
			t = temp_t;
		}
	}

	sqlite3_result_int64(ctx, t);
}

static void
pkgdb_vercmp(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
	const char *op_str, *arg1, *arg2;
	enum pkg_dep_version_op op;
	int cmp;
	bool ret;

	if (argc != 3) {
		sqlite3_result_error(ctx, "Invalid usage of vercmp\n", -1);
		return;
	}

	op_str = sqlite3_value_text(argv[0]);
	arg1 = sqlite3_value_text(argv[1]);
	arg2 = sqlite3_value_text(argv[2]);

	if (op_str == NULL || arg1 == NULL || arg2 == NULL) {
		sqlite3_result_error(ctx, "Invalid usage of vercmp\n", -1);
		return;
	}

	op = pkg_deps_string_toop(op_str);
	cmp = pkg_version_cmp(arg1, arg2);

	switch(op) {
	case VERSION_ANY:
	default:
		ret = true;
		break;
	case VERSION_EQ:
		ret = (cmp == 0);
		break;
	case VERSION_GE:
		ret = (cmp >= 0);
		break;
	case VERSION_LE:
		ret = (cmp <= 0);
		break;
	case VERSION_GT:
		ret = (cmp > 0);
		break;
	case VERSION_LT:
		ret = (cmp < 0);
		break;
	case VERSION_NOT:
		ret = (cmp != 0);
		break;
	}

	sqlite3_result_int(ctx, ret);
}

static int
pkgdb_upgrade(struct pkgdb *db)
{
	int64_t		 db_version = -1;
	const char	*sql_upgrade;
	int		 i, ret;

	assert(db != NULL);

	ret = get_pragma(db->sqlite, "PRAGMA user_version;", &db_version, false);
	if (ret != EPKG_OK)
		return (EPKG_FATAL);

	if (db_version == DBVERSION)
		return (EPKG_OK);
	else if (db_version > DBVERSION) {
		if (db_version / 1000 <= DB_SCHEMA_MAJOR) {
			/* VIEWS and TRIGGERS used as compatibility hack */
			pkg_emit_error("warning: database version %" PRId64
			    " is newer than libpkg(3) version %d, but still "
			    "compatible", db_version, DBVERSION);
			return (EPKG_OK);
		} else {
			pkg_emit_error("database version %" PRId64 " is newer "
			    "than and incompatible with libpkg(3) version %d",
			    db_version, DBVERSION);
			return (EPKG_FATAL);
		}
	}

	while (db_version < DBVERSION) {
		const char *sql_str;
		if (sqlite3_db_readonly(db->sqlite, "main")) {
			pkg_emit_error("The database is outdated and "
			    "opened readonly");
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
		 * maybe because the current version is too old and upgrade
		 * support has been removed.
		 */
		if (sql_upgrade == NULL) {
			pkg_emit_error("can not upgrade to db version %" PRId64,
			    db_version);
			return (EPKG_FATAL);
		}

		if (pkgdb_transaction_begin_sqlite(db->sqlite, NULL) != EPKG_OK)
			return (EPKG_FATAL);

		if (sql_exec(db->sqlite, sql_upgrade) != EPKG_OK) {
			pkgdb_transaction_rollback_sqlite(db->sqlite, NULL);
			return (EPKG_FATAL);
		}

		sql_str = "PRAGMA user_version = %" PRId64 ";";
		ret = sql_exec(db->sqlite, sql_str, db_version);
		if (ret != EPKG_OK) {
			pkgdb_transaction_rollback_sqlite(db->sqlite, NULL);
			return (EPKG_FATAL);
		}

		if (pkgdb_transaction_commit_sqlite(db->sqlite, NULL) != EPKG_OK)
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
	const char	sql[] = ""
	"PRAGMA journal_mode = TRUNCATE;"
	"PRAGMA synchronous = FULL;"
	"BEGIN;"
	"CREATE TABLE packages ("
		"id INTEGER PRIMARY KEY,"
		"origin TEXT NOT NULL,"
		"name TEXT NOT NULL,"
		"version TEXT NOT NULL,"
		"comment TEXT NOT NULL,"
		"desc TEXT NOT NULL,"
		"mtree_id INTEGER, "
		"message TEXT,"
		"arch TEXT NOT NULL,"
		"maintainer TEXT NOT NULL, "
		"www TEXT,"
		"prefix TEXT NOT NULL,"
		"flatsize INTEGER NOT NULL,"
		"automatic INTEGER NOT NULL,"
		"locked INTEGER NOT NULL DEFAULT 0,"
		"licenselogic INTEGER NOT NULL,"
		"time INTEGER, "
		"manifestdigest TEXT NULL, "
		"pkg_format_version INTEGER,"
		"dep_formula TEXT NULL"
		",vital INTEGER NOT NULL DEFAULT 0"
	");"
	"CREATE UNIQUE INDEX packages_unique ON packages(name);"
	"CREATE TABLE pkg_script ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"type INTEGER,"
		"script_id INTEGER REFERENCES script(script_id)"
                        " ON DELETE RESTRICT ON UPDATE CASCADE,"
		"PRIMARY KEY (package_id, type)"
	");"
        "CREATE TABLE script ("
                "script_id INTEGER PRIMARY KEY,"
                "script TEXT NOT NULL UNIQUE"
        ");"
	"CREATE TABLE option ("
		"option_id INTEGER PRIMARY KEY,"
		"option TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE option_desc ("
		"option_desc_id INTEGER PRIMARY KEY,"
		"option_desc TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE pkg_option ("
		"package_id INTEGER NOT NULL REFERENCES packages(id) "
			"ON DELETE CASCADE ON UPDATE CASCADE,"
		"option_id INTEGER NOT NULL REFERENCES option(option_id) "
			"ON DELETE RESTRICT ON UPDATE CASCADE,"
		"value TEXT NOT NULL,"
		"PRIMARY KEY(package_id, option_id)"
	");"
	"CREATE TABLE pkg_option_desc ("
		"package_id INTEGER NOT NULL REFERENCES packages(id) "
			"ON DELETE CASCADE ON UPDATE CASCADE,"
		"option_id INTEGER NOT NULL REFERENCES option(option_id) "
			"ON DELETE RESTRICT ON UPDATE CASCADE,"
		"option_desc_id INTEGER NOT NULL "
			"REFERENCES option_desc(option_desc_id) "
			"ON DELETE RESTRICT ON UPDATE CASCADE,"
		"PRIMARY KEY(package_id, option_id)"
	");"
	"CREATE TABLE pkg_option_default ("
		"package_id INTEGER NOT NULL REFERENCES packages(id) "
			"ON DELETE CASCADE ON UPDATE CASCADE,"
		"option_id INTEGER NOT NULL REFERENCES option(option_id) "
			"ON DELETE RESTRICT ON UPDATE CASCADE,"
		"default_value TEXT NOT NULL,"
		"PRIMARY KEY(package_id, option_id)"
	");"
	"CREATE TABLE deps ("
		"origin TEXT NOT NULL,"
		"name TEXT NOT NULL,"
		"version TEXT NOT NULL,"
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE"
	");"
	"CREATE UNIQUE INDEX deps_unique ON deps(name, version, package_id);"
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
	"CREATE TABLE pkg_shlibs_required ("
		"package_id INTEGER NOT NULL REFERENCES packages(id)"
			" ON DELETE CASCADE ON UPDATE CASCADE,"
		"shlib_id INTEGER NOT NULL REFERENCES shlibs(id)"
			" ON DELETE RESTRICT ON UPDATE RESTRICT,"
		"UNIQUE (package_id, shlib_id)"
	");"
	"CREATE TABLE pkg_shlibs_provided ("
		"package_id INTEGER NOT NULL REFERENCES packages(id)"
			" ON DELETE CASCADE ON UPDATE CASCADE,"
		"shlib_id INTEGER NOT NULL REFERENCES shlibs(id)"
			" ON DELETE RESTRICT ON UPDATE RESTRICT,"
		"UNIQUE (package_id, shlib_id)"
	");"
	"CREATE TABLE annotation ("
                "annotation_id INTEGER PRIMARY KEY,"
                "annotation TEXT NOT NULL UNIQUE"
        ");"
        "CREATE TABLE pkg_annotation ("
                "package_id INTEGER REFERENCES packages(id)"
                      " ON DELETE CASCADE ON UPDATE RESTRICT,"
                "tag_id INTEGER NOT NULL REFERENCES annotation(annotation_id)"
                      " ON DELETE CASCADE ON UPDATE RESTRICT,"
		"value_id INTEGER NOT NULL REFERENCES annotation(annotation_id)"
		      " ON DELETE CASCADE ON UPDATE RESTRICT,"
		"UNIQUE (package_id, tag_id)"
	");"
	"CREATE TABLE pkg_conflicts ("
	    "package_id INTEGER NOT NULL REFERENCES packages(id)"
	    "  ON DELETE CASCADE ON UPDATE CASCADE,"
	    "conflict_id INTEGER NOT NULL,"
	    "UNIQUE(package_id, conflict_id)"
	");"
	"CREATE TABLE pkg_lock ("
	    "exclusive INTEGER(1),"
	    "advisory INTEGER(1),"
	    "read INTEGER(8)"
	");"
	"CREATE TABLE pkg_lock_pid ("
	    "pid INTEGER PRIMARY KEY"
	");"
	"INSERT INTO pkg_lock VALUES(0,0,0);"
	"CREATE TABLE provides("
	"    id INTEGER PRIMARY KEY,"
	"    provide TEXT NOT NULL"
	");"
	"CREATE TABLE pkg_provides ("
	    "package_id INTEGER NOT NULL REFERENCES packages(id)"
	    "  ON DELETE CASCADE ON UPDATE CASCADE,"
	    "provide_id INTEGER NOT NULL REFERENCES provides(id)"
	    "  ON DELETE RESTRICT ON UPDATE RESTRICT,"
	    "UNIQUE(package_id, provide_id)"
	");"
	"CREATE TABLE config_files ("
		"path TEXT NOT NULL UNIQUE, "
		"content TEXT, "
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE"
	");"

	/* Mark the end of the array */

	"CREATE INDEX deporigini on deps(origin);"
	"CREATE INDEX pkg_script_package_id ON pkg_script(package_id);"
	"CREATE INDEX deps_package_id ON deps (package_id);"
	"CREATE INDEX files_package_id ON files (package_id);"
	"CREATE INDEX pkg_directories_package_id ON pkg_directories (package_id);"
	"CREATE INDEX pkg_categories_package_id ON pkg_categories (package_id);"
	"CREATE INDEX pkg_licenses_package_id ON pkg_licenses (package_id);"
	"CREATE INDEX pkg_users_package_id ON pkg_users (package_id);"
	"CREATE INDEX pkg_groups_package_id ON pkg_groups (package_id);"
	"CREATE INDEX pkg_shlibs_required_package_id ON pkg_shlibs_required (package_id);"
	"CREATE INDEX pkg_shlibs_provided_package_id ON pkg_shlibs_provided (package_id);"
	"CREATE INDEX pkg_directories_directory_id ON pkg_directories (directory_id);"
	"CREATE INDEX pkg_annotation_package_id ON pkg_annotation(package_id);"
	"CREATE INDEX pkg_digest_id ON packages(origin, manifestdigest);"
	"CREATE INDEX pkg_conflicts_pid ON pkg_conflicts(package_id);"
	"CREATE INDEX pkg_conflicts_cid ON pkg_conflicts(conflict_id);"
	"CREATE INDEX pkg_provides_id ON pkg_provides(package_id);"
	"CREATE INDEX packages_origin ON packages(origin COLLATE NOCASE);"
	"CREATE INDEX packages_name ON packages(name COLLATE NOCASE);"
	"CREATE TABLE requires("
	"    id INTEGER PRIMARY KEY,"
	"    require TEXT NOT NULL"
	");"
	"CREATE TABLE pkg_requires ("
	    "package_id INTEGER NOT NULL REFERENCES packages(id)"
	    "  ON DELETE CASCADE ON UPDATE CASCADE,"
	    "require_id INTEGER NOT NULL REFERENCES requires(id)"
	    "  ON DELETE RESTRICT ON UPDATE RESTRICT,"
	    "UNIQUE(package_id, require_id)"
	");"
	"CREATE TABLE lua_script("
	"    lua_script_id INTEGER PRIMARY KEY,"
	"    lua_script TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE pkg_lua_script ("
		"package_id INTEGER NOT NULL REFERENCES packages(id)"
		"  ON DELETE CASCADE ON UPDATE CASCADE,"
		"lua_script_id INTEGER NOT NULL REFERENCES lua_script(lua_script_id)"
		"  ON DELETE RESTRICT ON UPDATE RESTRICT,"
		"type INTEGER,"
		"UNIQUE(package_id, lua_script_id)"
	");"
	"PRAGMA user_version = %d;"
	"COMMIT;"
	;

	return (sql_exec(sdb, sql, DBVERSION));
}

static int
pkgdb_is_insecure_mode(int dbdirfd, const char *path, bool install_as_user)
{
	uid_t		fileowner;
	gid_t		filegroup;
	bool		bad_perms = false;
	bool		wrong_owner = false;
	struct stat	sb;

	if (dbdirfd == -1)
		return (EPKG_ENODB);

	if (install_as_user) {
		fileowner = geteuid();
		filegroup = getegid();
	} else {
		fileowner = 0;
		filegroup = 0;
	}

	if (fstatat(dbdirfd, path, &sb, 0) != 0) {
		if (errno == EACCES)
			return (EPKG_ENOACCESS);
		else if (errno == ENOENT)
			return (EPKG_ENODB);
		else
			return (EPKG_FATAL);
	}

	/* if fileowner == 0, root ownership and no group or other
	   read access.  if fileowner != 0, require no other read
	   access and group read access IFF the group ownership ==
	   filegroup */

	if ( fileowner == 0 ) {
		if ((sb.st_mode & (S_IWGRP|S_IWOTH)) != 0)
			bad_perms = true;
		if (sb.st_uid != fileowner)
			wrong_owner = true;
	} else {
		if ((sb.st_mode & S_IWOTH) != 0)
			bad_perms = true;
		if (sb.st_gid != filegroup && (sb.st_mode & S_IWGRP) != 0)
			bad_perms = true;
		if (sb.st_uid != 0 && sb.st_uid != fileowner && sb.st_gid != filegroup)
			wrong_owner = true;
	}

	if (bad_perms) {
		pkg_emit_error("%s permissions (%#o) too lax", path,
			       (sb.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO)));
		return (EPKG_INSECURE);
	}
	if (wrong_owner) {
		pkg_emit_error("%s wrong user or group ownership"
			       " (expected %d/%d versus actual %d/%d)",
			       path, fileowner, filegroup, sb.st_uid, sb.st_gid);
		return (EPKG_INSECURE);
	}

	return (EPKG_OK);
}

int
pkgdb_check_access(unsigned mode, const char *dbname)
{
	const char *dbpath = ".";
	int retval;
	bool database_exists;
	bool install_as_user;
	int dbdirfd = pkg_get_dbdirfd();

	if (dbname != NULL)
		dbpath = dbname;

	install_as_user = (getenv("INSTALL_AS_USER") != NULL);

	retval = pkgdb_is_insecure_mode(dbdirfd, dbpath, install_as_user);

	database_exists = (retval != EPKG_ENODB);

	if (database_exists && retval != EPKG_OK)
		return (retval);

	if (!database_exists && (mode & PKGDB_MODE_CREATE) != 0)
		return (EPKG_OK);

	retval = -1;
	switch(mode & (PKGDB_MODE_READ|PKGDB_MODE_WRITE)) {
	case 0:		/* Existence test */
		if (dbdirfd == -1)
			goto out;
		retval = faccessat(dbdirfd, dbpath, F_OK, AT_EACCESS);
		break;
	case PKGDB_MODE_READ:
		if (dbdirfd == -1)
			goto out;
		retval = faccessat(dbdirfd, dbpath, R_OK, AT_EACCESS);
		break;
	case PKGDB_MODE_WRITE:
		if (dbdirfd == -1) {
			pkg_mkdirs(ctx.dbdir);
			dbdirfd = pkg_get_dbdirfd();
			if (dbdirfd == -1)
				goto out;
		}
		retval = faccessat(dbdirfd, dbpath, W_OK, AT_EACCESS);
		break;
	case PKGDB_MODE_READ|PKGDB_MODE_WRITE:
		if (dbdirfd == -1) {
			pkg_mkdirs(ctx.dbdir);
			dbdirfd = pkg_get_dbdirfd();
			if (dbdirfd == -1)
				goto out;
		}
		retval = faccessat(dbdirfd, dbpath, R_OK|W_OK, AT_EACCESS);
		break;
	}

out:
	if (retval != 0) {
		if (errno == ENOENT)
			return (EPKG_ENODB);
		else if (errno == EACCES || errno == EROFS)
			return (EPKG_ENOACCESS);
		else
			return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

int
pkgdb_access(unsigned mode, unsigned database)
{

	return (pkgdb_access2(mode, database, NULL));
}

int
pkgdb_access2(unsigned mode, unsigned database, c_charv_t *dbs)
{
	int			 retval = EPKG_OK;

	/*
	 * This will return one of:
	 *
	 * EPKG_ENODB:  a database doesn't exist and we don't want to create
	 *             it, or dbdir doesn't exist
	 *
	 * EPKG_INSECURE: the dbfile or one of the directories in the
	 *	       path to it are writable by other than root or
	 *             (if $INSTALL_AS_USER is set) the current euid
	 *             and egid
	 *
	 * EPKG_ENOACCESS: we don't have privileges to read or write
	 *
	 * EPKG_FATAL: Couldn't determine the answer for other reason,
	 *     like configuration screwed up, invalid argument values,
	 *     read-only filesystem, etc.
	 *
	 * EPKG_OK: We can go ahead
	 */


	if ((mode & ~(PKGDB_MODE_READ|PKGDB_MODE_WRITE|PKGDB_MODE_CREATE))
	    != 0)
		return (EPKG_FATAL); /* EINVAL */

	if ((database & ~(PKGDB_DB_LOCAL|PKGDB_DB_REPO)) != 0)
		return (EPKG_FATAL); /* EINVAL */

	/* Test the enclosing directory: if we're going to create the
	   DB, then we need read and write permissions on the dir.
	   Otherwise, just test for read access */

	if ((mode & PKGDB_MODE_CREATE) != 0) {
		retval = pkgdb_check_access(PKGDB_MODE_READ|PKGDB_MODE_WRITE,
		    NULL);
	} else
		retval = pkgdb_check_access(PKGDB_MODE_READ, NULL);
	if (retval != EPKG_OK)
		return (retval);

	/* Test local.sqlite, if required */

	if ((database & PKGDB_DB_LOCAL) != 0) {
		retval = pkgdb_check_access(mode, "local.sqlite");
		if (retval != EPKG_OK)
			return (retval);
	}

	if ((database & PKGDB_DB_REPO) != 0) {
		struct pkg_repo	*r = NULL;

		while (pkg_repos(&r) == EPKG_OK) {
			/* Ignore any repos marked as inactive */
			if (!pkg_repo_enabled(r))
				continue;

			if (dbs != NULL && dbs->len > 0 && r->name &&
			    !c_charv_contains(dbs, r->name, true)) {
				/* Skip what is not needed */
				continue;
			}

			retval = r->ops->access(r, mode);
			if (retval != EPKG_OK) {
				if (retval == EPKG_ENODB &&
				    (mode & PKGDB_MODE_READ) != PKGDB_MODE_READ) {
					pkg_emit_error("Repository %s missing."
						       " 'pkg update' required",
					    r->name);
				}

				return (retval);
			}
		}
	}
	return (retval);
}

static int
pkgdb_profile_callback(unsigned type __unused, void *ud __unused,
    void *stmt, void *X)
{
	sqlite3_uint64 nsec = *((sqlite3_uint64*)X);
	const char *req = sqlite3_sql((sqlite3_stmt *)stmt);
	/* According to sqlite3 documentation, nsec has milliseconds accuracy */
	nsec /= 1000000LLU;
	if (nsec > 0)
		dbg(1, "Sqlite request %s was executed in %lu milliseconds",
			req, (unsigned long)nsec);
	return (0);
}

int
pkgdb_open(struct pkgdb **db_p, pkgdb_t type)
{
	return (pkgdb_open_all(db_p, type, NULL));
}

/* the higher the better */
static int
repos_prio_cmp(const void *a, const void *b)
{
	struct pkg_repo *ra = *(struct pkg_repo **)a;
	struct pkg_repo *rb = *(struct pkg_repo **)b;

	return ((ra->priority < rb->priority) - (ra->priority > rb->priority));
}

static int
pkgdb_open_repos(struct pkgdb *db, const char *reponame)
{
	struct pkg_repo *r = NULL;

	while (pkg_repos(&r) == EPKG_OK) {
		if (!r->enable && reponame == NULL) {
			continue;
		}

		if (reponame == NULL || STRIEQ(r->name, reponame)) {
			/* We need read only access here */
			if (r->ops->open(r, R_OK) == EPKG_OK) {
				r->ops->init(r);
				vec_push(&db->repos, r);
			} else
				pkg_emit_error("Repository %s cannot be opened."
				    " 'pkg update' required", r->name);
		}
	}
	qsort(db->repos.d, db->repos.len, sizeof(db->repos.d[0]), repos_prio_cmp);

	return (EPKG_OK);
}

static const char*
_dbdir_trim_path(const char*path)
{
	static size_t l = 0;
	const char *p;

	if (l == 0)
		l = strlen(ctx.dbdir);

	if (strncmp(ctx.dbdir, path, l) == 0) {
		p = path + l;
		while (*p == '/')
			p++;
		return (p);
	}
	if (*path == '/')
		return (path + 1);
	return (path);
}

static int
_dbdir_open(const char *path, int flags, int mode)
{
	int dfd = pkg_get_dbdirfd();

	return (openat(dfd, _dbdir_trim_path(path), flags, mode));
}

static int
_dbdir_access(const char *path, int mode)
{
	int dfd = pkg_get_dbdirfd();

	return (faccessat(dfd, _dbdir_trim_path(path), mode, 0));
}

static int
_dbdir_stat(const char * path, struct stat * sb)
{
	int dfd = pkg_get_dbdirfd();

	return (fstatat(dfd, _dbdir_trim_path(path), sb, 0));
}

static int
_dbdir_lstat(const char * path, struct stat * sb)
{
	int dfd = pkg_get_dbdirfd();

	return (fstatat(dfd, _dbdir_trim_path(path), sb, AT_SYMLINK_NOFOLLOW));
}

static int
_dbdir_unlink(const char *path)
{
	int dfd = pkg_get_dbdirfd();

	return (unlinkat(dfd, _dbdir_trim_path(path), 0));
}

static int
_dbdir_mkdir(const char *path, mode_t mode)
{
	int dfd = pkg_get_dbdirfd();

	return (mkdirat(dfd, _dbdir_trim_path(path), mode));
}

static char *
_dbdir_getcwd(char *path, size_t sz)
{
	if (0 == sz) {
		errno = EINVAL;
	} else 	if (sz >= 2) {
		path[0] = '/';
		path[1] = '\0';
		return path;
	} else {
		errno = ERANGE;
	}
	return 0;
}

void
pkgdb_syscall_overload(void)
{
	sqlite3_vfs	*vfs;

	vfs = sqlite3_vfs_find(NULL);
	vfs->xSetSystemCall(vfs, "open", (sqlite3_syscall_ptr)_dbdir_open);
	vfs->xSetSystemCall(vfs, "access", (sqlite3_syscall_ptr)_dbdir_access);
	vfs->xSetSystemCall(vfs, "stat", (sqlite3_syscall_ptr)_dbdir_stat);
	vfs->xSetSystemCall(vfs, "lstat", (sqlite3_syscall_ptr)_dbdir_lstat);
	vfs->xSetSystemCall(vfs, "unlink", (sqlite3_syscall_ptr)_dbdir_unlink);
	vfs->xSetSystemCall(vfs, "mkdir", (sqlite3_syscall_ptr)_dbdir_mkdir);
	vfs->xSetSystemCall(vfs, "getcwd", (sqlite3_syscall_ptr)_dbdir_getcwd);
}

void
pkgdb_nfs_corruption(sqlite3 *db)
{

	if (sqlite3_errcode(db) != SQLITE_CORRUPT)
		return;

	/*
	 * Fall back on unix-dotfile locking strategy if on a network filesystem
	 */

#if defined(HAVE_SYS_STATVFS_H) && defined(ST_LOCAL)
	int dbdirfd = pkg_get_dbdirfd();
	struct statvfs stfs;

	if (fstatvfs(dbdirfd, &stfs) == 0) {
		if ((stfs.f_flag & ST_LOCAL) != ST_LOCAL)
			pkg_emit_error("You are running on a remote filesystem,"
			    " please make sure, the locking mechanism is "
			    " properly setup\n");
	}
#elif defined(HAVE_FSTATFS) && defined(MNT_LOCAL)
	int dbdirfd = pkg_get_dbdirfd();
	struct statfs stfs;

	if (fstatfs(dbdirfd, &stfs) == 0) {
		if ((stfs.f_flags & MNT_LOCAL) != MNT_LOCAL)
			pkg_emit_error("You are running on a remote filesystem,"
			    " please make sure, the locking mechanism is "
			    " properly setup\n");
	}
#endif

}

int
pkgdb_open_all(struct pkgdb **db_p, pkgdb_t type, const char *reponame)
{
	c_charv_t r = vec_init();
	int ret;

	if (reponame != NULL)
		vec_push(&r, reponame);

	ret = pkgdb_open_all2(db_p, type, &r);
	vec_free(&r);
	return (ret);
}
int
pkgdb_open_all2(struct pkgdb **db_p, pkgdb_t type, c_charv_t *reponames)
{
	struct pkgdb	*db = NULL;
	bool		 reopen = false;
	bool		 profile = false;
	bool		 create = false;
	int		 ret;
	int		 dbdirfd;

	if (*db_p != NULL) {
		reopen = true;
		db = *db_p;
	}

	if (!reopen)
		db = xcalloc(1, sizeof(struct pkgdb));
	db->prstmt_initialized = false;

	if (!reopen) {
retry:
		dbdirfd = pkg_get_dbdirfd();
		if (dbdirfd == -1) {
			if (errno == ENOENT) {
				if (pkg_mkdirs(ctx.dbdir) != EPKG_OK) {
					pkgdb_close(db);
					return (EPKG_FATAL);
				}
				goto retry;
			}
		}
		if (faccessat(dbdirfd, "local.sqlite", R_OK, AT_EACCESS) != 0) {
			if (errno != ENOENT) {
				pkg_emit_nolocaldb();
				pkgdb_close(db);
				return (EPKG_ENODB);
			} else if ((faccessat(dbdirfd, ".", W_OK, AT_EACCESS) != 0)) {
				/*
				 * If we need to create the db but cannot
				 * write to it, fail early
				 */
				pkg_emit_nolocaldb();
				pkgdb_close(db);
				return (EPKG_ENODB);
			} else {
				create = true;
			}
		}

		sqlite3_initialize();

		pkgdb_syscall_overload();

		if (sqlite3_open("/local.sqlite", &db->sqlite) != SQLITE_OK) {
			ERROR_SQLITE(db->sqlite, "sqlite open");
			pkgdb_nfs_corruption(db->sqlite);
			pkgdb_close(db);
			return (EPKG_FATAL);
		}

		/* Wait up to 5 seconds if database is busy */
		sqlite3_busy_timeout(db->sqlite, 5000);

		/* If the database is missing we have to initialize it */
		if (create && pkgdb_init(db->sqlite) != EPKG_OK) {
			pkgdb_close(db);
			return (EPKG_FATAL);
		}

		/* Create our functions */
		pkgdb_sqlcmd_init(db->sqlite, NULL, NULL);

		if (pkgdb_upgrade(db) != EPKG_OK) {
			pkgdb_close(db);
			return (EPKG_FATAL);
		}

		/*
		 * allow foreign key option which will allow to have
		 * clean support for reinstalling
		 */
		ret = sql_exec(db->sqlite, "PRAGMA foreign_keys = ON;");
		if (ret != EPKG_OK) {
			pkgdb_close(db);
			return (EPKG_FATAL);
		}
		sql_exec(db->sqlite, "PRAGMA mmap_size=268435456;");
	}

	if (type == PKGDB_REMOTE || type == PKGDB_MAYBE_REMOTE) {
		if (pkg_repos_activated_count() > 0) {
			if (reponames == NULL || reponames->len == 0) {
				ret = pkgdb_open_repos(db, NULL);
			} else {
				for (size_t i = 0; i < reponames->len; i++)
					ret = pkgdb_open_repos(db, reponames->d[i]);
			}
			if (ret != EPKG_OK) {
				pkgdb_close(db);
				return (ret);
			}
		} else if (type == PKGDB_REMOTE) {
			if (*db_p == NULL)
				pkgdb_close(db);
			pkg_emit_error("No active remote repositories configured");
			return (EPKG_FATAL);
		}
	}

	if (prstmt_initialize(db) != EPKG_OK) {
		pkgdb_close(db);
		return (EPKG_FATAL);
	}


	profile = pkg_object_bool(pkg_config_get("SQLITE_PROFILE"));
	if (profile) {
		dbg(1, "pkgdb profiling is enabled");
		sqlite3_trace_v2(db->sqlite, SQLITE_TRACE_PROFILE,
		    pkgdb_profile_callback, NULL);
	}

	*db_p = db;
	return (EPKG_OK);
}

static void
pkgdb_free_repo(struct pkg_repo *repo)
{
	repo->ops->close(repo, false);
}

void
pkgdb_close(struct pkgdb *db)
{
	if (db == NULL)
		return;

	if (db->prstmt_initialized)
		prstmt_finalize(db);

	if (db->sqlite != NULL) {

		vec_free_and_free(&db->repos, pkgdb_free_repo);

		if (!sqlite3_db_readonly(db->sqlite, "main"))
			pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PKGDB_CLOSE_RW, NULL, db);

		if (sqlite3_close(db->sqlite) != SQLITE_OK)
			pkg_emit_error("Package database is busy while closing!");
	}

	sqlite3_shutdown();
	free(db);
}

/* How many times to try COMMIT or ROLLBACK if the DB is busy */
#define BUSY_RETRIES	6
#define BUSY_SLEEP	200

/* This is a MACRO instead of a function as any sqlite3_* function that
 * queries the DB can return SQLITE_BUSY. We would need a function to
 * wrap all sqlite3_* API since we cannot pass anonymous functions/blocks
 * in C. This can be used to wrap existing code. */
#define PKGDB_SQLITE_RETRY_ON_BUSY(ret) 				\
	ret = SQLITE_BUSY;						\
	for (int _sqlite_busy_retries = 0;				\
	    _sqlite_busy_retries < BUSY_RETRIES && ret == SQLITE_BUSY; 	\
	    ++_sqlite_busy_retries, ret == SQLITE_BUSY && 		\
	    sqlite3_sleep(BUSY_SLEEP))

static int
run_transaction(sqlite3 *sqlite, const char *query, const char *savepoint)
{
	int		 ret = SQLITE_OK;
	sqlite3_stmt	*stmt;
	char *sql = NULL;

	assert(sqlite != NULL);

	xasprintf(&sql, "%s %s", query, savepoint != NULL ? savepoint : "");
	stmt = prepare_sql(sqlite, sql);
	if (stmt == NULL)
		return (EPKG_FATAL);
	pkgdb_debug(4, stmt);

	PKGDB_SQLITE_RETRY_ON_BUSY(ret)
		ret = sqlite3_step(stmt);

	if (ret != SQLITE_OK && ret != SQLITE_DONE) {
		ERROR_STMT_SQLITE(sqlite, stmt);
	}
	sqlite3_finalize(stmt);
	free(sql);
	return (ret == SQLITE_OK || ret == SQLITE_DONE ? EPKG_OK : EPKG_FATAL);
}

int
pkgdb_transaction_begin_sqlite(sqlite3 *sqlite, const char *savepoint)
{

	if (savepoint == NULL || savepoint[0] == '\0') {
		return (run_transaction(sqlite, "BEGIN IMMEDIATE TRANSACTION",
		    NULL));
	}
	return (run_transaction(sqlite, "SAVEPOINT", savepoint));
}

int
pkgdb_transaction_commit_sqlite(sqlite3 *sqlite, const char *savepoint)
{

	if (savepoint == NULL || savepoint[0] == '\0') {
		return (run_transaction(sqlite, "COMMIT TRANSACTION", NULL));
	}
	return (run_transaction(sqlite, "RELEASE SAVEPOINT", savepoint));
}

int
pkgdb_transaction_rollback_sqlite(sqlite3 *sqlite, const char *savepoint)
{

	if (savepoint == NULL || savepoint[0] == '\0') {
		return (run_transaction(sqlite, "ROLLBACK TRANSACTION", NULL));
	}
	return (run_transaction(sqlite, "ROLLBACK TO SAVEPOINT", savepoint));
}

/*
 * Public API
 */
int
pkgdb_transaction_begin(struct pkgdb *db, const char *savepoint)
{
	dbg(2, "new transaction");
	return (pkgdb_transaction_begin_sqlite(db->sqlite, savepoint));
}
int
pkgdb_transaction_commit(struct pkgdb *db, const char *savepoint)
{
	dbg(2, "end transaction");
	return (pkgdb_transaction_commit_sqlite(db->sqlite, savepoint));
}
int
pkgdb_transaction_rollback(struct pkgdb *db, const char *savepoint)
{
	dbg(2, "end transaction");
	return (pkgdb_transaction_rollback_sqlite(db->sqlite, savepoint));
}


/* By default, MATCH_EXACT and MATCH_REGEX are case sensitive.  This
 * is modified in many actions according to the value of
 * CASE_SENSITIVE_MATCH in pkg.conf and then possbily reset again in
 * pkg search et al according to command line flags */

static bool _case_sensitive_flag = false;

void
pkgdb_set_case_sensitivity(bool case_sensitive)
{
	_case_sensitive_flag = case_sensitive;
	return;
}

bool
pkgdb_case_sensitive(void)
{
	return (_case_sensitive_flag);
}

typedef enum _sql_prstmt_index {
	MTREE = 0,
	PKG,
	DEPS_UPDATE,
	DEPS,
	FILES,
	FILES_REPLACE,
	DIRS1,
	DIRS2,
	CATEGORY1,
	CATEGORY2,
	LICENSES1,
	LICENSES2,
	USERS1,
	USERS2,
	GROUPS1,
	GROUPS2,
	SCRIPT1,
	SCRIPT2,
	OPTION1,
	OPTION2,
	SHLIBS1,
	SHLIBS_REQD,
	SHLIBS_PROV,
	ANNOTATE1,
	ANNOTATE2,
	ANNOTATE_ADD1,
	ANNOTATE_MOD1,
	ANNOTATE_DEL1,
	ANNOTATE_DEL2,
	CONFLICT,
	PKG_PROVIDE,
	PROVIDE,
	UPDATE_DIGEST,
	CONFIG_FILES,
	UPDATE_CONFIG_FILE,
	PKG_REQUIRE,
	REQUIRE,
	LUASCRIPT1,
	LUASCRIPT2,
	PRSTMT_LAST,
} sql_prstmt_index;

static sql_prstmt sql_prepared_statements[PRSTMT_LAST] = {
	[MTREE] = {
		NULL,
		NULL,
		"T",
	},
	[PKG] = {
		NULL,
		"INSERT OR REPLACE INTO packages( "
			"origin, name, version, comment, desc, message, arch, "
			"maintainer, www, prefix, flatsize, automatic, "
			"licenselogic, time, manifestdigest, dep_formula, vital)"
		"VALUES( ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, "
		"?13, NOW(), ?14, ?15, ?16 )",
		"TTTTTTTTTTIIITTI",
	},
	[DEPS_UPDATE] = {
		NULL,
		"UPDATE deps SET origin=?1, version=?2 WHERE name=?3;",
		"TTT",
	},
	[DEPS] = {
		NULL,
		"INSERT INTO deps (origin, name, version, package_id) "
		"VALUES (?1, ?2, ?3, ?4)",
		"TTTI",
	},
	[FILES] = {
		NULL,
		"INSERT INTO files (path, sha256, package_id) "
		"VALUES (?1, ?2, ?3)",
		"TTI",
	},
	[FILES_REPLACE] = {
		NULL,
		"INSERT OR REPLACE INTO files (path, sha256, package_id) "
		"VALUES (?1, ?2, ?3)",
		"TTI",
	},
	[DIRS1] = {
		NULL,
		"INSERT OR IGNORE INTO directories(path) VALUES(?1)",
		"T",
	},
	[DIRS2] = {
		NULL,
		"INSERT INTO pkg_directories(package_id, directory_id, try) "
		"VALUES (?1, "
		"(SELECT id FROM directories WHERE path = ?2), ?3)",
		"ITI",
	},
	[CATEGORY1] = {
		NULL,
		"INSERT OR IGNORE INTO categories(name) VALUES(?1)",
		"T",
	},
	[CATEGORY2] = {
		NULL,
		"INSERT INTO pkg_categories(package_id, category_id) "
		"VALUES (?1, (SELECT id FROM categories WHERE name = ?2))",
		"IT",
	},
	[LICENSES1] = {
		NULL,
		"INSERT OR IGNORE INTO licenses(name) VALUES(?1)",
		"T",
	},
	[LICENSES2] = {
		NULL,
		"INSERT INTO pkg_licenses(package_id, license_id) "
		"VALUES (?1, (SELECT id FROM licenses WHERE name = ?2))",
		"IT",
	},
	[USERS1] = {
		NULL,
		"INSERT OR IGNORE INTO users(name) VALUES(?1)",
		"T",
	},
	[USERS2] = {
		NULL,
		"INSERT INTO pkg_users(package_id, user_id) "
		"VALUES (?1, (SELECT id FROM users WHERE name = ?2))",
		"IT",
	},
	[GROUPS1] = {
		NULL,
		"INSERT OR IGNORE INTO groups(name) VALUES(?1)",
		"T",
	},
	[GROUPS2] = {
		NULL,
		"INSERT INTO pkg_groups(package_id, group_id) "
		"VALUES (?1, (SELECT id FROM groups WHERE name = ?2))",
		"IT",
	},
	[SCRIPT1] = {
		NULL,
		"INSERT OR IGNORE INTO script(script) VALUES (?1)",
		"T",
	},
	[SCRIPT2] = {
		NULL,
		"INSERT INTO pkg_script(script_id, package_id, type) "
		"VALUES ((SELECT script_id FROM script WHERE script = ?1), "
		"?2, ?3)",
		"TII",
	},
	[OPTION1] = {
		NULL,
		"INSERT OR IGNORE INTO option (option) "
		"VALUES (?1)",
		"T",
	},
	[OPTION2] = {
		NULL,
		"INSERT INTO pkg_option(package_id, option_id, value) "
		"VALUES (?1, "
			"(SELECT option_id FROM option WHERE option = ?2),"
			"?3)",
		"ITT",
	},
	[SHLIBS1] = {
		NULL,
		"INSERT OR IGNORE INTO shlibs(name) VALUES(?1)",
		"T",
	},
	[SHLIBS_REQD] = {
		NULL,
		"INSERT OR IGNORE INTO pkg_shlibs_required(package_id, shlib_id) "
		"VALUES (?1, (SELECT id FROM shlibs WHERE name = ?2))",
		"IT",
	},
	[SHLIBS_PROV] = {
		NULL,
		"INSERT OR IGNORE INTO pkg_shlibs_provided(package_id, shlib_id) "
		"VALUES (?1, (SELECT id FROM shlibs WHERE name = ?2))",
		"IT",
	},
	[ANNOTATE1] = {
		NULL,
		"INSERT OR IGNORE INTO annotation(annotation) "
		"VALUES (?1)",
		"T",
	},
	[ANNOTATE2] = {
		NULL,
		"INSERT OR ROLLBACK INTO pkg_annotation(package_id, tag_id, value_id) "
		"VALUES (?1,"
		" (SELECT annotation_id FROM annotation WHERE annotation = ?2),"
		" (SELECT annotation_id FROM annotation WHERE annotation = ?3))",
		"ITT",
	},
	[ANNOTATE_ADD1] = {
		NULL,
		"INSERT OR IGNORE INTO pkg_annotation(package_id, tag_id, value_id) "
		"VALUES ("
		" (SELECT id FROM packages WHERE name = ?1 ),"
		" (SELECT annotation_id FROM annotation WHERE annotation = ?2),"
		" (SELECT annotation_id FROM annotation WHERE annotation = ?3))",
		"TTTT", // "TTT"???
	},
	[ANNOTATE_MOD1] = {
		NULL,
		"INSERT OR REPLACE INTO pkg_annotation(package_id, tag_id, value_id) "
		"VALUES ("
		" (SELECT id FROM packages WHERE name = ?1 ),"
		" (SELECT annotation_id FROM annotation WHERE annotation = ?2),"
		" (SELECT annotation_id FROM annotation WHERE annotation = ?3))",
		"TTTT", // "TTT"???
	},
	[ANNOTATE_DEL1] = {
		NULL,
		"DELETE FROM pkg_annotation WHERE "
		"package_id IN"
                " (SELECT id FROM packages WHERE name = ?1) "
		"AND tag_id IN"
		" (SELECT annotation_id FROM annotation WHERE annotation = ?2)",
		"TTT", // "TT"???
	},
	[ANNOTATE_DEL2] = {
		NULL,
		"DELETE FROM annotation WHERE"
		" annotation_id NOT IN (SELECT tag_id FROM pkg_annotation) AND"
		" annotation_id NOT IN (SELECT value_id FROM pkg_annotation)",
		"",
	},
	[CONFLICT] = {
		NULL,
		"INSERT INTO pkg_conflicts(package_id, conflict_id) "
		"VALUES (?1, (SELECT id FROM packages WHERE name = ?2))",
		"IT",
	},
	[PKG_PROVIDE] = {
		NULL,
		"INSERT INTO pkg_provides(package_id, provide_id) "
		"VALUES (?1, (SELECT id FROM provides WHERE provide = ?2))",
		"IT",
	},
	[PROVIDE] = {
		NULL,
		"INSERT OR IGNORE INTO provides(provide) VALUES(?1)",
		"T",
	},
	[UPDATE_DIGEST] = {
		NULL,
		"UPDATE packages SET manifestdigest=?1 WHERE id=?2;",
		"TI"
	},
	[CONFIG_FILES] = {
		NULL,
		"INSERT INTO config_files(path, content, package_id) "
		"VALUES (?1, ?2, ?3);",
		"TTI"
	},
	[UPDATE_CONFIG_FILE] = {
		NULL,
		"UPDATE config_files SET content=?1 WHERE path=?2;",
		"TT"
	},
	[PKG_REQUIRE] = {
		NULL,
		"INSERT INTO pkg_requires(package_id, require_id) "
		"VALUES (?1, (SELECT id FROM requires WHERE require = ?2))",
		"IT",
	},
	[REQUIRE] = {
		NULL,
		"INSERT OR IGNORE INTO requires(require) VALUES(?1)",
		"T"
	},
	[LUASCRIPT1] = {
		NULL,
		"INSERT OR IGNORE INTO lua_script(lua_script) VALUES (?1)",
		"T",
	},
	[LUASCRIPT2] = {
		NULL,
		"INSERT INTO pkg_lua_script(lua_script_id, package_id, type) "
		"VALUES ((SELECT lua_script_id FROM lua_script WHERE "
		"lua_script = ?1), ?2, ?3)",
		"TII",
	},
	/* PRSTMT_LAST */
};

static int
prstmt_initialize(struct pkgdb *db)
{
	sql_prstmt_index	 i;
	sqlite3			*sqlite;

	assert(db != NULL);

	if (!db->prstmt_initialized) {
		sqlite = db->sqlite;

		for (i = 0; i < PRSTMT_LAST; i++) {
			if (SQL(i) == NULL)
				continue;
			STMT(i) = prepare_sql(sqlite, SQL(i));
			if (STMT(i) == NULL)
				return (EPKG_FATAL);
		}
		db->prstmt_initialized = true;
	}

	return (EPKG_OK);
}

static int
run_prstmt(sql_prstmt_index s, ...)
{
	int		 retcode;	/* Returns SQLITE error code */
	va_list		 ap;
	sqlite3_stmt	*stmt;
	int		 i;
	const char	*argtypes;

	stmt = STMT(s);
	argtypes = sql_prepared_statements[s].argtypes;

	sqlite3_reset(stmt);

	va_start(ap, s);

	for (i = 0; argtypes[i] != '\0'; i++)
	{
		switch (argtypes[i]) {
		case 'T':
			sqlite3_bind_text(stmt, i + 1, va_arg(ap, const char*),
					  -1, SQLITE_STATIC);
			break;
		case 'I':
			sqlite3_bind_int64(stmt, i + 1, va_arg(ap, int64_t));
			break;
		}
	}

	va_end(ap);

	char *debug_sql = sqlite3_expanded_sql(stmt);
	dbg(4, "running '%s'", debug_sql);
	sqlite3_free(debug_sql);

	retcode = sqlite3_step(stmt);

	return (retcode);
}

static void
prstmt_finalize(struct pkgdb *db)
{
	sql_prstmt_index	i;

	for (i = 0; i < PRSTMT_LAST; i++)
	{
		if (STMT(i) != NULL) {
			sqlite3_finalize(STMT(i));
			STMT(i) = NULL;
		}
	}
	db->prstmt_initialized = false;
	return;
}

/*
 * Register a package in the database.  If successful, the caller is required to
 * call pkgdb_register_finale() in order to either commit or roll back the
 * transaction.  Otherwise, the caller does not need to do any extra cleanup.
 */
int
pkgdb_register_pkg(struct pkgdb *db, struct pkg *pkg, int forced,
    const char *savepoint)
{
	struct pkg		*pkg2 = NULL;
	struct pkg_dep		*dep = NULL;
	struct pkg_file		*file = NULL;
	struct pkg_dir		*dir = NULL;
	struct pkg_option	*option = NULL;
	struct pkg_conflict	*conflict = NULL;
	struct pkg_config_file	*cf = NULL;
	struct pkgdb_it		*it = NULL;
	char			*msg = NULL;

	sqlite3			*s;

	int			 ret;
	int			 retcode = EPKG_FATAL;
	int64_t			 package_id;

	const char		*arch;

	assert(db != NULL);

	if (pkg_is_valid(pkg) != EPKG_OK) {
		pkg_emit_error("the package is not valid");
		return (EPKG_FATAL);
	}

	s = db->sqlite;

	if (pkgdb_transaction_begin_sqlite(s, savepoint) != EPKG_OK)
		return (EPKG_FATAL);

	/* Prefer new ABI over old one */
	arch = pkg->abi != NULL ? pkg->abi : pkg->altabi;

	/*
	 * Insert package record
	 */
	msg = pkg_message_to_str(pkg);
	ret = run_prstmt(PKG, pkg->origin, pkg->name, pkg->version,
	    pkg->comment, pkg->desc, msg, arch, pkg->maintainer,
	    pkg->www, pkg->prefix, pkg->flatsize, (int64_t)pkg->automatic,
	    (int64_t)pkg->licenselogic, pkg->digest, pkg->dep_formula, (int64_t)pkg->vital);
	if (ret != SQLITE_DONE) {
		ERROR_STMT_SQLITE(s, STMT(PKG));
		goto cleanup;
	}

	package_id = sqlite3_last_insert_rowid(s);

	/*
	 * Update dep information on packages that depend on the inserted
	 * package
	 */

	if (run_prstmt(DEPS_UPDATE, pkg->origin,
	    pkg->version ? pkg->version : "", pkg->name)
	    != SQLITE_DONE) {
		ERROR_STMT_SQLITE(s, STMT(DEPS_UPDATE));
		goto cleanup;
	}

	/*
	 * Insert dependencies list
	 */

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		if (run_prstmt(DEPS, dep->origin, dep->name,
		    dep->version ? dep->version : "",
		    package_id) != SQLITE_DONE) {
			ERROR_STMT_SQLITE(s, STMT(DEPS));
			goto cleanup;
		}
	}

	/*
	* Insert files.
	 */

	while (pkg_files(pkg, &file) == EPKG_OK) {
		bool		permissive = false;
		if (match_ucl_lists(file->path,
		    pkg_config_get("FILES_IGNORE_GLOB"),
		    pkg_config_get("FILES_IGNORE_REGEX"))) {
			continue;
			printf("matched\n");
		}

		ret = run_prstmt(FILES, file->path, file->sum, package_id);
		if (ret == SQLITE_DONE)
			continue;
		if (ret != SQLITE_CONSTRAINT) {
			ERROR_STMT_SQLITE(s, STMT(FILES));
			goto cleanup;
		}
		it = pkgdb_query_which(db, file->path, false);
		if (it == NULL) {
			ERROR_SQLITE(s, "pkg which");
			goto cleanup;
		}
		pkg2 = NULL;
		ret = pkgdb_it_next(it, &pkg2, PKG_LOAD_BASIC);
		if (ret == EPKG_END) {
			/* Stray entry in the files table not related to
			   any known package: overwrite this */
			ret = run_prstmt(FILES_REPLACE, file->path, file->sum,
					 package_id);
			pkgdb_it_free(it);
			if (ret == SQLITE_DONE)
				continue;
			else {
				ERROR_STMT_SQLITE(s, STMT(FILES_REPLACE));
				goto cleanup;
			}
		}
		if (ret != EPKG_OK && ret != EPKG_END) {
			pkgdb_it_free(it);
			ERROR_STMT_SQLITE(s, STMT(FILES_REPLACE));
			goto cleanup;
		}
		if (!forced) {
			if (!ctx.developer_mode)
				permissive = pkg_object_bool(pkg_config_get("PERMISSIVE"));
			pkg_emit_error("%s-%s conflicts with %s-%s"
			    " (installs files into the same place). "
			    " Problematic file: %s%s",
			    pkg->name, pkg->version, pkg2->name, pkg2->version, file->path,
			    permissive ? " ignored by permissive mode" : "");
			pkg_free(pkg2);
			if (!permissive) {
				pkgdb_it_free(it);
				goto cleanup;
			}
		} else {
			pkg_emit_error("%s-%s conflicts with %s-%s"
			    " (installs files into the same place). "
			    " Problematic file: %s ignored by forced mode",
			    pkg->name, pkg->version, pkg2->name, pkg2->version, file->path);
			pkg_free(pkg2);
		}
		pkgdb_it_free(it);
	}

	/*
	 * Insert config files
	 */
	while (pkg_config_files(pkg, &cf) == EPKG_OK) {
		if ((ret = run_prstmt(CONFIG_FILES, cf->path, cf->content, package_id)
		    != SQLITE_DONE)) {
			if (ret == SQLITE_CONSTRAINT) {
				pkg_emit_error("Another package already owns :%s",
				    cf->path);
			} else
				ERROR_STMT_SQLITE(s, STMT(CONFIG_FILES));
			goto cleanup;
		}
	}

	/*
	 * Insert dirs.
	 */

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		if (run_prstmt(DIRS1, dir->path) != SQLITE_DONE) {
			ERROR_STMT_SQLITE(s, STMT(DIRS1));
			goto cleanup;
		}
		if ((ret = run_prstmt(DIRS2, package_id, dir->path,
		    (int64_t)true)) != SQLITE_DONE) {
			if (ret == SQLITE_CONSTRAINT) {
				pkg_emit_error("Another package is already "
				    "providing directory: %s",
				    dir->path);
			} else
				ERROR_STMT_SQLITE(s, STMT(DIRS2));
			goto cleanup;
		}
	}

	/*
	 * Insert categories
	 */

	vec_foreach(pkg->categories, i) {
		ret = run_prstmt(CATEGORY1, pkg->categories.d[i]);
		if (ret == SQLITE_DONE)
			ret = run_prstmt(CATEGORY2, package_id, pkg->categories.d[i]);
		if (ret != SQLITE_DONE) {
			ERROR_STMT_SQLITE(s, STMT(CATEGORY2));
			goto cleanup;
		}
	}

	/*
	 * Insert licenses
	 */

	vec_foreach(pkg->licenses, i) {
		if (run_prstmt(LICENSES1, pkg->licenses.d[i])
		    != SQLITE_DONE
		    ||
		    run_prstmt(LICENSES2, package_id, pkg->licenses.d[i])
		    != SQLITE_DONE) {
			ERROR_STMT_SQLITE(s, STMT(LICENSES2));
			goto cleanup;
		}
	}

	/*
	 * Insert users
	 */

	vec_foreach(pkg->users, i) {
		if (run_prstmt(USERS1, pkg->users.d[i])
		    != SQLITE_DONE
		    ||
		    run_prstmt(USERS2, package_id, pkg->users.d[i])
		    != SQLITE_DONE) {
			ERROR_STMT_SQLITE(s, STMT(USERS2));
			goto cleanup;
		}
	}

	/*
	 * Insert groups
	 */

	vec_foreach(pkg->groups, i) {
		if (run_prstmt(GROUPS1, pkg->groups.d[i])
		    != SQLITE_DONE
		    ||
		    run_prstmt(GROUPS2, package_id, pkg->groups.d[i])
		    != SQLITE_DONE) {
			ERROR_STMT_SQLITE(s, STMT(GROUPS2));
			goto cleanup;
		}
	}

	/*
	 * Insert scripts
	 */

	if (pkgdb_insert_scripts(pkg, package_id, s) != EPKG_OK)
		goto cleanup;

	/*
	 * Insert lua scripts
	 */
	if (pkgdb_insert_lua_scripts(pkg, package_id, s) != EPKG_OK)
		goto cleanup;

	/*
	 * Insert options
	 */

	while (pkg_options(pkg, &option) == EPKG_OK) {
		if (run_prstmt(OPTION1, option->key) != SQLITE_DONE
		    ||
		    run_prstmt(OPTION2, package_id, option->key, option->value)
			       != SQLITE_DONE) {
			ERROR_STMT_SQLITE(s, STMT(OPTION2));
			goto cleanup;
		}
	}

	/*
	 * Insert shlibs
	 */
	if (pkgdb_update_shlibs_required(pkg, package_id, s) != EPKG_OK)
		goto cleanup;
	if (pkgdb_update_shlibs_provided(pkg, package_id, s) != EPKG_OK)
		goto cleanup;

	/*
	 * Insert annotation
	 */
	if (pkgdb_insert_annotations(pkg, package_id, s) != EPKG_OK)
		goto cleanup;

	/*
	 * Insert conflicts
	 */
	while (pkg_conflicts(pkg, &conflict) == EPKG_OK) {
		if (run_prstmt(CONFLICT, package_id, conflict->uid)
				!= SQLITE_DONE) {
			ERROR_STMT_SQLITE(s, STMT(CONFLICT));
			goto cleanup;
		}
	}

	/*
	 * Insert provides
	 */
	if (pkgdb_update_provides(pkg, package_id, s) != EPKG_OK)
		goto cleanup;
	if (pkgdb_update_requires(pkg, package_id, s) != EPKG_OK)
		goto cleanup;

	retcode = EPKG_OK;

cleanup:
	if (retcode != EPKG_OK)
		(void)pkgdb_transaction_rollback_sqlite(s, savepoint);
	free(msg);

	return (retcode);
}

static int
pkgdb_insert_scripts(struct pkg *pkg, int64_t package_id, sqlite3 *s)
{
	const char	*script;
	int64_t		 i;

	for (i = 0; i < PKG_NUM_SCRIPTS; i++) {
		script = pkg_script_get(pkg, i);

		if (script == NULL)
			continue;
		if (run_prstmt(SCRIPT1, script) != SQLITE_DONE
		    ||
		    run_prstmt(SCRIPT2, script, package_id, i) != SQLITE_DONE) {
			ERROR_STMT_SQLITE(s, STMT(SCRIPT2));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

static int
pkgdb_insert_lua_scripts(struct pkg *pkg, int64_t package_id, sqlite3 *s)
{
	int64_t			 i;

	for (i = 0; i < PKG_NUM_LUA_SCRIPTS; i++) {
		vec_foreach(pkg->lua_scripts[i], j) {
			if (run_prstmt(LUASCRIPT1, pkg->lua_scripts[i].d[j]) != SQLITE_DONE
			    ||
			    run_prstmt(LUASCRIPT2, pkg->lua_scripts[i].d[j], package_id, i) != SQLITE_DONE) {
				ERROR_STMT_SQLITE(s, STMT(LUASCRIPT2));
				return (EPKG_FATAL);
			}
		}
	}
	return (EPKG_OK);
}

int
pkgdb_update_shlibs_required(struct pkg *pkg, int64_t package_id, sqlite3 *s)
{
	vec_foreach(pkg->shlibs_required, i) {
		if (run_prstmt(SHLIBS1, pkg->shlibs_required.d[i])
		    != SQLITE_DONE
		    ||
		    run_prstmt(SHLIBS_REQD, package_id, pkg->shlibs_required.d[i])
		    != SQLITE_DONE) {
			ERROR_STMT_SQLITE(s, STMT(SHLIBS_REQD));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

int
pkgdb_update_config_file_content(struct pkg *p, sqlite3 *s)
{
	struct pkg_config_file	*cf = NULL;

	while (pkg_config_files(p, &cf) == EPKG_OK) {
		if (run_prstmt(UPDATE_CONFIG_FILE, cf->content, cf->path)
		    != SQLITE_DONE) {
			ERROR_STMT_SQLITE(s, STMT(SHLIBS_REQD));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

int
pkgdb_update_shlibs_provided(struct pkg *pkg, int64_t package_id, sqlite3 *s)
{
	vec_foreach(pkg->shlibs_provided, i) {
		if (run_prstmt(SHLIBS1, pkg->shlibs_provided.d[i])
		    != SQLITE_DONE
		    ||
		    run_prstmt(SHLIBS_PROV, package_id, pkg->shlibs_provided.d[i])
		    != SQLITE_DONE) {
			ERROR_STMT_SQLITE(s, STMT(SHLIBS_PROV));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

int
pkgdb_update_requires(struct pkg *pkg, int64_t package_id, sqlite3 *s)
{
	vec_foreach(pkg->requires, i) {
		if (run_prstmt(REQUIRE, pkg->requires.d[i])
		    != SQLITE_DONE
		    ||
		    run_prstmt(PKG_REQUIRE, package_id, pkg->requires.d[i])
		    != SQLITE_DONE) {
			ERROR_STMT_SQLITE(s, STMT(PKG_REQUIRE));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

int
pkgdb_update_provides(struct pkg *pkg, int64_t package_id, sqlite3 *s)
{
	vec_foreach(pkg->provides, i) {
		if (run_prstmt(PROVIDE, pkg->provides.d[i])
		    != SQLITE_DONE
		    ||
		    run_prstmt(PKG_PROVIDE, package_id, pkg->provides.d[i])
		    != SQLITE_DONE) {
			ERROR_STMT_SQLITE(s, STMT(PKG_PROVIDE));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

int
pkgdb_insert_annotations(struct pkg *pkg, int64_t package_id, sqlite3 *s)
{
	struct pkg_kv	*kv;

	vec_foreach(pkg->annotations, i) {
		kv = pkg->annotations.d[i];
		if (run_prstmt(ANNOTATE1, kv->key)
		    != SQLITE_DONE
		    ||
		    run_prstmt(ANNOTATE1,kv->value)
		    != SQLITE_DONE
		    ||
		    run_prstmt(ANNOTATE2, package_id,
			kv->key, kv->value)
		    != SQLITE_DONE) {
			ERROR_STMT_SQLITE(s, STMT(ANNOTATE2));
			return (EPKG_FATAL);
		}
	}
	return (EPKG_OK);
}

int
pkgdb_add_annotation(struct pkgdb *db, struct pkg *pkg, const char *tag,
    const char *value)
{
	int		 rows_changed;

	assert(pkg != NULL);
	assert(tag != NULL);
	assert(value != NULL);

	if (run_prstmt(ANNOTATE1, tag) != SQLITE_DONE
	    ||
	    run_prstmt(ANNOTATE1, value) != SQLITE_DONE
	    ||
	    run_prstmt(ANNOTATE_ADD1, pkg->uid, tag, value)
	    != SQLITE_DONE) {
		ERROR_STMT_SQLITE(db->sqlite, STMT(ANNOTATE_ADD1));
		pkgdb_transaction_rollback_sqlite(db->sqlite, NULL);
		return (EPKG_FATAL);
	}

	/* Expect rows_changed == 1 unless there's already an
	   annotation using the given tag */

	rows_changed = sqlite3_changes(db->sqlite);

	return (rows_changed == 1 ? EPKG_OK : EPKG_WARN);
}

int
pkgdb_set_pkg_digest(struct pkgdb *db, struct pkg *pkg)
{

	assert(pkg != NULL);
	assert(db != NULL);

	if (run_prstmt(UPDATE_DIGEST, pkg->digest, pkg->id) != SQLITE_DONE) {
		ERROR_STMT_SQLITE(db->sqlite, STMT(UPDATE_DIGEST));
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

int
pkgdb_modify_annotation(struct pkgdb *db, struct pkg *pkg, const char *tag,
        const char *value)
{
	int rows_changed;

	assert(pkg!= NULL);
	assert(tag != NULL);
	assert(value != NULL);

	if (pkgdb_transaction_begin_sqlite(db->sqlite, NULL) != EPKG_OK)
		return (EPKG_FATAL);

	if (run_prstmt(ANNOTATE1, tag) != SQLITE_DONE
	    ||
	    run_prstmt(ANNOTATE1, value) != SQLITE_DONE
	    ||
	    run_prstmt(ANNOTATE_MOD1, pkg->uid, tag, value) !=
	        SQLITE_DONE) {
		ERROR_STMT_SQLITE(db->sqlite, STMT(ANNOTATE_MOD1));
		pkgdb_transaction_rollback_sqlite(db->sqlite, NULL);

		return (EPKG_FATAL);
	}
	rows_changed = sqlite3_changes(db->sqlite);

	if (run_prstmt(ANNOTATE_DEL2) != SQLITE_DONE) {
		ERROR_STMT_SQLITE(db->sqlite, STMT(ANNOTATE_DEL2));
		pkgdb_transaction_rollback_sqlite(db->sqlite, NULL);

		return (EPKG_FATAL);
	}

	if (pkgdb_transaction_commit_sqlite(db->sqlite, NULL) != EPKG_OK)
		return (EPKG_FATAL);

	/* Something has gone very wrong if rows_changed != 1 here */
	return (rows_changed == 1 ? EPKG_OK : EPKG_WARN);
}

int
pkgdb_delete_annotation(struct pkgdb *db, struct pkg *pkg, const char *tag)
{
	int rows_changed;
	bool result;

	assert(pkg != NULL);
	assert(tag != NULL);

	if (pkgdb_transaction_begin_sqlite(db->sqlite, NULL) != EPKG_OK)
		return (EPKG_FATAL);

	result = (run_prstmt(ANNOTATE_DEL1, pkg->uid, tag)
		  == SQLITE_DONE);

	rows_changed = sqlite3_changes(db->sqlite);

	if (!result
	    ||
	    run_prstmt(ANNOTATE_DEL2) != SQLITE_DONE) {
		ERROR_STMT_SQLITE(db->sqlite, STMT(ANNOTATE_DEL2));
		pkgdb_transaction_rollback_sqlite(db->sqlite, NULL);
		return (EPKG_FATAL);
	}

	if (pkgdb_transaction_commit_sqlite(db->sqlite, NULL) != EPKG_OK)
		return (EPKG_FATAL);

	return (rows_changed == 1 ? EPKG_OK : EPKG_WARN);
}

/*
 * Complete a transaction started by pkgdb_register_pkg().
 */
int
pkgdb_register_finale(struct pkgdb *db, int retcode, const char *savepoint)
{
	int	ret = EPKG_OK;

	assert(db != NULL);

	if (retcode == EPKG_OK)
		ret = pkgdb_transaction_commit_sqlite(db->sqlite, savepoint);
	else
		ret = pkgdb_transaction_rollback_sqlite(db->sqlite, savepoint);

	return (ret);
}

int
pkgdb_unregister_pkg(struct pkgdb *db, int64_t id)
{
	sqlite3_stmt	*stmt_del;
	unsigned int	 obj;
	int		 ret;
	const char	 sql[] = ""
		"DELETE FROM packages WHERE id = ?1;";
	const char	*deletions[] = {
		"directories WHERE id NOT IN "
			"(SELECT DISTINCT directory_id FROM pkg_directories)",
		"categories WHERE id NOT IN "
			"(SELECT DISTINCT category_id FROM pkg_categories)",
		"licenses WHERE id NOT IN "
			"(SELECT DISTINCT license_id FROM pkg_licenses)",
		/* TODO print the users that are not used anymore */
		"users WHERE id NOT IN "
			"(SELECT DISTINCT user_id FROM pkg_users)",
		/* TODO print the groups that are not used anymore */
		"groups WHERE id NOT IN "
			"(SELECT DISTINCT group_id FROM pkg_groups)",
		"shlibs WHERE id NOT IN "
			"(SELECT DISTINCT shlib_id FROM pkg_shlibs_required)"
			"AND id NOT IN "
			"(SELECT DISTINCT shlib_id FROM pkg_shlibs_provided)",
		"script WHERE script_id NOT IN "
		        "(SELECT DISTINCT script_id FROM pkg_script)",
		"lua_script WHERE lua_script_id NOT IN "
			"(SELECT DISTINCT lua_script_id FROM pkg_lua_script)",
	};

	assert(db != NULL);

	stmt_del = prepare_sql(db->sqlite, sql);
	if (stmt_del == NULL)
		return (EPKG_FATAL);

	sqlite3_bind_int64(stmt_del, 1, id);
	pkgdb_debug(4, stmt_del);

	ret = sqlite3_step(stmt_del);

	if (ret != SQLITE_DONE) {
		ERROR_STMT_SQLITE(db->sqlite, stmt_del);
		sqlite3_finalize(stmt_del);
		return (EPKG_FATAL);
	}
	sqlite3_finalize(stmt_del);

	for (obj = 0 ;obj < NELEM(deletions); obj++) {
		ret = sql_exec(db->sqlite, "DELETE FROM %s;", deletions[obj]);
		if (ret != EPKG_OK)
			return (EPKG_FATAL);
	}
	return (EPKG_OK);
}

int
sql_exec(sqlite3 *s, const char *sql, ...)
{
	va_list		 ap;
	const char	*sql_to_exec;
	char		*sqlbuf = NULL;
	char		*errmsg;
	int		 ret = EPKG_FATAL;

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

	dbg(4, "executing '%s'", sql_to_exec);
	if (sqlite3_exec(s, sql_to_exec, NULL, NULL, &errmsg) != SQLITE_OK) {
		ERROR_SQLITE(s, sql_to_exec);
		sqlite3_free(errmsg);
		goto cleanup;
	}

	ret = EPKG_OK;

cleanup:
	if (sqlbuf != NULL)
		sqlite3_free(sqlbuf);

	return (ret);
}

int
get_pragma(sqlite3 *s, const char *sql, int64_t *res, bool silence)
{
	sqlite3_stmt	*stmt;
	int		 ret;

	assert(s != NULL && sql != NULL);

	if (sqlite3_prepare_v2(s, sql, -1, &stmt, NULL) != SQLITE_OK) {
		if (!silence)
			ERROR_SQLITE(s, sql);
		return (EPKG_OK);
	}
	pkgdb_debug(4, stmt);

	PKGDB_SQLITE_RETRY_ON_BUSY(ret)
		ret = sqlite3_step(stmt);

	if (ret == SQLITE_ROW)
		*res = sqlite3_column_int64(stmt, 0);


	if (ret != SQLITE_ROW && !silence)
		ERROR_STMT_SQLITE(s, stmt);
	sqlite3_finalize(stmt);

	return (ret == SQLITE_ROW ? EPKG_OK : EPKG_FATAL);
}

int
pkgdb_compact(struct pkgdb *db)
{
	int64_t	page_count = 0;
	int64_t	freelist_count = 0;
	int	ret;

	assert(db != NULL);

	ret = get_pragma(db->sqlite, "PRAGMA page_count;", &page_count, false);
	if (ret != EPKG_OK)
		return (EPKG_FATAL);

	ret = get_pragma(db->sqlite, "PRAGMA freelist_count;",
			 &freelist_count, false);
	if (ret != EPKG_OK)
		return (EPKG_FATAL);

	/*
	 * Only compact if we will save 25% (or more) of the current
	 * used space.
	 */

	if (freelist_count > 0 && freelist_count / (float)page_count < 0.25)
		return (EPKG_OK);

	return (sql_exec(db->sqlite, "VACUUM;"));
}

static int
pkgdb_vset(struct pkgdb *db, int64_t id, va_list ap)
{
	int		 attr;
	sqlite3_stmt	*stmt;
	int64_t		 flatsize;
	bool automatic, locked, vital;
	char		*oldval;
	char		*newval;

	/* Ensure there is an entry for each of the pkg_set_attr enum values */
	const char *sql[PKG_SET_MAX] = {
		[PKG_SET_FLATSIZE]  =
		    "UPDATE packages SET flatsize = ?1 WHERE id = ?2",
		[PKG_SET_AUTOMATIC] =
		    "UPDATE packages SET automatic = ?1 WHERE id = ?2",
		[PKG_SET_LOCKED] =
		    "UPDATE packages SET locked = ?1 WHERE id = ?2",
		[PKG_SET_DEPORIGIN] =
		    "UPDATE deps SET origin = ?1, "
		    "name=(SELECT name FROM packages WHERE origin = ?1), "
		    "version=(SELECT version FROM packages WHERE origin = ?1) "
		    "WHERE package_id = ?2 AND origin = ?3",
		[PKG_SET_ORIGIN]    =
		    "UPDATE packages SET origin=?1 WHERE id=?2",
		[PKG_SET_DEPNAME] =
		    "UPDATE deps SET name = ?1, "
		    "version=(SELECT version FROM packages WHERE name = ?1) "
		    "WHERE package_id = ?2 AND name = ?3",
		[PKG_SET_NAME]    =
		    "UPDATE packages SET name=?1 WHERE id=?2",
		[PKG_SET_VITAL] =
		    "UPDATE packages SET vital = ?1 WHERE id = ?2",
	};

	while ((attr = va_arg(ap, int)) > 0) {
		stmt = prepare_sql(db->sqlite, sql[attr]);
		if (stmt == NULL)
			return (EPKG_FATAL);

		switch (attr) {
		case PKG_SET_FLATSIZE:
			flatsize = va_arg(ap, int64_t);
			sqlite3_bind_int64(stmt, 1, flatsize);
			sqlite3_bind_int64(stmt, 2, id);
			break;
		case PKG_SET_AUTOMATIC:
			automatic = (bool)va_arg(ap, int);
			sqlite3_bind_int64(stmt, 1, automatic);
			sqlite3_bind_int64(stmt, 2, id);
			break;
		case PKG_SET_LOCKED:
			locked = (bool)va_arg(ap, int);
			sqlite3_bind_int64(stmt, 1, locked);
			sqlite3_bind_int64(stmt, 2, id);
			break;
		case PKG_SET_DEPORIGIN:
		case PKG_SET_DEPNAME:
			oldval = va_arg(ap, char *);
			newval = va_arg(ap, char *);
			sqlite3_bind_text(stmt, 1, newval, -1, SQLITE_STATIC);
			sqlite3_bind_int64(stmt, 2, id);
			sqlite3_bind_text(stmt, 3, oldval, -1, SQLITE_STATIC);
			break;
		case PKG_SET_ORIGIN:
		case PKG_SET_NAME:
			newval = va_arg(ap, char *);
			sqlite3_bind_text(stmt, 1, newval, -1, SQLITE_STATIC);
			sqlite3_bind_int64(stmt, 2, id);
			break;
		case PKG_SET_VITAL:
			vital = (bool)va_arg(ap, int);
			sqlite3_bind_int64(stmt, 1, vital);
			sqlite3_bind_int64(stmt, 2, id);
			break;
		}

		pkgdb_debug(4, stmt);
		if (sqlite3_step(stmt) != SQLITE_DONE) {
			ERROR_STMT_SQLITE(db->sqlite, stmt);
			sqlite3_finalize(stmt);
			return (EPKG_FATAL);
		}

		sqlite3_finalize(stmt);
	}
	return (EPKG_OK);
}

int
pkgdb_set2(struct pkgdb *db, struct pkg *pkg, ...)
{
	int ret = EPKG_OK;
	va_list	ap;

	assert(pkg != NULL);

	va_start(ap, pkg);
	ret = pkgdb_vset(db, pkg->id, ap);
	va_end(ap);

	return (ret);
}

int
pkgdb_file_set_cksum(struct pkgdb *db, struct pkg_file *file,
     const char *sum)
{
	sqlite3_stmt	*stmt = NULL;
	const char	 sql_file_update[] = ""
		"UPDATE files SET sha256 = ?1 WHERE path = ?2";

	stmt = prepare_sql(db->sqlite, sql_file_update);
	if (stmt == NULL)
		return (EPKG_FATAL);
	sqlite3_bind_text(stmt, 1, sum, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, file->path, -1, SQLITE_STATIC);
	pkgdb_debug(4, stmt);

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		ERROR_STMT_SQLITE(db->sqlite, stmt);
		sqlite3_finalize(stmt);
		return (EPKG_FATAL);
	}
	sqlite3_finalize(stmt);
	file->sum = xstrdup(sum);

	return (EPKG_OK);
}

/*
 * create our custom functions in the sqlite3 connection.
 * Used both in the shell and pkgdb_open
 */
int
pkgdb_sqlcmd_init(sqlite3 *db, __unused const char **err,
    __unused const void *noused)
{
	sqlite3_create_function(db, "now", 0, SQLITE_ANY|SQLITE_DETERMINISTIC, NULL,
	    pkgdb_now, NULL, NULL);
	sqlite3_create_function(db, "regexp", 2, SQLITE_ANY|SQLITE_DETERMINISTIC, NULL,
	    pkgdb_regex, NULL, NULL);
	sqlite3_create_function(db, "vercmp", 3, SQLITE_ANY|SQLITE_DETERMINISTIC, NULL,
	    pkgdb_vercmp, NULL, NULL);

	return SQLITE_OK;
}

void
pkgdb_cmd(int argc, char **argv)
{
	sqlite3_shell(argc, argv);
}

void
pkgdb_init_proc(void)
{
	sqlite3_initialize();
	sqlite3_auto_extension((void(*)(void))pkgdb_sqlcmd_init);
}


void
pkgshell_opendb(const char **reponame)
{
	char		 localpath[MAXPATHLEN];

	snprintf(localpath, sizeof(localpath), "%s/local.sqlite", ctx.dbdir);
	*reponame = xstrdup(localpath);
}

static int
pkgdb_write_lock_pid(struct pkgdb *db)
{
	const char lock_pid_sql[] = ""
			"INSERT INTO pkg_lock_pid VALUES (?1);";
	sqlite3_stmt	*stmt = NULL;

	stmt = prepare_sql(db->sqlite, lock_pid_sql);
	if (stmt == NULL)
		return (EPKG_FATAL);
	sqlite3_bind_int64(stmt, 1, (int64_t)getpid());

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		ERROR_SQLITE(db->sqlite, lock_pid_sql);
		sqlite3_finalize(stmt);
		return (EPKG_FATAL);
	}
	sqlite3_finalize(stmt);

	return (EPKG_OK);
}

static int
pkgdb_remove_lock_pid(struct pkgdb *db, int64_t pid)
{
	const char lock_pid_sql[] = ""
			"DELETE FROM pkg_lock_pid WHERE pid = ?1;";
	sqlite3_stmt	*stmt = NULL;

	stmt = prepare_sql(db->sqlite, lock_pid_sql);
	if (stmt == NULL)
		return (EPKG_FATAL);
	sqlite3_bind_int64(stmt, 1, pid);

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		ERROR_STMT_SQLITE(db->sqlite, stmt);
		sqlite3_finalize(stmt);
		return (EPKG_FATAL);
	}
	sqlite3_finalize(stmt);

	return (EPKG_OK);
}

static int
pkgdb_check_lock_pid(struct pkgdb *db)
{
	sqlite3_stmt	*stmt = NULL;
	int found = 0;
	int64_t pid, lpid;
	const char query[] = "SELECT pid FROM pkg_lock_pid;";

	stmt = prepare_sql(db->sqlite, query);
	if (stmt == NULL)
		return (EPKG_FATAL);

	lpid = getpid();

	while (sqlite3_step(stmt) != SQLITE_DONE) {
		pid = sqlite3_column_int64(stmt, 0);
		if (pid != lpid) {
			if (kill((pid_t)pid, 0) == -1) {
				dbg(1, "found stale pid %lld in lock database, my pid is: %lld",
						(long long)pid, (long long)lpid);
				if (pkgdb_remove_lock_pid(db, pid) != EPKG_OK){
					sqlite3_finalize(stmt);
					return (EPKG_FATAL);
				}
			}
			else {
				pkg_emit_notice("process with pid %lld still holds the lock",
						(long long int)pid);
				found ++;
			}
		}
	}
	sqlite3_finalize(stmt);

	if (found == 0)
		return (EPKG_END);

	return (EPKG_OK);
}

static int
pkgdb_reset_lock(struct pkgdb *db)
{
	const char init_sql[] = ""
		"UPDATE pkg_lock SET exclusive=0, advisory=0, read=0;";
	int ret;

	ret = sqlite3_exec(db->sqlite, init_sql, NULL, NULL, NULL);

	if (ret == SQLITE_OK)
		return (EPKG_OK);

	return (EPKG_FATAL);
}

static int
pkgdb_try_lock(struct pkgdb *db, const char *lock_sql, pkgdb_lock_t type,
		bool upgrade)
{
	unsigned int tries = 0;
	struct timespec ts;
	int ret = EPKG_END;
	const pkg_object *timeout, *max_tries;
	double num_timeout = 1.0;
	int64_t num_maxtries = 1;
	const char reset_lock_sql[] = ""
			"DELETE FROM pkg_lock; INSERT INTO pkg_lock VALUES (0,0,0);";


	timeout = pkg_config_get("LOCK_WAIT");
	max_tries = pkg_config_get("LOCK_RETRIES");

	if (timeout)
		num_timeout = pkg_object_int(timeout);
	if (max_tries)
		num_maxtries = pkg_object_int(max_tries);

	while (tries <= num_maxtries) {
		ret = sqlite3_exec(db->sqlite, lock_sql, NULL, NULL, NULL);
		if (ret != SQLITE_OK) {
			if (ret == SQLITE_READONLY && type == PKGDB_LOCK_READONLY) {
				dbg(1, "want read lock but cannot write to database, "
						"slightly ignore this error for now");
				return (EPKG_OK);
			}
			return (EPKG_FATAL);
		}

		ret = EPKG_END;
		if (sqlite3_changes(db->sqlite) == 0) {
			if (pkgdb_check_lock_pid(db) == EPKG_END) {
				/* No live processes found, so we can safely reset lock */
				dbg(1, "no concurrent processes found, cleanup the lock");
				pkgdb_reset_lock(db);

				if (upgrade) {
					/*
					 * In case of upgrade we should obtain a lock from the beginning,
					 * hence switch upgrade to retain
					 */
					pkgdb_remove_lock_pid(db, (int64_t)getpid());
					return pkgdb_obtain_lock(db, type);
				}
				else {
					/*
					 * We might have inconsistent db, or some strange issue, so
					 * just insert new record and go forward
					 */
					pkgdb_remove_lock_pid(db, (int64_t)getpid());
					sqlite3_exec(db->sqlite, reset_lock_sql, NULL, NULL, NULL);
					return pkgdb_obtain_lock(db, type);
				}
			}
			else if (num_timeout > 0) {
				ts.tv_sec = (int)num_timeout;
				ts.tv_nsec = (num_timeout - (int)num_timeout) * 1000000000.;
				dbg(1, "waiting for database lock for %d times, "
						"next try in %.2f seconds", tries, num_timeout);
				(void)nanosleep(&ts, NULL);
			}
			else {
				break;
			}
		}
		else if (!upgrade) {
			ret = pkgdb_write_lock_pid(db);
			break;
		}
		else {
			ret = EPKG_OK;
			break;
		}
		tries ++;
	}

	return (ret);
}

int
pkgdb_obtain_lock(struct pkgdb *db, pkgdb_lock_t type)
{
	int ret;

	const char readonly_lock_sql[] = ""
			"UPDATE pkg_lock SET read=read+1 WHERE exclusive=0;";
	const char advisory_lock_sql[] = ""
			"UPDATE pkg_lock SET advisory=1 WHERE exclusive=0 AND advisory=0;";
	const char exclusive_lock_sql[] = ""
			"UPDATE pkg_lock SET exclusive=1 WHERE exclusive=0 AND advisory=0 AND read=0;";
	const char *lock_sql = NULL;

	assert(db != NULL);

	switch (type) {
	case PKGDB_LOCK_READONLY:
		if (!ucl_object_toboolean(pkg_config_get("READ_LOCK")))
				return (EPKG_OK);
		lock_sql = readonly_lock_sql;
		dbg(1, "want to get a read only lock on a database");
		break;
	case PKGDB_LOCK_ADVISORY:
		lock_sql = advisory_lock_sql;
		dbg(1, "want to get an advisory lock on a database");
		break;
	case PKGDB_LOCK_EXCLUSIVE:
		dbg(1, "want to get an exclusive lock on a database");
		lock_sql = exclusive_lock_sql;
		break;
	}

	ret = pkgdb_try_lock(db, lock_sql, type, false);

	if (ret != EPKG_OK)
		dbg(1, "failed to obtain the lock: %s",
		    sqlite3_errmsg(db->sqlite));

	return (ret);
}

int
pkgdb_upgrade_lock(struct pkgdb *db, pkgdb_lock_t old_type, pkgdb_lock_t new_type)
{
	const char advisory_exclusive_lock_sql[] = ""
		"UPDATE pkg_lock SET exclusive=1,advisory=1 WHERE exclusive=0 AND advisory=1 AND read=0;";
	int ret = EPKG_FATAL;

	assert(db != NULL);

	if (old_type == PKGDB_LOCK_ADVISORY && new_type == PKGDB_LOCK_EXCLUSIVE) {
		dbg(1, "want to upgrade advisory to exclusive lock");
		ret = pkgdb_try_lock(db, advisory_exclusive_lock_sql,
				new_type, true);
	}

	return (ret);
}

int
pkgdb_downgrade_lock(struct pkgdb *db, pkgdb_lock_t old_type,
    pkgdb_lock_t new_type)
{
	const char downgrade_exclusive_lock_sql[] = ""
		"UPDATE pkg_lock SET exclusive=0,advisory=1 WHERE exclusive=1 "
		"AND advisory=1 AND read=0;";
	int ret = EPKG_FATAL;

	assert(db != NULL);

	if (old_type == PKGDB_LOCK_EXCLUSIVE &&
	    new_type == PKGDB_LOCK_ADVISORY) {
		dbg(1, "want to downgrade exclusive to advisory lock");
		ret = pkgdb_try_lock(db, downgrade_exclusive_lock_sql,
		    new_type, true);
	}

	return (ret);
}

int
pkgdb_release_lock(struct pkgdb *db, pkgdb_lock_t type)
{
	const char readonly_unlock_sql[] = ""
			"UPDATE pkg_lock SET read=read-1 WHERE read>0;";
	const char advisory_unlock_sql[] = ""
			"UPDATE pkg_lock SET advisory=0 WHERE advisory=1;";
	const char exclusive_unlock_sql[] = ""
			"UPDATE pkg_lock SET exclusive=0 WHERE exclusive=1;";
	const char *unlock_sql = NULL;
	int ret = EPKG_FATAL;

	if (db == NULL)
		return (EPKG_OK);

	switch (type) {
	case PKGDB_LOCK_READONLY:
		if (!ucl_object_toboolean(pkg_config_get("READ_LOCK")))
			return (EPKG_OK);

		unlock_sql = readonly_unlock_sql;
		dbg(1, "release a read only lock on a database");

		break;
	case PKGDB_LOCK_ADVISORY:
		unlock_sql = advisory_unlock_sql;
		dbg(1, "release an advisory lock on a database");
		break;
	case PKGDB_LOCK_EXCLUSIVE:
		dbg(1, "release an exclusive lock on a database");
		unlock_sql = exclusive_unlock_sql;
		break;
	}

	ret = sqlite3_exec(db->sqlite, unlock_sql, NULL, NULL, NULL);
	if (ret != SQLITE_OK)
		return (EPKG_FATAL);

	if (sqlite3_changes(db->sqlite) == 0)
		return (EPKG_END);

	return pkgdb_remove_lock_pid(db, (int64_t)getpid());
}

int64_t
pkgdb_stats(struct pkgdb *db, pkg_stats_t type)
{
	sqlite3_stmt	*stmt = NULL;
	int64_t		 stats = 0;
	const char *sql = NULL;

	assert(db != NULL);

	switch(type) {
	case PKG_STATS_LOCAL_COUNT:
		sql = "SELECT COUNT(id) FROM main.packages;";
		break;
	case PKG_STATS_LOCAL_SIZE:
		sql = "SELECT SUM(flatsize) FROM main.packages;";
		break;
	case PKG_STATS_REMOTE_UNIQUE:
	case PKG_STATS_REMOTE_COUNT:
	case PKG_STATS_REMOTE_SIZE:
		vec_foreach(db->repos, i) {
			if (db->repos.d[i]->ops->stat != NULL)
				stats += db->repos.d[i]->ops->stat(db->repos.d[i], type);
		}
		return (stats);
		break;
	case PKG_STATS_REMOTE_REPOS:
		return (vec_len(&db->repos));
		break;
	}

	stmt = prepare_sql(db->sqlite, sql);
	if (stmt == NULL)
		return (-1);

	while (sqlite3_step(stmt) != SQLITE_DONE) {
		stats = sqlite3_column_int64(stmt, 0);
		pkgdb_debug(4, stmt);
	}

	sqlite3_finalize(stmt);

	return (stats);
}


int
pkgdb_begin_solver(struct pkgdb *db)
{
	const char solver_sql[] = ""
		"BEGIN TRANSACTION;";
	const char update_digests_sql[] = ""
		"DROP INDEX IF EXISTS pkg_digest_id;"
		"BEGIN TRANSACTION;";
	const char end_update_sql[] = ""
		"END TRANSACTION;"
		"CREATE INDEX pkg_digest_id ON packages(name, manifestdigest);";
	struct pkgdb_it *it;
	struct pkg *p = NULL;
	pkgs_t pkglist = vec_init();
	int rc = EPKG_OK;
	int64_t cnt = 0, cur = 0;

	it = pkgdb_query_cond(db, " WHERE manifestdigest IS NULL OR manifestdigest==''",
		NULL, MATCH_ALL);
	if (it != NULL) {
		while (pkgdb_it_next(it, &p, PKG_LOAD_BASIC|PKG_LOAD_OPTIONS) == EPKG_OK) {
			pkg_checksum_calculate(p, NULL, false, true, false);
			vec_push(&pkglist, p);
			p = NULL;
			cnt ++;
		}
		pkgdb_it_free(it);

		if (vec_len(&pkglist) > 0) {
			rc = sql_exec(db->sqlite, update_digests_sql);
			if (rc != EPKG_OK) {
				ERROR_SQLITE(db->sqlite, update_digests_sql);
			}
			else {
				pkg_emit_progress_start("Updating database digests format");
				vec_foreach(pkglist, i) {
					p = pkglist.d[i];
					pkg_emit_progress_tick(cur++, cnt);
					rc = run_prstmt(UPDATE_DIGEST, p->digest, p->id);
					if (rc != SQLITE_DONE) {
						assert(0);
						ERROR_STMT_SQLITE(db->sqlite, STMT(UPDATE_DIGEST));
					}
				}

				pkg_emit_progress_tick(cnt, cnt);
				if (rc == SQLITE_DONE)
					rc = sql_exec(db->sqlite, end_update_sql);

				if (rc != SQLITE_OK)
					ERROR_SQLITE(db->sqlite, end_update_sql);
			}
		}

		if (rc == EPKG_OK)
			rc = sql_exec(db->sqlite, solver_sql);

		vec_free_and_free(&pkglist, pkg_free);
	} else {
		rc = sql_exec(db->sqlite, solver_sql);
	}

	return (rc);
}

int
pkgdb_end_solver(struct pkgdb *db)
{
	const char solver_sql[] = ""
		"END TRANSACTION;";

	return (sql_exec(db->sqlite, solver_sql));
}

int
pkgdb_is_dir_used(struct pkgdb *db, struct pkg *p, const char *dir, int64_t *res)
{
	sqlite3_stmt *stmt;
	int ret;

	const char sql[] = ""
		"SELECT count(package_id) FROM pkg_directories, directories "
		"WHERE directory_id = directories.id AND directories.path = ?1 "
		"AND package_id != ?2;";

	stmt = prepare_sql(db->sqlite, sql);
	if (stmt == NULL)
		return (EPKG_FATAL);

	sqlite3_bind_text(stmt, 1, dir, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 2, p->id);

	ret = sqlite3_step(stmt);

	if (ret == SQLITE_ROW)
		*res = sqlite3_column_int64(stmt, 0);

	sqlite3_finalize(stmt);

	if (ret != SQLITE_ROW) {
		ERROR_SQLITE(db->sqlite, sql);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

void
pkgdb_debug(int level, sqlite3_stmt *stmt)
{
	char *str;

	if (ctx.debug_level < level)
		return;

	str = sqlite3_expanded_sql(stmt);
	dbg(level, "running: '%s'", str);
	sqlite3_free(str);
}

bool
pkgdb_is_shlib_provided(struct pkgdb *db, const char *req)
{
	sqlite3_stmt *stmt;
	int ret;
	bool found = false;

	const char *sql = ""
		"select package_id from pkg_shlibs_provided INNER JOIN shlibs "
		"on pkg_shlibs_provided.shlib_id = shlibs.id "
		"where shlibs.name=?1" ;

	stmt = prepare_sql(db->sqlite, sql);
	if (stmt == NULL)
		return (false);

	sqlite3_bind_text(stmt, 1, req, -1, SQLITE_TRANSIENT);
	ret = sqlite3_step(stmt);
	if (ret == SQLITE_ROW)
		found = true;

	sqlite3_finalize(stmt);
	return (found);
}

bool
pkgdb_is_provided(struct pkgdb *db, const char *req)
{
	sqlite3_stmt *stmt;
	int ret;
	bool found = false;

	const char *sql = ""
		"select package_id from pkg_provides INNER JOIN provides "
		"on pkg_provides.provide_id = provides.id "
		"where provides.provide = ?1" ;

	stmt = prepare_sql(db->sqlite, sql);
	if (stmt == NULL)
		return (false);

	sqlite3_bind_text(stmt, 1, req, -1, SQLITE_TRANSIENT);
	ret = sqlite3_step(stmt);
	if (ret == SQLITE_ROW)
		found = true;

	sqlite3_finalize(stmt);
	return (found);
}
