/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
 * Copyright (c) 2013 Gerald Pfeifer <gerald@pfeifer.com>
 * Copyright (c) 2013-2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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

#include <sqlite3.h>

#ifdef HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/utils.h"

#include "private/db_upgrades.h"

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
#define DB_SCHEMA_MINOR	30

#define DBVERSION (DB_SCHEMA_MAJOR * 1000 + DB_SCHEMA_MINOR)

static int pkgdb_upgrade(struct pkgdb *);
static int prstmt_initialize(struct pkgdb *db);
/* static int run_prstmt(sql_prstmt_index s, ...); */
static void prstmt_finalize(struct pkgdb *db);
static int pkgdb_insert_scripts(struct pkg *pkg, int64_t package_id, sqlite3 *s);


extern int sqlite3_shell(int, char**);

void
pkgdb_regex(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
	const unsigned char	*regex = NULL;
	const unsigned char	*str;
	regex_t			*re;
	int			 ret;

	if (argc != 2 || (regex = sqlite3_value_text(argv[0])) == NULL ||
		(str = sqlite3_value_text(argv[1])) == NULL) {
		sqlite3_result_error(ctx, "SQL function regex() called "
		    "with invalid arguments.\n", -1);
		return;
	}

	re = (regex_t *)sqlite3_get_auxdata(ctx, 0);
	if (re == NULL) {
		int cflags;

		if (pkgdb_case_sensitive())
			cflags = REG_EXTENDED | REG_NOSUB;
		else
			cflags = REG_EXTENDED | REG_NOSUB | REG_ICASE;

		re = malloc(sizeof(regex_t));
		if (regcomp(re, regex, cflags) != 0) {
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
pkgdb_split_common(sqlite3_context *ctx, int argc, sqlite3_value **argv,
    char delim, const char *first, const char *second)
{
	const unsigned char *what = NULL;
	const unsigned char *data;
	const unsigned char *pos;

	if (argc != 2 || (what = sqlite3_value_text(argv[0])) == NULL ||
	    (data = sqlite3_value_text(argv[1])) == NULL) {
		sqlite3_result_error(ctx, "SQL function split_*() called "
		    "with invalid arguments.\n", -1);
		return;
	}

	if (strcasecmp(what, first) == 0) {
		pos = strrchr(data, delim);
		if (pos != NULL)
			sqlite3_result_text(ctx, data, (pos - data), NULL);
		else
			sqlite3_result_text(ctx, data, -1, NULL);
	}
	else if (strcasecmp(what, second) == 0) {
		pos = strrchr(data, delim);
		if (pos != NULL)
			sqlite3_result_text(ctx, pos + 1, -1, NULL);
		else
			sqlite3_result_text(ctx, data, -1, NULL);
	}
	else {
		sqlite3_result_error(ctx, "SQL function split_*() called "
		    "with invalid arguments.\n", -1);
	}
}

void
pkgdb_split_version(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
	pkgdb_split_common(ctx, argc, argv, '-', "name", "version");
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
	if (argc != 0) {
		sqlite3_result_error(ctx, "Invalid usage of now() "
		    "no arguments expected\n", -1);
		return;
	}

	sqlite3_result_int64(ctx, (int64_t)time(NULL));
}

void
pkgdb_myarch(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
	const unsigned char	*arch = NULL;
	char			 myarch[BUFSIZ];

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
	"BEGIN;"
	"CREATE TABLE packages ("
		"id INTEGER PRIMARY KEY,"
		"origin TEXT NOT NULL,"
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
		"locked INTEGER NOT NULL DEFAULT 0,"
		"licenselogic INTEGER NOT NULL,"
		"time INTEGER, "
		"manifestdigest TEXT NULL, "
		"pkg_format_version INTEGER"
	");"
	"CREATE UNIQUE INDEX packages_unique ON packages(name);"
	"CREATE TABLE mtree ("
		"id INTEGER PRIMARY KEY,"
		"content TEXT NOT NULL UNIQUE"
	");"
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
                "package_id INTERGER REFERENCES packages(id)"
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

	/* FTS search table */

	"CREATE VIRTUAL TABLE pkg_search USING fts4(id, name, origin);"

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

	"CREATE VIEW pkg_shlibs AS SELECT * FROM pkg_shlibs_required;"
	"CREATE TRIGGER pkg_shlibs_update "
		"INSTEAD OF UPDATE ON pkg_shlibs "
	"FOR EACH ROW BEGIN "
		"UPDATE pkg_shlibs_required "
		"SET package_id = new.package_id, "
		"  shlib_id = new.shlib_id "
		"WHERE shlib_id = old.shlib_id "
		"AND package_id = old.package_id; "
	"END;"
	"CREATE TRIGGER pkg_shlibs_insert "
		"INSTEAD OF INSERT ON pkg_shlibs "
	"FOR EACH ROW BEGIN "
		"INSERT INTO pkg_shlibs_required (shlib_id, package_id) "
		"VALUES (new.shlib_id, new.package_id); "
	"END;"
	"CREATE TRIGGER pkg_shlibs_delete "
		"INSTEAD OF DELETE ON pkg_shlibs "
	"FOR EACH ROW BEGIN "
		"DELETE FROM pkg_shlibs_required "
                "WHERE shlib_id = old.shlib_id "
		"AND package_id = old.package_id; "
	"END;"

	"CREATE VIEW scripts AS SELECT package_id, script, type"
                " FROM pkg_script ps JOIN script s"
                " ON (ps.script_id = s.script_id);"
        "CREATE TRIGGER scripts_update"
                " INSTEAD OF UPDATE ON scripts "
        "FOR EACH ROW BEGIN"
                " INSERT OR IGNORE INTO script(script)"
                " VALUES(new.script);"
	        " UPDATE pkg_script"
                " SET package_id = new.package_id,"
                        " type = new.type,"
	                " script_id = ( SELECT script_id"
	                " FROM script WHERE script = new.script )"
                " WHERE package_id = old.package_id"
                        " AND type = old.type;"
        "END;"
        "CREATE TRIGGER scripts_insert"
                " INSTEAD OF INSERT ON scripts "
        "FOR EACH ROW BEGIN"
                " INSERT OR IGNORE INTO script(script)"
                " VALUES(new.script);"
	        " INSERT INTO pkg_script(package_id, type, script_id) "
	        " SELECT new.package_id, new.type, s.script_id"
                " FROM script s WHERE new.script = s.script;"
	"END;"
	"CREATE TRIGGER scripts_delete"
	        " INSTEAD OF DELETE ON scripts "
        "FOR EACH ROW BEGIN"
                " DELETE FROM pkg_script"
                " WHERE package_id = old.package_id"
                " AND type = old.type;"
                " DELETE FROM script"
                " WHERE script_id NOT IN"
                         " (SELECT DISTINCT script_id FROM pkg_script);"
	"END;"
	"CREATE VIEW options AS "
		"SELECT package_id, option, value "
		"FROM pkg_option JOIN option USING(option_id);"
	"CREATE TRIGGER options_update "
		"INSTEAD OF UPDATE ON options "
	"FOR EACH ROW BEGIN "
		"UPDATE pkg_option "
		"SET value = new.value "
		"WHERE package_id = old.package_id AND "
			"option_id = ( SELECT option_id FROM option "
				      "WHERE option = old.option );"
	"END;"
	"CREATE TRIGGER options_insert "
		"INSTEAD OF INSERT ON options "
	"FOR EACH ROW BEGIN "
		"INSERT OR IGNORE INTO option(option) "
		"VALUES(new.option);"
		"INSERT INTO pkg_option(package_id, option_id, value) "
		"VALUES (new.package_id, "
			"(SELECT option_id FROM option "
			"WHERE option = new.option), "
			"new.value);"
	"END;"
	"CREATE TRIGGER options_delete "
		"INSTEAD OF DELETE ON options "
	"FOR EACH ROW BEGIN "
		"DELETE FROM pkg_option "
		"WHERE package_id = old.package_id AND "
			"option_id = ( SELECT option_id FROM option "
					"WHERE option = old.option );"
		"DELETE FROM option "
		"WHERE option_id NOT IN "
			"( SELECT DISTINCT option_id FROM pkg_option );"
	"END;"

	"PRAGMA user_version = %d;"
	"COMMIT;"
	;

	return (sql_exec(sdb, sql, DBVERSION));
}

static int
pkgdb_is_insecure_mode(const char *path, bool install_as_user)
{
	uid_t		fileowner;
	gid_t		filegroup;
	bool		bad_perms = false;
	bool		wrong_owner = false;
	struct stat	sb;

	if (install_as_user) {
		fileowner = geteuid();
		filegroup = getegid();
	} else {
		fileowner = 0;
		filegroup = 0;
	}

	if (stat(path, &sb) != 0) {
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
pkgdb_check_access(unsigned mode, const char* dbdir, const char *dbname)
{
	char		 dbpath[MAXPATHLEN];
	int		 retval;
	bool		 database_exists;
	bool		 install_as_user;

	if (dbname != NULL)
		snprintf(dbpath, sizeof(dbpath), "%s/%s", dbdir, dbname);
	else
		strlcpy(dbpath, dbdir, sizeof(dbpath));

	install_as_user = (getenv("INSTALL_AS_USER") != NULL);

	retval = pkgdb_is_insecure_mode(dbpath, install_as_user);

	database_exists = (retval != EPKG_ENODB);

	if (database_exists && retval != EPKG_OK)
		return (retval);

	if (!database_exists && (mode & PKGDB_MODE_CREATE) != 0)
		return (EPKG_OK);

	switch(mode & (PKGDB_MODE_READ|PKGDB_MODE_WRITE)) {
	case 0:		/* Existence test */
		retval = eaccess(dbpath, F_OK);
		break;
	case PKGDB_MODE_READ:
		retval = eaccess(dbpath, R_OK);
		break;
	case PKGDB_MODE_WRITE:
		retval = eaccess(dbpath, W_OK);
		if (retval != 0 && errno == ENOENT) {
			mkdirs(dbdir);
			retval = eaccess(dbpath, W_OK);
		}
		break;
	case PKGDB_MODE_READ|PKGDB_MODE_WRITE:
		retval = eaccess(dbpath, R_OK|W_OK);
		if (retval != 0 && errno == ENOENT) {
			mkdirs(dbdir);
			retval = eaccess(dbpath, W_OK);
		}
		break;
	}

	if (retval != 0) {
		if (errno == ENOENT)
			return (EPKG_ENODB);
		else if (errno == EACCES)
			return (EPKG_ENOACCESS);
		else
			return (EPKG_FATAL);
	}
 
	return (EPKG_OK);
}


int
pkgdb_access(unsigned mode, unsigned database)
{
	const pkg_object	*o;
	const char		*dbdir;
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

	o = pkg_config_get("PKG_DBDIR");
	dbdir = pkg_object_string(o);
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
					 dbdir, NULL);
	} else
		retval = pkgdb_check_access(PKGDB_MODE_READ, dbdir, NULL);
	if (retval != EPKG_OK)
		return (retval);

	/* Test local.sqlite, if required */

	if ((database & PKGDB_DB_LOCAL) != 0) {
		retval = pkgdb_check_access(mode, dbdir, "local.sqlite");
		if (retval != EPKG_OK)
			return (retval);
	}

	if ((database & PKGDB_DB_REPO) != 0) {
		struct pkg_repo	*r = NULL;

		while (pkg_repos(&r) == EPKG_OK) {
			/* Ignore any repos marked as inactive */
			if (!pkg_repo_enabled(r))
				continue;

			retval = r->ops->access(r, mode);
			if (retval != EPKG_OK) {
				if (retval == EPKG_ENODB &&
				    mode == PKGDB_MODE_READ)
					pkg_emit_error("Repository %s missing."
					    " 'pkg update' required", r->name);
				return (retval);
			}
		}
	}
	return (retval);
}

static void
pkgdb_profile_callback(void *ud, const char *req, sqlite3_uint64 nsec)
{
	/* According to sqlite3 documentation, nsec has milliseconds accuracy */
	nsec /= 1000000LLU;
	if (nsec > 0)
		pkg_debug(1, "Sqlite request %s was executed in %lu milliseconds",
			req, (unsigned long)nsec);
}

int
pkgdb_open(struct pkgdb **db_p, pkgdb_t type)
{
	return (pkgdb_open_all(db_p, type, NULL));
}

static int
pkgdb_open_repos(struct pkgdb *db, const char *reponame)
{
	struct pkg_repo *r = NULL;
	struct _pkg_repo_list_item *item;

	while (pkg_repos(&r) == EPKG_OK) {
		if (reponame == NULL || strcasecmp(r->name, reponame) == 0) {
			/* We need read only access here */
			if (r->ops->open(r, R_OK) == EPKG_OK) {
				item = malloc(sizeof(*item));
				if (item == NULL) {
					pkg_emit_errno("malloc", "_pkg_repo_list_item");
					return (EPKG_FATAL);
				}

				r->ops->init(r);
				item->repo = r;
				LL_PREPEND(db->repos, item);
			} else
				pkg_emit_error("Repository %s cannot be opened."
				    " 'pkg update' required", r->name);
		}
	}

	return (EPKG_OK);
}

int
pkgdb_open_all(struct pkgdb **db_p, pkgdb_t type, const char *reponame)
{
	struct pkgdb	*db = NULL;
	struct statfs	 stfs;
	bool		 reopen = false;
	bool		 profile = false;
	char		 localpath[MAXPATHLEN];
	const char	*dbdir;
	bool		 create = false;
	bool		 createdir = false;
	int		 ret;

	if (*db_p != NULL) {
		reopen = true;
		db = *db_p;
	}

	dbdir = pkg_object_string(pkg_config_get("PKG_DBDIR"));
	if (!reopen && (db = calloc(1, sizeof(struct pkgdb))) == NULL) {
		pkg_emit_errno("malloc", "pkgdb");
		return (EPKG_FATAL);
	}

	db->prstmt_initialized = false;

	if (!reopen) {
		snprintf(localpath, sizeof(localpath), "%s/local.sqlite", dbdir);

		if (eaccess(localpath, R_OK) != 0) {
			if (errno != ENOENT) {
				pkg_emit_nolocaldb();
				pkgdb_close(db);
				return (EPKG_ENODB);
			} else if ((eaccess(dbdir, W_OK) != 0)) {
				/*
				 * If we need to create the db but cannot
				 * write to it, fail early
				 */
				if (errno == ENOENT) {
					createdir = true;
					create = true;
				} else {
					pkg_emit_nolocaldb();
					pkgdb_close(db);
					return (EPKG_ENODB);
				}
			} else {
				create = true;
			}
		}

		/* Create the directory if it doesn't exist */
		if (createdir && mkdirs(dbdir) != EPKG_OK) {
			pkgdb_close(db);
			return (EPKG_FATAL);
		}

		sqlite3_initialize();

#ifdef MNT_LOCAL
		/*
		 * Fall back on unix-dotfile locking strategy if on a network filesystem
		 */
		if (statfs(dbdir, &stfs) == 0) {
			if ((stfs.f_flags & MNT_LOCAL) != MNT_LOCAL)
				sqlite3_vfs_register(sqlite3_vfs_find("unix-dotfile"), 1);
		}
#endif
		if (sqlite3_open(localpath, &db->sqlite) != SQLITE_OK) {
			ERROR_SQLITE(db->sqlite, "sqlite open");
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
	}

	if (type == PKGDB_REMOTE || type == PKGDB_MAYBE_REMOTE) {
		if (reponame != NULL || pkg_repos_activated_count() > 0) {
			ret = pkgdb_open_repos(db, reponame);
			if (ret != EPKG_OK)
				return (ret);
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
		pkg_debug(1, "pkgdb profiling is enabled");
		sqlite3_profile(db->sqlite, pkgdb_profile_callback, NULL);
	}

	*db_p = db;
	return (EPKG_OK);
}

void
pkgdb_close(struct pkgdb *db)
{
	struct _pkg_repo_list_item *cur, *tmp;

	if (db == NULL)
		return;

	if (db->prstmt_initialized)
		prstmt_finalize(db);

	if (db->sqlite != NULL) {

		LL_FOREACH_SAFE(db->repos, cur, tmp) {
			cur->repo->ops->close(cur->repo, false);
			free(cur);
		}

		if (!sqlite3_db_readonly(db->sqlite, "main"))
			pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PKGDB_CLOSE_RW, NULL, db);

		sqlite3_close(db->sqlite);
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

int
pkgdb_transaction_begin_sqlite(sqlite3 *sqlite, const char *savepoint)
{
	int		 ret;
	sqlite3_stmt	*stmt;
	const char *psql;

	assert(sqlite != NULL);

	if (savepoint == NULL || savepoint[0] == '\0') {
		const char sql[] = "BEGIN IMMEDIATE TRANSACTION";
		
		psql = sql;
		pkg_debug(4, "Pkgdb: running '%s'", sql);
		ret = sqlite3_prepare_v2(sqlite, sql, strlen(sql) + 1,
					 &stmt, NULL);
	} else {
		char sql[128] = "SAVEPOINT ";

		strlcat(sql, savepoint, sizeof(sql));

		psql = sql;
		pkg_debug(4, "Pkgdb: running '%s'", sql);
		ret = sqlite3_prepare_v2(sqlite, sql, strlen(sql) + 1,
					 &stmt, NULL);
	}

	if (ret == SQLITE_OK)
		PKGDB_SQLITE_RETRY_ON_BUSY(ret)
			ret = sqlite3_step(stmt);

	sqlite3_finalize(stmt);

	if (ret != SQLITE_OK && ret != SQLITE_DONE)
		ERROR_SQLITE(sqlite, psql);

	return (ret == SQLITE_OK || ret == SQLITE_DONE ? EPKG_OK : EPKG_FATAL);
}

int
pkgdb_transaction_commit_sqlite(sqlite3 *sqlite, const char *savepoint)
{
	int		 ret;
	sqlite3_stmt	*stmt;
	const char *psql;

	assert(sqlite != NULL);

	if (savepoint == NULL || savepoint[0] == '\0') {
		const char sql[] = "COMMIT TRANSACTION";
		psql = sql;

		pkg_debug(4, "Pkgdb: running '%s'", sql);
		ret = sqlite3_prepare_v2(sqlite, sql, strlen(sql) + 1,
					 &stmt, NULL);
	} else {
		char sql[128] = "RELEASE SAVEPOINT ";

		strlcat(sql, savepoint, sizeof(sql));

		psql = sql;

		pkg_debug(4, "Pkgdb: running '%s'", sql);
		ret = sqlite3_prepare_v2(sqlite, sql, strlen(sql) + 1,
					 &stmt, NULL);
	}

	if (ret == SQLITE_OK)
		PKGDB_SQLITE_RETRY_ON_BUSY(ret)
			ret = sqlite3_step(stmt);

	sqlite3_finalize(stmt);

	if (ret != SQLITE_OK && ret != SQLITE_DONE)
		ERROR_SQLITE(sqlite, psql);

	return (ret == SQLITE_OK || ret == SQLITE_DONE ? EPKG_OK : EPKG_FATAL);
}

int
pkgdb_transaction_rollback_sqlite(sqlite3 *sqlite, const char *savepoint)
{
	int		 ret;
	sqlite3_stmt	*stmt;
	const char *psql;

	assert(sqlite != NULL);

	if (savepoint == NULL || savepoint[0] == '\0') {
		const char sql[] = "ROLLBACK TRANSACTION";

		psql = sql;
		pkg_debug(4, "Pkgdb: running '%s'", sql);
		ret = sqlite3_prepare_v2(sqlite, sql, strlen(sql) + 1,
					 &stmt, NULL);
	} else {
		char sql[128] = "ROLLBACK TO SAVEPOINT ";

		strlcat(sql, savepoint, sizeof(sql));

		psql = sql;
		pkg_debug(4, "Pkgdb: running '%s'", sql);
		ret = sqlite3_prepare_v2(sqlite, sql, strlen(sql) + 1,
					 &stmt, NULL);
	}

	if (ret == SQLITE_OK)
		PKGDB_SQLITE_RETRY_ON_BUSY(ret)
			ret = sqlite3_step(stmt);

	sqlite3_finalize(stmt);

	if (ret != SQLITE_OK && ret != SQLITE_DONE)
		ERROR_SQLITE(sqlite, psql);

	return (ret == SQLITE_OK || ret == SQLITE_DONE ? EPKG_OK : EPKG_FATAL);
}

/*
 * Public API
 */
int
pkgdb_transaction_begin(struct pkgdb *db, const char *savepoint)
{
	return (pkgdb_transaction_begin_sqlite(db->sqlite, savepoint));
}
int
pkgdb_transaction_commit(struct pkgdb *db, const char *savepoint)
{
	return (pkgdb_transaction_commit_sqlite(db->sqlite, savepoint));
}
int
pkgdb_transaction_rollback(struct pkgdb *db, const char *savepoint)
{
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
	ANNOTATE_DEL1,
	ANNOTATE_DEL2,
	CONFLICT,
	PKG_PROVIDE,
	PROVIDE,
	FTS_APPEND,
	UPDATE_DIGEST,
	CONFIG_FILES,
	UPDATE_CONFIG_FILE,
	PRSTMT_LAST,
} sql_prstmt_index;

static sql_prstmt sql_prepared_statements[PRSTMT_LAST] = {
	[MTREE] = {
		NULL,
		"INSERT OR IGNORE INTO mtree(content) VALUES(?1)",
		"T",
	},
	[PKG] = {
		NULL,
		"INSERT OR REPLACE INTO packages( "
			"origin, name, version, comment, desc, message, arch, "
			"maintainer, www, prefix, flatsize, automatic, "
			"licenselogic, mtree_id, time, manifestdigest) "
		"VALUES( ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, "
		"?13, (SELECT id FROM mtree WHERE content = ?14), NOW(), ?15)",
		"TTTTTTTTTTIIITT",
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
		"TTTT",
	},
	[ANNOTATE_DEL1] = {
		NULL,
		"DELETE FROM pkg_annotation WHERE "
		"package_id IN"
                " (SELECT id FROM packages WHERE name = ?1) "
		"AND tag_id IN"
		" (SELECT annotation_id FROM annotation WHERE annotation = ?2)",
		"TTT",
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
	[FTS_APPEND] = {
		NULL,
		"INSERT OR ROLLBACK INTO pkg_search(id, name, origin) "
		"VALUES (?1, ?2 || '-' || ?3, ?4);",
		"ITTT"
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
	}
	/* PRSTMT_LAST */
};

static int
prstmt_initialize(struct pkgdb *db)
{
	sql_prstmt_index	 i;
	sqlite3			*sqlite;
	int			 ret;

	assert(db != NULL);

	if (!db->prstmt_initialized) {
		sqlite = db->sqlite;

		for (i = 0; i < PRSTMT_LAST; i++)
		{
			pkg_debug(4, "Pkgdb: preparing statement '%s'", SQL(i));
			ret = sqlite3_prepare_v2(sqlite, SQL(i), -1, &STMT(i), NULL);
			if (ret != SQLITE_OK) {
				ERROR_SQLITE(sqlite, SQL(i));
				return (EPKG_FATAL);
			}
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

int
pkgdb_register_pkg(struct pkgdb *db, struct pkg *pkg, int complete, int forced)
{
	struct pkg		*pkg2 = NULL;
	struct pkg_strel	*el;
	struct pkg_dep		*dep = NULL;
	struct pkg_file		*file = NULL;
	struct pkg_dir		*dir = NULL;
	struct pkg_option	*option = NULL;
	struct pkg_user		*user = NULL;
	struct pkg_group	*group = NULL;
	struct pkg_conflict	*conflict = NULL;
	struct pkg_config_file	*cf = NULL;
	struct pkgdb_it		*it = NULL;

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

	if (!complete && pkgdb_transaction_begin_sqlite(s, NULL) != EPKG_OK)
		return (EPKG_FATAL);

	/* Prefer new ABI over old one */
	arch = pkg->abi != NULL ? pkg->abi : pkg->arch;

	/*
	 * Insert package record
	 */
	ret = run_prstmt(PKG, pkg->origin, pkg->name, pkg->version,
	    pkg->comment, pkg->desc, pkg->message, arch, pkg->maintainer,
	    pkg->www, pkg->prefix, pkg->flatsize, (int64_t)pkg->automatic,
	    (int64_t)pkg->licenselogic, NULL, pkg->digest);
	if (ret != SQLITE_DONE) {
		ERROR_SQLITE(s, SQL(PKG));
		goto cleanup;
	}

	package_id = sqlite3_last_insert_rowid(s);

	if (run_prstmt(FTS_APPEND, package_id, pkg->name, pkg->version,
	    pkg->origin) != SQLITE_DONE) {
		ERROR_SQLITE(s, SQL(FTS_APPEND));
		goto cleanup;
	}

	/*
	 * update dep informations on packages that depends on the insert
	 * package
	 */

	if (run_prstmt(DEPS_UPDATE, pkg->origin, pkg->version, pkg->name)
	    != SQLITE_DONE) {
		ERROR_SQLITE(s, SQL(DEPS_UPDATE));
		goto cleanup;
	}

	/*
	 * Insert dependencies list
	 */

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		if (run_prstmt(DEPS, dep->origin, dep->name, dep->version,
		    package_id) != SQLITE_DONE) {
			ERROR_SQLITE(s, SQL(DEPS));
			goto cleanup;
		}
	}

	/*
	 * Insert files.
	 */

	while (pkg_files(pkg, &file) == EPKG_OK) {
		bool		permissive = false;
		bool		devmode = false;

		ret = run_prstmt(FILES, file->path, file->sum, package_id);
		if (ret == SQLITE_DONE)
			continue;
		if (ret != SQLITE_CONSTRAINT) {
			ERROR_SQLITE(s, SQL(FILES));
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
				ERROR_SQLITE(s, SQL(FILES_REPLACE));
				goto cleanup;
			}
		}
		if (ret != EPKG_OK && ret != EPKG_END) {
			pkgdb_it_free(it);
			ERROR_SQLITE(s, SQL(FILES_REPLACE));
			goto cleanup;
		}
		if (!forced) {
			devmode = pkg_object_bool(pkg_config_get("DEVELOPER_MODE"));
			if (!devmode)
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
				ERROR_SQLITE(s, SQL(CONFIG_FILES));
			goto cleanup;
		}
	}

	/*
	 * Insert dirs.
	 */

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		if (run_prstmt(DIRS1, dir->path) != SQLITE_DONE) {
			ERROR_SQLITE(s, SQL(DIRS1));
			goto cleanup;
		}
		if ((ret = run_prstmt(DIRS2, package_id, dir->path,
		    true)) != SQLITE_DONE) {
			if (ret == SQLITE_CONSTRAINT) {
				pkg_emit_error("Another package is already "
				    "providing directory: %s",
				    dir->path);
			} else
				ERROR_SQLITE(s, SQL(DIRS2));
			goto cleanup;
		}
	}

	/*
	 * Insert categories
	 */

	LL_FOREACH(pkg->categories, el) {
		ret = run_prstmt(CATEGORY1, el->value);
		if (ret == SQLITE_DONE)
			ret = run_prstmt(CATEGORY2, package_id, el->value);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(s, SQL(CATEGORY2));
			goto cleanup;
		}
	}

	/*
	 * Insert licenses
	 */

	LL_FOREACH(pkg->licenses, el) {
		if (run_prstmt(LICENSES1, el->value)
		    != SQLITE_DONE
		    ||
		    run_prstmt(LICENSES2, package_id, el->value)
		    != SQLITE_DONE) {
			ERROR_SQLITE(s, SQL(LICENSES2));
			goto cleanup;
		}
	}

	/*
	 * Insert users
	 */

	while (pkg_users(pkg, &user) == EPKG_OK) {
		if (run_prstmt(USERS1, user->name)
		    != SQLITE_DONE
		    ||
		    run_prstmt(USERS2, package_id, user->name)
		    != SQLITE_DONE) {
			ERROR_SQLITE(s, SQL(USERS2));
			goto cleanup;
		}
	}

	/*
	 * Insert groups
	 */

	while (pkg_groups(pkg, &group) == EPKG_OK) {
		if (run_prstmt(GROUPS1, group->name)
		    != SQLITE_DONE
		    ||
		    run_prstmt(GROUPS2, package_id, group->name)
		    != SQLITE_DONE) {
			ERROR_SQLITE(s, SQL(GROUPS2));
			goto cleanup;
		}
	}

	/*
	 * Insert scripts
	 */

	if (pkgdb_insert_scripts(pkg, package_id, s) != EPKG_OK)
		goto cleanup;

	/*
	 * Insert options
	 */

	while (pkg_options(pkg, &option) == EPKG_OK) {
		if (run_prstmt(OPTION1, option->key) != SQLITE_DONE
		    ||
		    run_prstmt(OPTION2, package_id, option->key, option->value)
			       != SQLITE_DONE) {
			ERROR_SQLITE(s, SQL(OPTION2));
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
			ERROR_SQLITE(s, SQL(CONFLICT));
			goto cleanup;
		}
	}

	/*
	 * Insert provides
	 */
	if (pkgdb_update_provides(pkg, package_id, s) != EPKG_OK)
		goto cleanup;

	retcode = EPKG_OK;

	cleanup:

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
			ERROR_SQLITE(s, SQL(SCRIPT2));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

int
pkgdb_update_shlibs_required(struct pkg *pkg, int64_t package_id, sqlite3 *s)
{
	struct pkg_shlib	*shlib = NULL;

	while (pkg_shlibs_required(pkg, &shlib) == EPKG_OK) {
		if (run_prstmt(SHLIBS1, shlib->name)
		    != SQLITE_DONE
		    ||
		    run_prstmt(SHLIBS_REQD, package_id, shlib->name)
		    != SQLITE_DONE) {
			ERROR_SQLITE(s, SQL(SHLIBS_REQD));
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
			ERROR_SQLITE(s, SQL(SHLIBS_REQD));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

int
pkgdb_update_shlibs_provided(struct pkg *pkg, int64_t package_id, sqlite3 *s)
{
	struct pkg_shlib	*shlib = NULL;

	while (pkg_shlibs_provided(pkg, &shlib) == EPKG_OK) {
		if (run_prstmt(SHLIBS1, shlib->name)
		    != SQLITE_DONE
		    ||
		    run_prstmt(SHLIBS_PROV, package_id, shlib->name)
		    != SQLITE_DONE) {
			ERROR_SQLITE(s, SQL(SHLIBS_PROV));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

int
pkgdb_update_provides(struct pkg *pkg, int64_t package_id, sqlite3 *s)
{
	struct pkg_provide	*provide = NULL;

	while (pkg_provides(pkg, &provide) == EPKG_OK) {
		if (run_prstmt(PROVIDE, provide->provide)
		    != SQLITE_DONE
		    ||
		    run_prstmt(PKG_PROVIDE, package_id, provide->provide)
		    != SQLITE_DONE) {
			ERROR_SQLITE(s, SQL(PKG_PROVIDE));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

int
pkgdb_insert_annotations(struct pkg *pkg, int64_t package_id, sqlite3 *s)
{
	struct pkg_kv	*kv;

	LL_FOREACH(pkg->annotations, kv) {
		if (run_prstmt(ANNOTATE1, kv->key)
		    != SQLITE_DONE
		    ||
		    run_prstmt(ANNOTATE1,kv->value)
		    != SQLITE_DONE
		    ||
		    run_prstmt(ANNOTATE2, package_id,
			kv->key, kv->value)
		    != SQLITE_DONE) {
			ERROR_SQLITE(s, SQL(ANNOTATE2));
			return (EPKG_FATAL);
		}
	}
	return (EPKG_OK);
}

int
pkgdb_reanalyse_shlibs(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3		*s;
	int64_t		 package_id;
	int		 ret = EPKG_OK;
	int		 i;
	const char	*sql[] = {
		"DELETE FROM pkg_shlibs_required WHERE package_id = ?1",

		"DELETE FROM pkg_shlibs_provided WHERE package_id = ?1",

		"DELETE FROM shlibs "
		"WHERE id NOT IN "
		"(SELECT DISTINCT shlib_id FROM pkg_shlibs_required)" 
		"AND id NOT IN "
		"(SELECT DISTINCT shlib_id FROM pkg_shlibs_provided)",
	};

	sqlite3_stmt	*stmt_del;

	assert(db != NULL);

	if (pkg_is_valid(pkg) != EPKG_OK) {
		pkg_emit_error("the package is not valid");
		return (EPKG_FATAL);
	}

	if ((ret = pkg_analyse_files(db, pkg, NULL)) == EPKG_OK) {
		s = db->sqlite;
		package_id = pkg->id;

		for (i = 0; i < 2; i++) {
			/* Clean out old shlibs first */
			pkg_debug(4, "Pkgdb: running '%s'", sql[i]);
			if (sqlite3_prepare_v2(db->sqlite, sql[i], -1,
					       &stmt_del, NULL)
			    != SQLITE_OK) {
				ERROR_SQLITE(db->sqlite, sql[i]);
				return (EPKG_FATAL);
			}

			sqlite3_bind_int64(stmt_del, 1, package_id);

			ret = sqlite3_step(stmt_del);
			sqlite3_finalize(stmt_del);

			if (ret != SQLITE_DONE) {
				ERROR_SQLITE(db->sqlite, sql[i]);
				return (EPKG_FATAL);
			}
		}

		if (sql_exec(db->sqlite, sql[2]) != EPKG_OK)
			return (EPKG_FATAL);

		/* Save shlibs */
		ret = pkgdb_update_shlibs_required(pkg, package_id, s);
		if (ret == EPKG_OK)
			ret = pkgdb_update_shlibs_provided(pkg, package_id, s);
	}

	return (ret);
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
		ERROR_SQLITE(db->sqlite, SQL(ANNOTATE_ADD1));
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
		ERROR_SQLITE(db->sqlite, SQL(UPDATE_DIGEST));
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

	if (run_prstmt(ANNOTATE_DEL1, pkg->uid, tag) != SQLITE_DONE
	    ||
	    run_prstmt(ANNOTATE1, tag) != SQLITE_DONE
	    ||
	    run_prstmt(ANNOTATE1, value) != SQLITE_DONE
	    ||
	    run_prstmt(ANNOTATE_ADD1, pkg->uid, tag, value) !=
	        SQLITE_DONE
	    ||
	    run_prstmt(ANNOTATE_DEL2) != SQLITE_DONE) {
		ERROR_SQLITE(db->sqlite, SQL(ANNOTATE_DEL2));
		pkgdb_transaction_rollback_sqlite(db->sqlite, NULL);

		return (EPKG_FATAL);
	}

	/* Something has gone very wrong if rows_changed != 1 here */

	rows_changed = sqlite3_changes(db->sqlite);

	if (pkgdb_transaction_commit_sqlite(db->sqlite, NULL) != EPKG_OK)
		return (EPKG_FATAL);

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
		ERROR_SQLITE(db->sqlite, SQL(ANNOTATE_DEL2));
		pkgdb_transaction_rollback_sqlite(db->sqlite, NULL);
		return (EPKG_FATAL);
	}

	if (pkgdb_transaction_commit_sqlite(db->sqlite, NULL) != EPKG_OK)
		return (EPKG_FATAL);

	return (rows_changed == 1 ? EPKG_OK : EPKG_WARN);
}


int
pkgdb_register_finale(struct pkgdb *db, int retcode)
{
	int	ret = EPKG_OK;

	assert(db != NULL);

	if (retcode == EPKG_OK) 
		ret = pkgdb_transaction_commit_sqlite(db->sqlite, NULL);
	else
		ret = pkgdb_transaction_rollback_sqlite(db->sqlite, NULL);

	return (ret);
}

int
pkgdb_register_ports(struct pkgdb *db, struct pkg *pkg)
{
	int	ret;

	pkg_emit_install_begin(pkg);

	ret = pkgdb_register_pkg(db, pkg, 0, 0);
	if (ret == EPKG_OK)
		pkg_emit_install_finished(pkg);

	pkgdb_register_finale(db, ret);

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
		"mtree WHERE id NOT IN "
			"(SELECT DISTINCT mtree_id FROM packages)",
		/* TODO print the users that are not used anymore */
		"users WHERE id NOT IN "
			"(SELECT DISTINCT user_id FROM pkg_users)",
		/* TODO print the groups trhat are not used anymore */
		"groups WHERE id NOT IN "
			"(SELECT DISTINCT group_id FROM pkg_groups)",
		"shlibs WHERE id NOT IN "
			"(SELECT DISTINCT shlib_id FROM pkg_shlibs_required)"
			"AND id NOT IN "
			"(SELECT DISTINCT shlib_id FROM pkg_shlibs_provided)",
		"script WHERE script_id NOT IN "
		        "(SELECT DISTINCT script_id FROM pkg_script)",
	};

	assert(db != NULL);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt_del, NULL)
	    != SQLITE_OK){
		ERROR_SQLITE(db->sqlite, sql);
		return (EPKG_FATAL);
	}

	sqlite3_bind_int64(stmt_del, 1, id);

	ret = sqlite3_step(stmt_del);
	sqlite3_finalize(stmt_del);

	if (ret != SQLITE_DONE) {
		ERROR_SQLITE(db->sqlite, sql);
		return (EPKG_FATAL);
	}

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

	pkg_debug(4, "Pkgdb: executing '%s'", sql_to_exec);
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

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(s, sql, -1, &stmt, NULL) != SQLITE_OK) {
		if (!silence)
			ERROR_SQLITE(s, sql);
		return (EPKG_OK);
	}

	PKGDB_SQLITE_RETRY_ON_BUSY(ret)
		ret = sqlite3_step(stmt);

	if (ret == SQLITE_ROW)
		*res = sqlite3_column_int64(stmt, 0);

	sqlite3_finalize(stmt);

	if (ret != SQLITE_ROW) {
		if (!silence)
			ERROR_SQLITE(s, sql);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

int
get_sql_string(sqlite3 *s, const char *sql, char **res)
{
	sqlite3_stmt	*stmt;
	int		 ret;

	assert(s != NULL && sql != NULL);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(s, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s, sql);
		return (EPKG_OK);
	}

	ret = sqlite3_step(stmt);

	if (ret == SQLITE_ROW) {
		const unsigned char *tmp;
		tmp = sqlite3_column_text(stmt, 0);
		*res = (tmp == NULL ? NULL : strdup(tmp));
	}

	if (ret == SQLITE_DONE)
		*res = NULL;

	sqlite3_finalize(stmt);

	if (ret != SQLITE_ROW && ret != SQLITE_DONE) {
		ERROR_SQLITE(s, sql);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
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

	if (freelist_count / (float)page_count < 0.25)
		return (EPKG_OK);

	return (sql_exec(db->sqlite, "VACUUM;"));
}

struct pkgdb_it *
pkgdb_integrity_conflict_local(struct pkgdb *db, const char *uniqueid)
{
	sqlite3_stmt	*stmt;
	int		 ret;

	assert(db != NULL && uniqueid != NULL);

	const char	sql_conflicts [] = ""
		"SELECT DISTINCT p.id AS rowid, p.origin, p.name, p.version, "
		    "p.prefix "
		"FROM packages AS p, files AS f, integritycheck AS i "
		"WHERE p.id = f.package_id AND f.path = i.path "
		"AND i.uid = ?1 AND "
		"i.uid != p.name" ;

	pkg_debug(4, "Pkgdb: running '%s'", sql_conflicts);
	ret = sqlite3_prepare_v2(db->sqlite, sql_conflicts, -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sql_conflicts);
		return (NULL);
	}

	sqlite3_bind_text(stmt, 1, uniqueid, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new_sqlite(db, stmt, PKG_INSTALLED, PKGDB_IT_FLAG_ONCE));
}

static int
pkgdb_vset(struct pkgdb *db, int64_t id, va_list ap)
{
	int		 attr;
	sqlite3_stmt	*stmt;
	int64_t		 flatsize;
	bool automatic, locked;
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
	};

	while ((attr = va_arg(ap, int)) > 0) {
		pkg_debug(4, "Pkgdb: running '%s'", sql[attr]);
		if (sqlite3_prepare_v2(db->sqlite, sql[attr], -1, &stmt, NULL)
		    != SQLITE_OK) {
			ERROR_SQLITE(db->sqlite, sql[attr]);
			return (EPKG_FATAL);
		}

		switch (attr) {
		case PKG_SET_FLATSIZE:
			flatsize = va_arg(ap, int64_t);
			sqlite3_bind_int64(stmt, 1, flatsize);
			sqlite3_bind_int64(stmt, 2, id);
			break;
		case PKG_SET_AUTOMATIC:
			automatic = (bool)va_arg(ap, int);
			if (automatic != 0 && automatic != 1) {
				sqlite3_finalize(stmt);
				continue;
			}
			sqlite3_bind_int64(stmt, 1, automatic);
			sqlite3_bind_int64(stmt, 2, id);
			break;
		case PKG_SET_LOCKED:
			locked = (bool)va_arg(ap, int);
			if (locked != 0 && locked != 1)
				continue;
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
		}

		if (sqlite3_step(stmt) != SQLITE_DONE) {
			ERROR_SQLITE(db->sqlite, sql[attr]);
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
		     const char *sha256)
{
	sqlite3_stmt	*stmt = NULL;
	const char	 sql_file_update[] = ""
		"UPDATE files SET sha256 = ?1 WHERE path = ?2";
	int		 ret;

	pkg_debug(4, "Pkgdb: running '%s'", sql_file_update);
	ret = sqlite3_prepare_v2(db->sqlite, sql_file_update, -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sql_file_update);
		return (EPKG_FATAL);
	}
	sqlite3_bind_text(stmt, 1, sha256, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, file->path, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		ERROR_SQLITE(db->sqlite, sql_file_update);
		sqlite3_finalize(stmt);
		return (EPKG_FATAL);
	}
	sqlite3_finalize(stmt);
	strlcpy(file->sum, sha256, sizeof(file->sum));

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
	sqlite3_create_function(db, "now", 0, SQLITE_ANY, NULL,
	    pkgdb_now, NULL, NULL);
	sqlite3_create_function(db, "myarch", 0, SQLITE_ANY, NULL,
	    pkgdb_myarch, NULL, NULL);
	sqlite3_create_function(db, "myarch", 1, SQLITE_ANY, NULL,
	    pkgdb_myarch, NULL, NULL);
	sqlite3_create_function(db, "regexp", 2, SQLITE_ANY, NULL,
	    pkgdb_regex, NULL, NULL);
	sqlite3_create_function(db, "split_version", 2, SQLITE_ANY, NULL,
	    pkgdb_split_version, NULL, NULL);


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
	char		 localpath[MAXPATHLEN];
	const char	*dbdir;

	sqlite3_auto_extension((void(*)(void))pkgdb_sqlcmd_init);

	dbdir = pkg_object_string(pkg_config_get("PKG_DBDIR"));

	snprintf(localpath, sizeof(localpath), "%s/local.sqlite", dbdir);
	*reponame = strdup(localpath);
}

static int
pkgdb_write_lock_pid(struct pkgdb *db)
{
	const char lock_pid_sql[] = ""
			"INSERT INTO pkg_lock_pid VALUES (?1);";
	sqlite3_stmt	*stmt = NULL;
	int ret;

	ret = sqlite3_prepare_v2(db->sqlite, lock_pid_sql, -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, lock_pid_sql);
		return (EPKG_FATAL);
	}
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
	int ret;

	ret = sqlite3_prepare_v2(db->sqlite, lock_pid_sql, -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, lock_pid_sql);
		return (EPKG_FATAL);
	}
	sqlite3_bind_int64(stmt, 1, pid);

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		ERROR_SQLITE(db->sqlite, lock_pid_sql);
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
	int ret, found = 0;
	int64_t pid, lpid;
	const char query[] = "SELECT pid FROM pkg_lock_pid;";

	ret = sqlite3_prepare_v2(db->sqlite, query, -1,
			&stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, query);
		return (EPKG_FATAL);
	}

	lpid = getpid();

	while (sqlite3_step(stmt) != SQLITE_DONE) {
		pid = sqlite3_column_int64(stmt, 0);
		if (pid != lpid) {
			if (kill((pid_t)pid, 0) == -1) {
				pkg_debug(1, "found stale pid %lld in lock database, my pid is: %lld",
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
	int64_t num_timeout = 1, num_maxtries = 1;

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
				pkg_debug(1, "want read lock but cannot write to database, "
						"slightly ignore this error for now");
				return (EPKG_OK);
			}
			return (EPKG_FATAL);
		}

		ret = EPKG_END;
		if (sqlite3_changes(db->sqlite) == 0) {
			if (pkgdb_check_lock_pid(db) == EPKG_END) {
				/* No live processes found, so we can safely reset lock */
				pkg_debug(1, "no concurrent processes found, cleanup the lock");
				pkgdb_reset_lock(db);
				if (upgrade) {
					/*
					 * In case of upgrade we should obtain a lock from the beginning,
					 * hence switch upgrade to retain
					 */
					pkgdb_remove_lock_pid(db, (int64_t)getpid());
					return pkgdb_obtain_lock(db, type);
				}
				continue;
			}
			else if (num_timeout > 0) {
				ts.tv_sec = (int)num_timeout;
				ts.tv_nsec = (num_timeout - (int)num_timeout) * 1000000000.;
				pkg_debug(1, "waiting for database lock for %d times, "
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
		pkg_debug(1, "want to get a read only lock on a database");
		break;
	case PKGDB_LOCK_ADVISORY:
		lock_sql = advisory_lock_sql;
		pkg_debug(1, "want to get an advisory lock on a database");
		break;
	case PKGDB_LOCK_EXCLUSIVE:
		pkg_debug(1, "want to get an exclusive lock on a database");
		lock_sql = exclusive_lock_sql;
		break;
	}

	ret = pkgdb_try_lock(db, lock_sql, type, false);

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
		pkg_debug(1, "want to upgrade advisory to exclusive lock");
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
		pkg_debug(1, "want to downgrade exclusive to advisory lock");
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
		pkg_debug(1, "release a read only lock on a database");

		break;
	case PKGDB_LOCK_ADVISORY:
		unlock_sql = advisory_unlock_sql;
		pkg_debug(1, "release an advisory lock on a database");
		break;
	case PKGDB_LOCK_EXCLUSIVE:
		pkg_debug(1, "release an exclusive lock on a database");
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
	struct sbuf	*sql = NULL;
	int		 ret;
	struct _pkg_repo_list_item *rit;

	assert(db != NULL);

	sql = sbuf_new_auto();

	switch(type) {
	case PKG_STATS_LOCAL_COUNT:
		sbuf_printf(sql, "SELECT COUNT(id) FROM main.packages;");
		break;
	case PKG_STATS_LOCAL_SIZE:
		sbuf_printf(sql, "SELECT SUM(flatsize) FROM main.packages;");
		break;
	case PKG_STATS_REMOTE_UNIQUE:
	case PKG_STATS_REMOTE_COUNT:
	case PKG_STATS_REMOTE_SIZE:
		LL_FOREACH(db->repos, rit) {
			struct pkg_repo *repo = rit->repo;

			if (repo->ops->stat != NULL)
				stats += repo->ops->stat(repo, type);
		}
		goto remote;
		break;
	case PKG_STATS_REMOTE_REPOS:
		LL_FOREACH(db->repos, rit) {
			stats ++;
		}
		goto remote;
		break;
	}

	sbuf_finish(sql);
	pkg_debug(4, "Pkgdb: running '%s'", sbuf_data(sql));
	ret = sqlite3_prepare_v2(db->sqlite, sbuf_data(sql), -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sbuf_data(sql));
		sbuf_free(sql);
		return (-1);
	}

	while (sqlite3_step(stmt) != SQLITE_DONE) {
		stats = sqlite3_column_int64(stmt, 0);
	}

	sqlite3_finalize(stmt);

remote:
	sbuf_free(sql);

	return (stats);
}


int
pkgdb_begin_solver(struct pkgdb *db)
{
	const char solver_sql[] = ""
		"PRAGMA synchronous = OFF;"
		"PRAGMA journal_mode = MEMORY;"
		"BEGIN TRANSACTION;";
	const char update_digests_sql[] = ""
		"DROP INDEX IF EXISTS pkg_digest_id;"
		"BEGIN TRANSACTION;";
	const char end_update_sql[] = ""
		"END TRANSACTION;"
		"CREATE INDEX pkg_digest_id ON packages(origin, manifestdigest);";
	struct pkgdb_it *it;
	struct pkg *pkglist = NULL, *p = NULL;
	int rc = EPKG_OK;
	int64_t cnt = 0, cur = 0;

	it = pkgdb_query(db, " WHERE manifestdigest IS NULL OR manifestdigest==''",
		MATCH_CONDITION);
	if (it != NULL) {
		while (pkgdb_it_next(it, &p, PKG_LOAD_BASIC|PKG_LOAD_OPTIONS) == EPKG_OK) {
			pkg_checksum_calculate(p, NULL);
			LL_PREPEND(pkglist, p);
			p = NULL;
			cnt ++;
		}
		pkgdb_it_free(it);

		if (pkglist != NULL) {
			rc = sql_exec(db->sqlite, update_digests_sql);
			if (rc != EPKG_OK) {
				ERROR_SQLITE(db->sqlite, update_digests_sql);
			}
			else {
				pkg_emit_progress_start("Updating database digests format");
				LL_FOREACH(pkglist, p) {
					pkg_emit_progress_tick(cur++, cnt);
					rc = run_prstmt(UPDATE_DIGEST, p->digest, p->id);
					if (rc != SQLITE_DONE) {
						assert(0);
						ERROR_SQLITE(db->sqlite, SQL(UPDATE_DIGEST));
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

		LL_FREE(pkglist, pkg_free);
	}
	else {
		rc = sql_exec(db->sqlite, solver_sql);
	}

	return (rc);
}

int
pkgdb_end_solver(struct pkgdb *db)
{
	const char solver_sql[] = ""
		"END TRANSACTION;"
		"PRAGMA synchronous = NORMAL;"
		"PRAGMA journal_mode = DELETE;";

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

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sql);
		return (EPKG_FATAL);
	}

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

int
pkgdb_repo_count(struct pkgdb *db)
{
	struct _pkg_repo_list_item *cur;
	int result = 0;

	if (db != NULL) {
		LL_FOREACH(db->repos, cur)
			result ++;
	}

	return (result);
}
