/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
 * Copyright (c) 2013 Gerald Pfeifer <gerald@pfeifer.com>
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
#include <sys/mount.h>

#include <assert.h>
#include <errno.h>
#include <regex.h>
#include <grp.h>
#include <libutil.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <sqlite3.h>

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
#define DB_SCHEMA_MINOR	22

#define DBVERSION (DB_SCHEMA_MAJOR * 1000 + DB_SCHEMA_MINOR)

static void pkgdb_regex(sqlite3_context *, int, sqlite3_value **);
static void pkgdb_regex_delete(void *);
static int pkgdb_upgrade(struct pkgdb *);
static void populate_pkg(sqlite3_stmt *stmt, struct pkg *pkg);
static void pkgdb_detach_remotes(sqlite3 *);
static int sqlcmd_init(sqlite3 *db, __unused const char **err,
    __unused const void *noused);
static int prstmt_initialize(struct pkgdb *db);
/* static int run_prstmt(sql_prstmt_index s, ...); */
static void prstmt_finalize(struct pkgdb *db);
static int pkgdb_insert_scripts(struct pkg *pkg, int64_t package_id, sqlite3 *s);


extern int sqlite3_shell(int, char**);

/*
 * Keep entries sorted by name!
 */
static struct column_mapping {
	const char * const name;
	pkg_attr type;
} columns[] = {
	{ "arch",	PKG_ARCH },
	{ "automatic",	PKG_AUTOMATIC },
	{ "cksum",	PKG_CKSUM },
	{ "comment",	PKG_COMMENT },
	{ "dbname",	PKG_REPONAME },
	{ "desc",	PKG_DESC },
	{ "flatsize",	PKG_FLATSIZE },
	{ "id",		PKG_ROWID },
	{ "licenselogic", PKG_LICENSE_LOGIC },
	{ "locked",	PKG_LOCKED },
	{ "maintainer",	PKG_MAINTAINER },
	{ "manifestdigest",	PKG_DIGEST },
	{ "message",	PKG_MESSAGE },
	{ "name",	PKG_NAME },
	{ "oldflatsize", PKG_OLD_FLATSIZE },
	{ "oldversion",	PKG_OLD_VERSION },
	{ "origin",	PKG_ORIGIN },
	{ "pkgsize",	PKG_PKGSIZE },
	{ "prefix",	PKG_PREFIX },
	{ "repopath",	PKG_REPOPATH },
	{ "rowid",	PKG_ROWID },
	{ "time",	PKG_TIME },
	{ "version",	PKG_VERSION },
	{ "weight",	-1 },
	{ "www",	PKG_WWW },
	{ NULL,		-1 }
};

static int
load_val(sqlite3 *db, struct pkg *pkg, const char *sql, unsigned flags,
    int (*pkg_adddata)(struct pkg *pkg, const char *data), int list)
{
	sqlite3_stmt	*stmt;
	int		 ret;
	int64_t		 rowid;

	assert(db != NULL && pkg != NULL);

	if (pkg->flags & flags)
		return (EPKG_OK);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db);
		return (EPKG_FATAL);
	}

	pkg_get(pkg, PKG_ROWID, &rowid);
	sqlite3_bind_int64(stmt, 1, rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
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

static int
load_tag_val(sqlite3 *db, struct pkg *pkg, const char *sql, unsigned flags,
	     int (*pkg_addtagval)(struct pkg *pkg, const char *tag, const char *val),
	     int list)
{
	sqlite3_stmt	*stmt;
	int		 ret;
	int64_t		 rowid;

	assert(db != NULL && pkg != NULL);

	if (pkg->flags & flags)
		return (EPKG_OK);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db);
		return (EPKG_FATAL);
	}

	pkg_get(pkg, PKG_ROWID, &rowid);
	sqlite3_bind_int64(stmt, 1, rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addtagval(pkg, sqlite3_column_text(stmt, 0),
			      sqlite3_column_text(stmt, 1));
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

static int
compare_column_func(const void *pkey, const void *pcolumn)
{
	const char *key = (const char*)pkey;
	const struct column_mapping *column =
			(const struct column_mapping*)pcolumn;

	return strcmp(key, column->name);
}

static void
populate_pkg(sqlite3_stmt *stmt, struct pkg *pkg) {
	int		 icol = 0;
	const char	*colname;

	assert(stmt != NULL);

	for (icol = 0; icol < sqlite3_column_count(stmt); icol++) {
		colname = sqlite3_column_name(stmt, icol);
		struct column_mapping *column;
		switch (sqlite3_column_type(stmt, icol)) {
		case SQLITE_TEXT:
			column = bsearch(colname, columns, NELEM(columns) - 1,
					sizeof(columns[0]), compare_column_func);
			if (column == NULL)
				pkg_emit_error("Unknown column %s",
				    colname);
			else
				pkg_set(pkg, column->type,
						sqlite3_column_text(stmt,
						icol));
			break;
		case SQLITE_INTEGER:
			column = bsearch(colname, columns, NELEM(columns) - 1,
					sizeof(columns[0]), compare_column_func);
			if (column == NULL)
				pkg_emit_error("Unknown column %s",
						colname);
			else
				pkg_set(pkg, column->type,
						sqlite3_column_int64(stmt,
						icol));
			break;
		case SQLITE_BLOB:
		case SQLITE_FLOAT:
			pkg_emit_error("Wrong type for column: %s",
			    colname);
			/* just ignore currently */
			break;
		case SQLITE_NULL:
			break;
		}
	}
}

static void
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
pkgdb_regex_delete(void *p)
{
	regex_t	*re = (regex_t *)p;

	regfree(re);
	free(re);
}

static void
pkgdb_now(sqlite3_context *ctx, int argc, __unused sqlite3_value **argv)
{
	if (argc != 0) {
		sqlite3_result_error(ctx, "Invalid usage of now() "
		    "no arguments expected\n", -1);
		return;
	}

	sqlite3_result_int64(ctx, (int64_t)time(NULL));
}

static void
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

		if (pkgdb_transaction_begin(db->sqlite, NULL) != EPKG_OK)
			return (EPKG_FATAL);

		if (sql_exec(db->sqlite, sql_upgrade) != EPKG_OK) {
			pkgdb_transaction_rollback(db->sqlite, NULL);
			return (EPKG_FATAL);
		}

		sql_str = "PRAGMA user_version = %" PRId64 ";";
		ret = sql_exec(db->sqlite, sql_str, db_version);
		if (ret != EPKG_OK) {
			pkgdb_transaction_rollback(db->sqlite, NULL);
			return (EPKG_FATAL);
		}

		if (pkgdb_transaction_commit(db->sqlite, NULL) != EPKG_OK)
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
		"locked INTEGER NOT NULL DEFAULT 0,"
		"licenselogic INTEGER NOT NULL,"
		"time INTEGER, "
		"manifestdigest TEXT NULL, "
		"pkg_format_version INTEGER"
	");"
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

/**
 * Initialize the local cache of the remote database with indicies
 */
int
pkgdb_remote_init(struct pkgdb *db, const char *repo)
{
	struct sbuf	*sql = NULL;
	const char	*reponame = NULL;
	int		 ret;
	const char	 init_sql[] = ""
	"BEGIN;"
	"CREATE INDEX IF NOT EXISTS '%s'.deps_origin ON deps(origin);"
	"CREATE INDEX IF NOT EXISTS '%s'.pkg_digest_id ON packages(origin, manifestdigest);"
	"CREATE INDEX IF NOT EXISTS '%s'.pkg_conflicts_pid ON pkg_conflicts(package_id);"
	"CREATE INDEX IF NOT EXISTS '%s'.pkg_conflicts_cid ON pkg_conflicts(conflict_id);"
	"CREATE INDEX IF NOT EXISTS '%s'.pkg_provides_id ON pkg_provides(package_id);"
	"COMMIT;"
	;

	if ((reponame = pkgdb_get_reponame(db, repo)) == NULL) {
		return (EPKG_FATAL);
	}

	sql = sbuf_new_auto();
	sbuf_printf(sql, init_sql, reponame, reponame, reponame, reponame, reponame);

	ret = sql_exec(db->sqlite, sbuf_data(sql));
	sbuf_delete(sql);
	return (ret);
}

static int
pkgdb_open_multirepos(const char *dbdir, struct pkgdb *db,
		      const char *reponame)
{
	int		  ret;
	char		  remotepath[MAXPATHLEN];
	struct pkg_repo	 *r = NULL;
	int		  repocount = 0;

	while (pkg_repos(&r) == EPKG_OK) {
		if (reponame != NULL) {
			if (strcmp(pkg_repo_ident(r), reponame) != 0)
				continue;
		} else {
			if (!pkg_repo_enabled(r)) 
				continue;
		}

		/* is it already attached? */
		if (pkgdb_is_attached(db->sqlite, pkg_repo_name(r))) {
			pkg_emit_error("repository '%s' is already "
			    "listed, ignoring", pkg_repo_ident(r));
			continue;
		}

		snprintf(remotepath, sizeof(remotepath), "%s/%s.sqlite",
			 dbdir, pkg_repo_name(r));

		if (access(remotepath, R_OK) != 0) {
			pkg_emit_noremotedb(pkg_repo_ident(r));
			pkgdb_close(db);
			return (EPKG_ENODB);
		}

		ret = sql_exec(db->sqlite, "ATTACH '%s' AS '%s';",
		          remotepath, pkg_repo_name(r));
		if (ret != EPKG_OK) {
			pkgdb_close(db);
			return (EPKG_FATAL);
		}

		switch (pkgdb_repo_check_version(db, pkg_repo_name(r))) {
		case EPKG_FATAL:
			pkgdb_close(db);
			return (EPKG_FATAL);
			break;
		case EPKG_REPOSCHEMA:
			ret = sql_exec(db->sqlite, "DETACH DATABASE '%s'",
				  pkg_repo_name(r));
			if (ret != EPKG_OK) {
				pkgdb_close(db);
				return (EPKG_FATAL);
			}
			break;
		default:
			repocount++;
			break;
		}
	}
	return (repocount > 0 ? EPKG_OK : EPKG_FATAL);
}

static int
file_mode_insecure(const char *path, bool install_as_user)
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

static int
database_access(unsigned mode, const char* dbdir, const char *dbname)
{
	char		 dbpath[MAXPATHLEN];
	int		 retval;
	bool		 database_exists;
	bool		 install_as_user;

	if (dbname != NULL)
		snprintf(dbpath, sizeof(dbpath), "%s/%s.sqlite", dbdir, dbname);
	else
		strlcpy(dbpath, dbdir, sizeof(dbpath));

	install_as_user = (getenv("INSTALL_AS_USER") != NULL);

	retval = file_mode_insecure(dbpath, install_as_user);

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
			mkdirs(dbpath);
			retval = eaccess(dbpath, W_OK);
		}
		break;
	case PKGDB_MODE_READ|PKGDB_MODE_WRITE:
		retval = eaccess(dbpath, R_OK|W_OK);
		if (retval != 0 && errno == ENOENT) {
			mkdirs(dbpath);
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
	 * EPKG_NODB:  a database doesn't exist and we don't want to create
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
		retval = database_access(PKGDB_MODE_READ|PKGDB_MODE_WRITE,
					 dbdir, NULL);
	} else
		retval = database_access(PKGDB_MODE_READ, dbdir, NULL);
	if (retval != EPKG_OK)
		return (retval);

	/* Test local.sqlite, if required */

	if ((database & PKGDB_DB_LOCAL) != 0) {
		retval = database_access(mode, dbdir, "local");
		if (retval != EPKG_OK)
			return (retval);
	}

	if ((database & PKGDB_DB_REPO) != 0) {
		struct pkg_repo	*r = NULL;

		while (pkg_repos(&r) == EPKG_OK) {
			/* Ignore any repos marked as inactive */
			if (!pkg_repo_enabled(r))
				continue;

			retval = database_access(mode, dbdir, pkg_repo_name(r));
			if (retval != EPKG_OK)
				return (retval);
		}
	}
	return (retval);
}

int
pkgdb_open(struct pkgdb **db_p, pkgdb_t type)
{
	return (pkgdb_open_all(db_p, type, NULL));
}

int
pkgdb_open_all(struct pkgdb **db_p, pkgdb_t type, const char *reponame)
{
	struct pkgdb	*db = NULL;
	struct statfs	 stfs;
	bool		 reopen = false;
	char		 localpath[MAXPATHLEN];
	const char	*dbdir;
	bool		 create = false;
	bool		 createdir = false;
	int		 ret;

	if (*db_p != NULL) {
		assert((*db_p)->lock_count == 0);
		reopen = true;
		db = *db_p;
		if (db->type == type)
			return (EPKG_OK);
	}

	dbdir = pkg_object_string(pkg_config_get("PKG_DBDIR"));
	if (!reopen && (db = calloc(1, sizeof(struct pkgdb))) == NULL) {
		pkg_emit_errno("malloc", "pkgdb");
		return EPKG_FATAL;
	}

	db->type = type;
	db->lock_count = 0;
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

		/*
		 * Fall back on unix-dotfile locking strategy if on a network filesystem
		 */
		if (statfs(dbdir, &stfs) == 0) {
			if ((stfs.f_flags & MNT_LOCAL) != MNT_LOCAL)
				sqlite3_vfs_register(sqlite3_vfs_find("unix-dotfile"), 1);
		}

		if (sqlite3_open(localpath, &db->sqlite) != SQLITE_OK) {
			ERROR_SQLITE(db->sqlite);
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
		sqlcmd_init(db->sqlite, NULL, NULL);

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
			db->type = PKGDB_REMOTE;
			ret = pkgdb_open_multirepos(dbdir, db, reponame);
			if (ret != EPKG_OK)
				return (ret);
		} else if (type == PKGDB_REMOTE) {
			if (*db_p == NULL)
				pkgdb_close(db);
			pkg_emit_error("No active remote repositories configured");
			return (EPKG_FATAL);
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

	if (db->prstmt_initialized)
		prstmt_finalize(db);

	if (db->sqlite != NULL) {
		assert(db->lock_count == 0);
		if (db->type == PKGDB_REMOTE) {
			pkgdb_detach_remotes(db->sqlite);
		}

		if (!sqlite3_db_readonly(db->sqlite, "main"))
			pkg_plugins_hook_run(PKG_PLUGIN_HOOK_PKGDB_CLOSE_RW, NULL, db);

		sqlite3_close(db->sqlite);
	}

	sqlite3_shutdown();
	free(db);
}

/* How many times to try COMMIT or ROLLBACK if the DB is busy */ 
#define NTRIES	3

int
pkgdb_transaction_begin(sqlite3 *sqlite, const char *savepoint)
{
	int		 ret;
	int		 tries;
	sqlite3_stmt	*stmt;


	assert(sqlite != NULL);

	if (savepoint == NULL || savepoint[0] == '\0') {
		const char sql[] = "BEGIN IMMEDIATE TRANSACTION";
		
		pkg_debug(4, "Pkgdb: running '%s'", sql);
		ret = sqlite3_prepare_v2(sqlite, sql, strlen(sql) + 1,
					 &stmt, NULL);
	} else {
		char sql[128] = "SAVEPOINT ";

		strlcat(sql, savepoint, sizeof(sql));

		pkg_debug(4, "Pkgdb: running '%s'", sql);
		ret = sqlite3_prepare_v2(sqlite, sql, strlen(sql) + 1,
					 &stmt, NULL);
	}

	if (ret == SQLITE_OK)
		for (tries = 0; tries < NTRIES; tries++) {
			ret = sqlite3_step(stmt);
			if (ret != SQLITE_BUSY)
				break;
			sqlite3_sleep(250);
		}

	sqlite3_finalize(stmt);

	if (ret != SQLITE_OK && ret != SQLITE_DONE)
		ERROR_SQLITE(sqlite);

	return (ret == SQLITE_OK || ret == SQLITE_DONE ? EPKG_OK : EPKG_FATAL);
}

int
pkgdb_transaction_commit(sqlite3 *sqlite, const char *savepoint)
{
	int		 ret;
	int		 tries;
	sqlite3_stmt	*stmt;

	assert(sqlite != NULL);

	if (savepoint == NULL || savepoint[0] == '\0') {
		const char sql[] = "COMMIT TRANSACTION";

		pkg_debug(4, "Pkgdb: running '%s'", sql);
		ret = sqlite3_prepare_v2(sqlite, sql, strlen(sql) + 1,
					 &stmt, NULL);
	} else {
		char sql[128] = "RELEASE SAVEPOINT ";

		strlcat(sql, savepoint, sizeof(sql));

		pkg_debug(4, "Pkgdb: running '%s'", sql);
		ret = sqlite3_prepare_v2(sqlite, sql, strlen(sql) + 1,
					 &stmt, NULL);
	}

	if (ret == SQLITE_OK)
		for (tries = 0; tries < NTRIES; tries++) {
			ret = sqlite3_step(stmt);
			if (ret != SQLITE_BUSY)
				break;
			sqlite3_sleep(250);
		}

	sqlite3_finalize(stmt);

	if (ret != SQLITE_OK && ret != SQLITE_DONE)
		ERROR_SQLITE(sqlite);

	return (ret == SQLITE_OK || ret == SQLITE_DONE ? EPKG_OK : EPKG_FATAL);
}

int
pkgdb_transaction_rollback(sqlite3 *sqlite, const char *savepoint)
{
	int		 ret;
	int		 tries;
	sqlite3_stmt	*stmt;

	assert(sqlite != NULL);

	if (savepoint == NULL || savepoint[0] == '\0') {
		const char sql[] = "ROLLBACK TRANSACTION";

		pkg_debug(4, "Pkgdb: running '%s'", sql);
		ret = sqlite3_prepare_v2(sqlite, sql, strlen(sql) + 1,
					 &stmt, NULL);
	} else {
		char sql[128] = "ROLLBACK TO SAVEPOINT ";

		strlcat(sql, savepoint, sizeof(sql));

		pkg_debug(4, "Pkgdb: running '%s'", sql);
		ret = sqlite3_prepare_v2(sqlite, sql, strlen(sql) + 1,
					 &stmt, NULL);
	}

	if (ret == SQLITE_OK)
		for (tries = 0; tries < NTRIES; tries++) {
			ret = sqlite3_step(stmt);
			if (ret != SQLITE_BUSY)
				break;
			sqlite3_sleep(250);
		}

	sqlite3_finalize(stmt);

	if (ret != SQLITE_OK && ret != SQLITE_DONE)
		ERROR_SQLITE(sqlite);

	return (ret == SQLITE_OK || ret == SQLITE_DONE ? EPKG_OK : EPKG_FATAL);
}


struct pkgdb_it *
pkgdb_it_new(struct pkgdb *db, sqlite3_stmt *s, int type, short flags)
{
	struct pkgdb_it	*it;

	assert(db != NULL && s != NULL);
	assert(!(flags & (PKGDB_IT_FLAG_CYCLED & PKGDB_IT_FLAG_ONCE)));
	assert(!(flags & (PKGDB_IT_FLAG_AUTO & (PKGDB_IT_FLAG_CYCLED | PKGDB_IT_FLAG_ONCE))));

	if ((it = malloc(sizeof(struct pkgdb_it))) == NULL) {
		pkg_emit_errno("malloc", "pkgdb_it");
		sqlite3_finalize(s);
		return (NULL);
	}

	it->db = db;
	it->sqlite = db->sqlite;
	it->stmt = s;
	it->type = type;
	it->flags = flags;
	it->finished = 0;
	return (it);
}

static struct load_on_flag {
	int	flag;
	int	(*load)(struct pkgdb *db, struct pkg *p);
} load_on_flag[] = {
	{ PKG_LOAD_DEPS,		pkgdb_load_deps },
	{ PKG_LOAD_RDEPS,		pkgdb_load_rdeps },
	{ PKG_LOAD_FILES,		pkgdb_load_files },
	{ PKG_LOAD_DIRS,		pkgdb_load_dirs },
	{ PKG_LOAD_SCRIPTS,		pkgdb_load_scripts },
	{ PKG_LOAD_OPTIONS,		pkgdb_load_options },
	{ PKG_LOAD_MTREE,		pkgdb_load_mtree },
	{ PKG_LOAD_CATEGORIES,		pkgdb_load_category },
	{ PKG_LOAD_LICENSES,		pkgdb_load_license },
	{ PKG_LOAD_USERS,		pkgdb_load_user },
	{ PKG_LOAD_GROUPS,		pkgdb_load_group },
	{ PKG_LOAD_SHLIBS_REQUIRED,	pkgdb_load_shlib_required },
	{ PKG_LOAD_SHLIBS_PROVIDED,	pkgdb_load_shlib_provided },
	{ PKG_LOAD_ANNOTATIONS,		pkgdb_load_annotations },
	{ PKG_LOAD_CONFLICTS,		pkgdb_load_conflicts },
	{ PKG_LOAD_PROVIDES,		pkgdb_load_provides },
	{ -1,			        NULL }
};

int
pkgdb_it_next(struct pkgdb_it *it, struct pkg **pkg_p, unsigned flags)
{
	struct pkg	*pkg;
	int		 i;
	int		 ret;

	assert(it != NULL);

	if (it->finished && (it->flags & PKGDB_IT_FLAG_ONCE))
		return (EPKG_END);

	switch (sqlite3_step(it->stmt)) {
	case SQLITE_ROW:
		if (*pkg_p == NULL) {
			ret = pkg_new(pkg_p, it->type);
			if (ret != EPKG_OK)
				return (ret);
		} else
			pkg_reset(*pkg_p, it->type);
		pkg = *pkg_p;

		populate_pkg(it->stmt, pkg);

		for (i = 0; load_on_flag[i].load != NULL; i++) {
			if (flags & load_on_flag[i].flag) {
				if (it->db != NULL) {
					ret = load_on_flag[i].load(it->db, pkg);
					if (ret != EPKG_OK)
						return (ret);
				}
				else {
					pkg_emit_error("invalid iterator passed to pkgdb_it_next");
					return (EPKG_FATAL);
				}
			}
		}

		return (EPKG_OK);
	case SQLITE_DONE:
		it->finished ++;
		if (it->flags & PKGDB_IT_FLAG_CYCLED) {
			sqlite3_reset(it->stmt);
			return (EPKG_OK);
		}
		else {
			if (it->flags & PKGDB_IT_FLAG_AUTO)
				pkgdb_it_free(it);
			return (EPKG_END);
		}
		break;
	default:
		ERROR_SQLITE(it->sqlite);
		return (EPKG_FATAL);
	}
}

void
pkgdb_it_reset(struct pkgdb_it *it)
{
	if (it == NULL)
		return;

	it->finished = 0;
	sqlite3_reset(it->stmt);
}

void
pkgdb_it_free(struct pkgdb_it *it)
{
	if (it == NULL)
		return;

	sqlite3_finalize(it->stmt);
	free(it);
}

/* By default, MATCH_EXACT and MATCH_REGEX are case sensitive. */

static bool _case_sensitive_flag = true;

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

const char *
pkgdb_get_pattern_query(const char *pattern, match_t match)
{
	char		*checkorigin = NULL;
	const char	*comp = NULL;

	if (pattern != NULL)
		checkorigin = strchr(pattern, '/');

	switch (match) {
	case MATCH_ALL:
		comp = "";
		break;
	case MATCH_EXACT:
		if (pkgdb_case_sensitive()) {
			if (checkorigin == NULL)
				comp = " WHERE name = ?1 "
					"OR name || \"-\" || version = ?1";
			else
				comp = " WHERE origin = ?1";
		} else {
			if (checkorigin == NULL)
				comp = " WHERE name = ?1 COLLATE NOCASE "
					"OR name || \"-\" || version = ?1"
					"COLLATE NOCASE";
			else
				comp = " WHERE origin = ?1 COLLATE NOCASE";
		}
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
	case MATCH_CONDITION:
		comp = pattern;
		break;
	}

	return (comp);
}

static const char *
pkgdb_get_match_how(match_t match)
{
	const char	*how = NULL;

	switch (match) {
	case MATCH_ALL:
		how = NULL;
		break;
	case MATCH_EXACT:
		if (pkgdb_case_sensitive())
			how = "%s = ?1";
		else
			how = "%s = ?1 COLLATE NOCASE";			
		break;
	case MATCH_GLOB:
		how = "%s GLOB ?1";
		break;
	case MATCH_REGEX:
		how = "%s REGEXP ?1";
		break;
	case MATCH_CONDITION:
		/* Should not be called by pkgdb_get_match_how(). */
		assert(0);
		break;
	}

	return (how);
}

struct pkgdb_it *
pkgdb_query(struct pkgdb *db, const char *pattern, match_t match)
{
	char		 sql[BUFSIZ];
	sqlite3_stmt	*stmt;
	const char	*comp = NULL;

	assert(db != NULL);
	assert(match == MATCH_ALL || (pattern != NULL && pattern[0] != '\0'));

	comp = pkgdb_get_pattern_query(pattern, match);

	sqlite3_snprintf(sizeof(sql), sql,
			"SELECT id, origin, name, version, comment, desc, "
				"message, arch, maintainer, www, "
				"prefix, flatsize, licenselogic, automatic, "
				"locked, time "
			"FROM packages AS p%s "
			"ORDER BY p.name;", comp);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	if (match != MATCH_ALL && match != MATCH_CONDITION)
		sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_INSTALLED, PKGDB_IT_FLAG_ONCE));
}

struct pkgdb_it *
pkgdb_query_which(struct pkgdb *db, const char *path, bool glob)
{
	sqlite3_stmt	*stmt;
	char	sql[BUFSIZ];


	assert(db != NULL);
	sqlite3_snprintf(sizeof(sql), sql,
			"SELECT p.id, p.origin, p.name, p.version, p.comment, p.desc, "
			"p.message, p.arch, p.maintainer, p.www, "
			"p.prefix, p.flatsize, p.time "
			"FROM packages AS p "
			"LEFT JOIN files AS f ON p.id = f.package_id "
			"WHERE f.path %s ?1 GROUP BY p.id;", glob ? "GLOB" : "=");

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_INSTALLED, PKGDB_IT_FLAG_ONCE));
}

struct pkgdb_it *
pkgdb_query_shlib_required(struct pkgdb *db, const char *shlib)
{
	sqlite3_stmt	*stmt;
	const char	 sql[] = ""
		"SELECT p.id, p.origin, p.name, p.version, p.comment, p.desc, "
			"p.message, p.arch, p.maintainer, p.www, "
			"p.prefix, p.flatsize, p.time "
			"FROM packages AS p, pkg_shlibs_required AS ps, shlibs AS s "
			"WHERE p.id = ps.package_id "
				"AND ps.shlib_id = s.id "
				"AND s.name = ?1;";

	assert(db != NULL);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sqlite3_bind_text(stmt, 1, shlib, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_INSTALLED, PKGDB_IT_FLAG_ONCE));
}

struct pkgdb_it *
pkgdb_query_shlib_provided(struct pkgdb *db, const char *shlib)
{
	sqlite3_stmt	*stmt;
	const char	 sql[] = ""
		"SELECT p.id, p.origin, p.name, p.version, p.comment, p.desc, "
			"p.message, p.arch, p.maintainer, p.www, "
			"p.prefix, p.flatsize, p.time "
			"FROM packages AS p, pkg_shlibs_provided AS ps, shlibs AS s "
			"WHERE p.id = ps.package_id "
				"AND ps.shlib_id = s.id "
				"AND s.name = ?1;";

	assert(db != NULL);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sqlite3_bind_text(stmt, 1, shlib, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_INSTALLED, PKGDB_IT_FLAG_ONCE));
}

int
pkgdb_is_dir_used(struct pkgdb *db, const char *dir, int64_t *res)
{
	sqlite3_stmt	*stmt;
	int		 ret;
	const char	 sql[] = ""
		"SELECT count(package_id) FROM pkg_directories, directories "
		"WHERE directory_id = directories.id AND directories.path = ?1;";

	assert(db != NULL);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
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
	sqlite3_stmt	*stmt = NULL;
	int		 ret = EPKG_OK;
	int64_t		 rowid;
	char		 sql[BUFSIZ];
	const char	*reponame = NULL;
	const char	*mainsql = ""
		"SELECT d.name, d.origin, d.version, p.locked "
		"FROM main.deps AS d "
		"LEFT JOIN main.packages AS p ON p.origin = d.origin "
		"WHERE d.package_id = ?1 ORDER BY d.origin DESC;";
	const char	*reposql = ""
		"SELECT d.name, d.origin, d.version, 0 "
		"FROM %Q.deps AS d "
		"WHERE d.package_id = ?1 ORDER BY d.origin DESC;";

	assert(db != NULL && pkg != NULL);

	if (pkg->flags & PKG_LOAD_DEPS)
		return (EPKG_OK);

	if (pkg->type == PKG_REMOTE) {
		assert(db->type == PKGDB_REMOTE);
		pkg_get(pkg, PKG_REPONAME, &reponame);
		sqlite3_snprintf(sizeof(sql), sql, reposql, reponame);
		pkg_debug(4, "Pkgdb: running '%s'", sql);
		ret = sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL);
	} else {
		pkg_debug(4, "Pkgdb: running '%s'", mainsql);
		ret = sqlite3_prepare_v2(db->sqlite, mainsql, -1, &stmt, NULL);
	}

	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg_get(pkg, PKG_ROWID, &rowid);
	sqlite3_bind_int64(stmt, 1, rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_adddep(pkg, sqlite3_column_text(stmt, 0),
			   sqlite3_column_text(stmt, 1),
			   sqlite3_column_text(stmt, 2),
			   sqlite3_column_int(stmt, 3));
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
	sqlite3_stmt	*stmt = NULL;
	int		 ret;
	const char	*origin;
	const char	*reponame = NULL;
	char		 sql[BUFSIZ];
	const char	*mainsql = ""
		"SELECT p.name, p.origin, p.version, p.locked "
		"FROM main.packages AS p, main.deps AS d "
		"WHERE p.id = d.package_id "
			"AND d.origin = ?1;";
	const char	*reposql = ""
		"SELECT p.name, p.origin, p.version, 0 "
		"FROM %Q.packages AS p, %Q.deps AS d "
		"WHERE p.id = d.package_id "
			"AND d.origin = ?1;";

	assert(db != NULL && pkg != NULL);

	if (pkg->flags & PKG_LOAD_RDEPS)
		return (EPKG_OK);

	if (pkg->type == PKG_REMOTE) {
		assert(db->type == PKGDB_REMOTE);
		pkg_get(pkg, PKG_REPONAME, &reponame);
		sqlite3_snprintf(sizeof(sql), sql, reposql, reponame, reponame);
		pkg_debug(4, "Pkgdb: running '%s'", sql);
		ret = sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL);
	} else {
		pkg_debug(4, "Pkgdb: running '%s'", mainsql);
		ret = sqlite3_prepare_v2(db->sqlite, mainsql, -1, &stmt, NULL);
	}

	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg_get(pkg, PKG_ORIGIN, &origin);
	sqlite3_bind_text(stmt, 1, origin, -1, SQLITE_STATIC);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addrdep(pkg, sqlite3_column_text(stmt, 0),
			    sqlite3_column_text(stmt, 1),
			    sqlite3_column_text(stmt, 2),
			    sqlite3_column_int(stmt, 3));
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
	sqlite3_stmt	*stmt = NULL;
	int		 ret;
	int64_t		 rowid;
	const char	 sql[] = ""
		"SELECT path, sha256 "
		"FROM files "
		"WHERE package_id = ?1 "
		"ORDER BY PATH ASC";

	assert(db != NULL && pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_FILES)
		return (EPKG_OK);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg_get(pkg, PKG_ROWID, &rowid);
	sqlite3_bind_int64(stmt, 1, rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addfile(pkg, sqlite3_column_text(stmt, 0),
		    sqlite3_column_text(stmt, 1), false);
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
	const char	 sql[] = ""
		"SELECT path, try "
		"FROM pkg_directories, directories "
		"WHERE package_id = ?1 "
		"AND directory_id = directories.id "
		"ORDER by path DESC";
	sqlite3_stmt	*stmt;
	int		 ret;
	int64_t		 rowid;

	assert(db != NULL && pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_DIRS)
		return (EPKG_OK);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg_get(pkg, PKG_ROWID, &rowid);
	sqlite3_bind_int64(stmt, 1, rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_adddir(pkg, sqlite3_column_text(stmt, 0),
		    sqlite3_column_int(stmt, 1), false);
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
	char		 sql[BUFSIZ];
	const char	*reponame = NULL;
	const char	*basesql = ""
		"SELECT name "
		"FROM %Q.pkg_licenses, %Q.licenses AS l "
		"WHERE package_id = ?1 "
			"AND license_id = l.id "
		"ORDER by name DESC";

	assert(db != NULL && pkg != NULL);

	if (pkg->type == PKG_REMOTE) {
		assert(db->type == PKGDB_REMOTE);
		pkg_get(pkg, PKG_REPONAME, &reponame);
		sqlite3_snprintf(sizeof(sql), sql, basesql, reponame, reponame);
	} else
		sqlite3_snprintf(sizeof(sql), sql, basesql, "main", "main");

	return (load_val(db->sqlite, pkg, sql, PKG_LOAD_LICENSES,
	    pkg_addlicense, PKG_LICENSES));
}

int
pkgdb_load_category(struct pkgdb *db, struct pkg *pkg)
{
	char		 sql[BUFSIZ];
	const char	*reponame = NULL;
	const char	*basesql = ""
		"SELECT name "
		"FROM %Q.pkg_categories, %Q.categories AS c "
		"WHERE package_id = ?1 "
			"AND category_id = c.id "
		"ORDER by name DESC";
	
	assert(db != NULL && pkg != NULL);

	if (pkg->type == PKG_REMOTE) {
		assert(db->type == PKGDB_REMOTE);
		pkg_get(pkg, PKG_REPONAME, &reponame);
		sqlite3_snprintf(sizeof(sql), sql, basesql, reponame, reponame);
	} else
		sqlite3_snprintf(sizeof(sql), sql, basesql, "main", "main");

	return (load_val(db->sqlite, pkg, sql, PKG_LOAD_CATEGORIES,
	    pkg_addcategory, PKG_CATEGORIES));
}

int
pkgdb_load_user(struct pkgdb *db, struct pkg *pkg)
{
	/*struct pkg_user *u = NULL;
	struct passwd *pwd = NULL;*/
	int		ret;
	const char	sql[] = ""
		"SELECT users.name "
		"FROM pkg_users, users "
		"WHERE package_id = ?1 "
			"AND user_id = users.id "
		"ORDER by name DESC";

	assert(db != NULL && pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	ret = load_val(db->sqlite, pkg, sql, PKG_LOAD_USERS,
	    pkg_adduser, PKG_USERS);

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
	struct pkg_group	*g = NULL;
	struct group		*grp = NULL;
	int			 ret;
	const char		 sql[] = ""
		"SELECT groups.name "
		"FROM pkg_groups, groups "
		"WHERE package_id = ?1 "
			"AND group_id = groups.id "
		"ORDER by name DESC";

	assert(db != NULL && pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	ret = load_val(db->sqlite, pkg, sql, PKG_LOAD_GROUPS,
	    pkg_addgroup, PKG_GROUPS);

	while (pkg_groups(pkg, &g) == EPKG_OK) {
		grp = getgrnam(pkg_group_name(g));
		if (grp == NULL)
			continue;
		strlcpy(g->gidstr, gr_make(grp), sizeof(g->gidstr));
	}

	return (ret);
}

int
pkgdb_load_shlib_required(struct pkgdb *db, struct pkg *pkg)
{
	char		 sql[BUFSIZ];
	const char	*reponame = NULL;
	const char	*basesql = ""
		"SELECT name "
		"FROM %Q.pkg_shlibs_required, %Q.shlibs AS s "
		"WHERE package_id = ?1 "
			"AND shlib_id = s.id "
		"ORDER by name DESC";

	assert(db != NULL && pkg != NULL);

	if (pkg->type == PKG_REMOTE) {
		assert(db->type == PKGDB_REMOTE);
		pkg_get(pkg, PKG_REPONAME, &reponame);
		sqlite3_snprintf(sizeof(sql), sql, basesql, reponame, reponame);
	} else
		sqlite3_snprintf(sizeof(sql), sql, basesql, "main", "main");

	return (load_val(db->sqlite, pkg, sql, PKG_LOAD_SHLIBS_REQUIRED,
	    pkg_addshlib_required, PKG_SHLIBS_REQUIRED));
}


int
pkgdb_load_shlib_provided(struct pkgdb *db, struct pkg *pkg)
{
	char		 sql[BUFSIZ];
	const char	*reponame = NULL;
	const char	*basesql = ""
		"SELECT name "
		"FROM %Q.pkg_shlibs_provided, %Q.shlibs AS s "
		"WHERE package_id = ?1 "
			"AND shlib_id = s.id "
		"ORDER by name DESC";

	assert(db != NULL && pkg != NULL);

	if (pkg->type == PKG_REMOTE) {
		assert(db->type == PKGDB_REMOTE);
		pkg_get(pkg, PKG_REPONAME, &reponame);
		sqlite3_snprintf(sizeof(sql), sql, basesql, reponame, reponame);
	} else
		sqlite3_snprintf(sizeof(sql), sql, basesql, "main", "main");

	return (load_val(db->sqlite, pkg, sql, PKG_LOAD_SHLIBS_PROVIDED,
	    pkg_addshlib_provided, PKG_SHLIBS_PROVIDED));
}

int
pkgdb_load_annotations(struct pkgdb *db, struct pkg *pkg)
{
	char		 sql[BUFSIZ];
	const char	*reponame = NULL;
	const char	*basesql = ""
		"SELECT k.annotation AS tag, v.annotation AS value"
		"  FROM %Q.pkg_annotation p"
		"    JOIN %Q.annotation k ON (p.tag_id = k.annotation_id)"
		"    JOIN %Q.annotation v ON (p.value_id = v.annotation_id)"
		"  WHERE p.package_id = ?1"
		"  ORDER BY tag, value";

	assert(db != NULL && pkg != NULL);

	if (pkg->type == PKG_REMOTE) {
		assert(db->type == PKGDB_REMOTE);
		pkg_get(pkg, PKG_REPONAME, &reponame);
		sqlite3_snprintf(sizeof(sql), sql, basesql, reponame,
		    reponame, reponame);
	} else
		sqlite3_snprintf(sizeof(sql), sql, basesql, "main",
                    "main", "main");

	return (load_tag_val(db->sqlite, pkg, sql, PKG_LOAD_ANNOTATIONS,
		   pkg_addannotation, PKG_ANNOTATIONS));
}

int
pkgdb_load_scripts(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt	*stmt = NULL;
	int		 ret;
	int64_t		 rowid;
	const char	 sql[] = ""
		"SELECT script, type "
		"FROM pkg_script JOIN script USING(script_id) "
		"WHERE package_id = ?1";

	assert(db != NULL && pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_SCRIPTS)
		return (EPKG_OK);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg_get(pkg, PKG_ROWID, &rowid);
	sqlite3_bind_int64(stmt, 1, rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addscript(pkg, sqlite3_column_text(stmt, 0),
		    sqlite3_column_int(stmt, 1));
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_SCRIPTS;
	return (EPKG_OK);
}


int
pkgdb_load_options(struct pkgdb *db, struct pkg *pkg)
{
	const char	*reponame;
	char		 sql[BUFSIZ];
	unsigned int	 i;

	struct optionsql {
		const char	 *sql;
		int		(*pkg_addtagval)(struct pkg *pkg,
						  const char *tag,
						  const char *val);
		int		  nargs;
	}			  optionsql[] = {
		{
			"SELECT option, value "
			"FROM %Q.option JOIN %Q.pkg_option USING(option_id) "
			"WHERE package_id = ?1 ORDER BY option",
			pkg_addoption,
			2,
		},
		{
			"SELECT option, default_value "
			"FROM %Q.option JOIN %Q.pkg_option_default USING(option_id) "
			"WHERE package_id = ?1 ORDER BY option",
			pkg_addoption_default,
			2,
		},
		{
			"SELECT option, description "
			"FROM %Q.option JOIN %Q.pkg_option_desc USING(option_id) "
			"JOIN %Q.option_desc USING(option_desc_id) ORDER BY option",
			pkg_addoption_description,
			3,
		}
	};
	const char		 *opt_sql;
	int			(*pkg_addtagval)(struct pkg *pkg,
						 const char *tag,
						 const char *val);
	int			  nargs, ret;

	assert(db != NULL && pkg != NULL);

	if (pkg->flags & PKG_LOAD_OPTIONS)
		return (EPKG_OK);

	if (pkg->type == PKG_REMOTE) {
		assert(db->type == PKGDB_REMOTE);
		pkg_get(pkg, PKG_REPONAME, &reponame);
	} else {
		reponame = "main";
	}

	for (i = 0; i < NELEM(optionsql); i++) {
		opt_sql       = optionsql[i].sql;
		pkg_addtagval = optionsql[i].pkg_addtagval;
		nargs         = optionsql[i].nargs;

		switch(nargs) {
		case 1:
			sqlite3_snprintf(sizeof(sql), sql, opt_sql, reponame);
			break;
		case 2:
			sqlite3_snprintf(sizeof(sql), sql, opt_sql, reponame,
					 reponame);
			break;
		case 3:
			sqlite3_snprintf(sizeof(sql), sql, opt_sql, reponame,
					 reponame, reponame);
			break;
		default:
			/* Nothing needs 4 or more, yet... */
			return (EPKG_FATAL);
			break;
		}

		pkg_debug(4, "Pkgdb> adding option");
		ret = load_tag_val(db->sqlite, pkg, sql, PKG_LOAD_OPTIONS,
				   pkg_addtagval, PKG_OPTIONS);
		if (ret != EPKG_OK)
			break;
	}
	return (ret);
}

int
pkgdb_load_mtree(struct pkgdb *db, struct pkg *pkg)
{
	const char	sql[] = ""
		"SELECT m.content "
		"FROM mtree AS m, packages AS p "
		"WHERE m.id = p.mtree_id "
			"AND p.id = ?1;";

	assert(db != NULL && pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	return (load_val(db->sqlite, pkg, sql, PKG_LOAD_MTREE, pkg_set_mtree, -1));
}

int
pkgdb_load_conflicts(struct pkgdb *db, struct pkg *pkg)
{
	char		 sql[BUFSIZ];
	const char	*reponame = NULL;
	const char	*basesql = ""
			"SELECT packages.origin "
			"FROM %Q.pkg_conflicts "
			"LEFT JOIN %Q.packages ON "
			"packages.id = pkg_conflicts.conflict_id "
			"WHERE package_id = ?1";

	assert(db != NULL && pkg != NULL);

	if (pkg->type == PKG_REMOTE) {
		assert(db->type == PKGDB_REMOTE);
		pkg_get(pkg, PKG_REPONAME, &reponame);
		sqlite3_snprintf(sizeof(sql), sql, basesql, reponame, reponame);
	} else
		sqlite3_snprintf(sizeof(sql), sql, basesql, "main", "main");

	return (load_val(db->sqlite, pkg, sql, PKG_LOAD_CONFLICTS,
			pkg_addconflict, PKG_CONFLICTS));
}

int
pkgdb_load_provides(struct pkgdb *db, struct pkg *pkg)
{
	char		 sql[BUFSIZ];
	const char	*reponame = NULL;
	const char	*basesql = ""
		"SELECT provide "
		"FROM %Q.provides "
		"WHERE package_id = ?1";

	assert(db != NULL && pkg != NULL);

	if (pkg->type == PKG_REMOTE) {
		assert(db->type == PKGDB_REMOTE);
		pkg_get(pkg, PKG_REPONAME, &reponame);
		sqlite3_snprintf(sizeof(sql), sql, basesql, reponame, reponame);
	} else
		sqlite3_snprintf(sizeof(sql), sql, basesql, "main", "main");

	return (load_val(db->sqlite, pkg, sql, PKG_LOAD_PROVIDES,
			pkg_addconflict, PKG_PROVIDES));
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
		"UPDATE deps SET name=?1, version=?2 WHERE origin=?3;",
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
		"INSERT INTO pkg_shlibs_required(package_id, shlib_id) "
		"VALUES (?1, (SELECT id FROM shlibs WHERE name = ?2))",
		"IT",
	},
	[SHLIBS_PROV] = {
		NULL,
		"INSERT INTO pkg_shlibs_provided(package_id, shlib_id) "
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
		" (SELECT id FROM packages WHERE origin = ?1 ),"
		" (SELECT annotation_id FROM annotation WHERE annotation = ?2),"
		" (SELECT annotation_id FROM annotation WHERE annotation = ?3))",
		"TTTT",
	},
	[ANNOTATE_DEL1] = {
		NULL,
		"DELETE FROM pkg_annotation WHERE "
		"package_id IN"
                " (SELECT id FROM packages WHERE origin = ?1) "
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
		"VALUES (?1, (SELECT id FROM packages WHERE origin = ?2))",
		"IT",
	},
	[PKG_PROVIDE] = {
		NULL,
		"INSERT INTO pkg_provides(package_id, provide_id) "
		"VALUES (?1, (SELECT id FROM provides WHERE provide = ?2))",
		"IT",
	},
	{
		NULL,
		"INSERT OR IGNORE INTO provides(provide) VALUES(?1)",
		"T",
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

	sqlite = db->sqlite;

	for (i = 0; i < PRSTMT_LAST; i++)
	{
		pkg_debug(4, "Pkgdb: running '%s'", SQL(i));
		ret = sqlite3_prepare_v2(sqlite, SQL(i), -1, &STMT(i), NULL);
		if (ret != SQLITE_OK) {
			ERROR_SQLITE(sqlite);
			return (EPKG_FATAL);
		}
	}
	db->prstmt_initialized = true;

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
	struct pkg_dep		*dep = NULL;
	struct pkg_file		*file = NULL;
	struct pkg_dir		*dir = NULL;
	struct pkg_option	*option = NULL;
	struct pkg_user		*user = NULL;
	struct pkg_group	*group = NULL;
	struct pkg_conflict	*conflict = NULL;
	struct pkgdb_it		*it = NULL;
	const pkg_object	*obj;
	pkg_iter		 iter;

	sqlite3			*s;

	int			 ret;
	int			 retcode = EPKG_FATAL;
	int64_t			 package_id;

	const char		*mtree, *origin, *name, *version, *name2;
	const char		*version2, *comment, *desc, *message;
	const char		*arch, *maintainer, *www, *prefix;
	const char		*digest;

	bool			 automatic;
	lic_t			 licenselogic;
	int64_t			 flatsize;

	const pkg_object	*licenses, *categories;

	assert(db != NULL);

	if (pkg_is_valid(pkg) != EPKG_OK) {
		pkg_emit_error("the package is not valid");
		return (EPKG_FATAL);
	}

	if (!db->prstmt_initialized && prstmt_initialize(db) != EPKG_OK)
		return (EPKG_FATAL);

	s = db->sqlite;

	if (!complete && pkgdb_transaction_begin(s, NULL) != EPKG_OK)
		return (EPKG_FATAL);

	pkg_get(pkg,
		PKG_MTREE,	&mtree,
		PKG_ORIGIN,	&origin,
		PKG_VERSION,	&version,
		PKG_COMMENT,	&comment,
		PKG_DESC,	&desc,
		PKG_MESSAGE,	&message,
		PKG_ARCH,	&arch,
		PKG_MAINTAINER,	&maintainer,
		PKG_WWW,	&www,
		PKG_PREFIX,	&prefix,
		PKG_FLATSIZE,	&flatsize,
		PKG_AUTOMATIC,	&automatic,
		PKG_LICENSE_LOGIC, &licenselogic,
		PKG_NAME,	&name,
		PKG_DIGEST,	&digest,
		PKG_LICENSES,	&licenses,
		PKG_CATEGORIES,	&categories);

	/*
	 * Insert mtree record
	 */

	if (run_prstmt(MTREE, mtree) != SQLITE_DONE) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	/*
	 * Insert package record
	 */
	ret = run_prstmt(PKG, origin, name, version, comment, desc, message,
	    arch, maintainer, www, prefix, flatsize, (int64_t)automatic,
	    (int64_t)licenselogic, mtree, digest);
	if (ret != SQLITE_DONE) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	package_id = sqlite3_last_insert_rowid(s);

	/*
	 * update dep informations on packages that depends on the insert
	 * package
	 */

	if (run_prstmt(DEPS_UPDATE, name, version, origin) != SQLITE_DONE) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	/*
	 * Insert dependencies list
	 */

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		if (run_prstmt(DEPS, pkg_dep_origin(dep),
		    pkg_dep_name(dep), pkg_dep_version(dep),
		    package_id) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
	}

	/*
	 * Insert files.
	 */

	while (pkg_files(pkg, &file) == EPKG_OK) {
		const char	*pkg_path = pkg_file_path(file);
		const char	*pkg_sum  = pkg_file_cksum(file);
		bool		permissive = false;
		bool		devmode = false;

		ret = run_prstmt(FILES, pkg_path, pkg_sum, package_id);
		if (ret == SQLITE_DONE)
			continue;
		if (ret != SQLITE_CONSTRAINT) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		it = pkgdb_query_which(db, pkg_path, false);
		if (it == NULL) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		pkg2 = NULL;
		ret = pkgdb_it_next(it, &pkg2, PKG_LOAD_BASIC);
		if (ret == EPKG_END) {
			/* Stray entry in the files table not related to
			   any known package: overwrite this */
			ret = run_prstmt(FILES_REPLACE, pkg_path, pkg_sum,
					 package_id);
			pkgdb_it_free(it);
			if (ret == SQLITE_DONE)
				continue;
			else {
				ERROR_SQLITE(s);
				goto cleanup;
			}
		}
		if (ret != EPKG_OK && ret != EPKG_END) {
			pkgdb_it_free(it);
			ERROR_SQLITE(s);
			goto cleanup;
		}
		pkg_get(pkg2, PKG_NAME, &name2, PKG_VERSION, &version2);
		if (!forced) {
			devmode = pkg_object_bool(pkg_config_get("DEVELOPER_MODE"));
			if (!devmode)
				permissive = pkg_object_bool(pkg_config_get("PERMISSIVE"));
			pkg_emit_error("%s-%s conflicts with %s-%s"
					" (installs files into the same place). "
					" Problematic file: %s%s",
					name, version, name2, version2, pkg_path,
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
					name, version, name2, version2, pkg_path);
			pkg_free(pkg2);
		}
		pkgdb_it_free(it);
	}

	/*
	 * Insert dirs.
	 */

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		if (run_prstmt(DIRS1, pkg_dir_path(dir)) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		if ((ret = run_prstmt(DIRS2, package_id, pkg_dir_path(dir),
		    pkg_dir_try(dir))) != SQLITE_DONE) {
			if (ret == SQLITE_CONSTRAINT) {
				pkg_emit_error("Another package is already "
				    "providing directory: %s",
				    pkg_dir_path(dir));
			} else
				ERROR_SQLITE(s);
			goto cleanup;
		}
	}

	/*
	 * Insert categories
	 */

	iter = NULL;
	while ((obj = pkg_object_iterate(categories, &iter))) {
		ret = run_prstmt(CATEGORY1, pkg_object_string(obj));
		if (ret == SQLITE_DONE)
			ret = run_prstmt(CATEGORY2, package_id, pkg_object_string(obj));
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
	}

	/*
	 * Insert licenses
	 */

	iter = NULL;
	while ((obj = pkg_object_iterate(licenses, &iter))) {
		if (run_prstmt(LICENSES1, pkg_object_string(obj))
		    != SQLITE_DONE
		    ||
		    run_prstmt(LICENSES2, package_id, pkg_object_string(obj))
		    != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
	}

	/*
	 * Insert users
	 */

	while (pkg_users(pkg, &user) == EPKG_OK) {
		if (run_prstmt(USERS1, pkg_user_name(user))
		    != SQLITE_DONE
		    ||
		    run_prstmt(USERS2, package_id, pkg_user_name(user))
		    != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
	}

	/*
	 * Insert groups
	 */

	while (pkg_groups(pkg, &group) == EPKG_OK) {
		if (run_prstmt(GROUPS1, pkg_group_name(group))
		    != SQLITE_DONE
		    ||
		    run_prstmt(GROUPS2, package_id, pkg_group_name(group))
		    != SQLITE_DONE) {
			ERROR_SQLITE(s);
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
		if (run_prstmt(OPTION1, pkg_option_opt(option)) != SQLITE_DONE
		    ||
		    run_prstmt(OPTION2, package_id, pkg_option_opt(option),
			       pkg_option_value(option)) != SQLITE_DONE) {
			ERROR_SQLITE(s);
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
		if (run_prstmt(CONFLICT, package_id, pkg_conflict_origin(conflict))
				!= SQLITE_DONE) {
			ERROR_SQLITE(s);
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
			ERROR_SQLITE(s);
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
		if (run_prstmt(SHLIBS1, pkg_shlib_name(shlib))
		    != SQLITE_DONE
		    ||
		    run_prstmt(SHLIBS_REQD, package_id, pkg_shlib_name(shlib))
		    != SQLITE_DONE) {
			ERROR_SQLITE(s);
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
		if (run_prstmt(SHLIBS1, pkg_shlib_name(shlib))
		    != SQLITE_DONE
		    ||
		    run_prstmt(SHLIBS_PROV, package_id, pkg_shlib_name(shlib))
		    != SQLITE_DONE) {
			ERROR_SQLITE(s);
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
		if (run_prstmt(PROVIDE, pkg_provide_name(provide))
		    != SQLITE_DONE
		    ||
		    run_prstmt(PKG_PROVIDE, package_id, pkg_provide_name(provide))
		    != SQLITE_DONE) {
			ERROR_SQLITE(s);
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

int
pkgdb_insert_annotations(struct pkg *pkg, int64_t package_id, sqlite3 *s)
{
	const pkg_object	*note, *annotations;
	pkg_iter		 it = NULL;

	pkg_get(pkg, PKG_ANNOTATIONS, &annotations);
	while ((note = pkg_object_iterate(annotations, &it))) {
		if (run_prstmt(ANNOTATE1, pkg_object_key(note))
		    != SQLITE_DONE
		    ||
		    run_prstmt(ANNOTATE1, pkg_object_string(note))
		    != SQLITE_DONE
		    ||
		    run_prstmt(ANNOTATE2, package_id,
			pkg_object_key(note),
			pkg_object_string(note))
		    != SQLITE_DONE) {
			ERROR_SQLITE(s);
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
		if (!db->prstmt_initialized &&
		    prstmt_initialize(db) != EPKG_OK)
			return (EPKG_FATAL);

		s = db->sqlite;
		pkg_get(pkg, PKG_ROWID, &package_id);

		for (i = 0; i < 2; i++) {
			/* Clean out old shlibs first */
			pkg_debug(4, "Pkgdb: running '%s'", sql[i]);
			if (sqlite3_prepare_v2(db->sqlite, sql[i], -1,
					       &stmt_del, NULL)
			    != SQLITE_OK) {
				ERROR_SQLITE(db->sqlite);
				return (EPKG_FATAL);
			}

			sqlite3_bind_int64(stmt_del, 1, package_id);

			ret = sqlite3_step(stmt_del);
			sqlite3_finalize(stmt_del);

			if (ret != SQLITE_DONE) {
				ERROR_SQLITE(db->sqlite);
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
	const char	*pkgorigin;

	assert(pkg != NULL);
	assert(tag != NULL);
	assert(value != NULL);

	if (!db->prstmt_initialized && prstmt_initialize(db) != EPKG_OK)
		return (EPKG_FATAL);

	pkg_get(pkg, PKG_ORIGIN, &pkgorigin);

	if (run_prstmt(ANNOTATE1, tag) != SQLITE_DONE
	    ||
	    run_prstmt(ANNOTATE1, value) != SQLITE_DONE
	    ||
	    run_prstmt(ANNOTATE_ADD1, pkgorigin, tag, value)
	    != SQLITE_DONE) {
		ERROR_SQLITE(db->sqlite);
		pkgdb_transaction_rollback(db->sqlite, NULL);
		return (EPKG_FATAL);
	}

	/* Expect rows_changed == 1 unless there's already an
	   annotation using the given tag */

	rows_changed = sqlite3_changes(db->sqlite);

	return (rows_changed == 1 ? EPKG_OK : EPKG_WARN);
}

int
pkgdb_modify_annotation(struct pkgdb *db, struct pkg *pkg, const char *tag,
        const char *value)
{
	int		 rows_changed; 
	const char	*pkgorigin;

	assert(pkg!= NULL);
	assert(tag != NULL);
	assert(value != NULL);

	if (!db->prstmt_initialized && prstmt_initialize(db) != EPKG_OK)
		return (EPKG_FATAL);

	if (pkgdb_transaction_begin(db->sqlite, NULL) != EPKG_OK)
		return (EPKG_FATAL);

	pkg_get(pkg, PKG_ORIGIN, &pkgorigin);

	if (run_prstmt(ANNOTATE_DEL1, pkgorigin, tag) != SQLITE_DONE
	    ||
	    run_prstmt(ANNOTATE1, tag) != SQLITE_DONE
	    ||
	    run_prstmt(ANNOTATE1, value) != SQLITE_DONE
	    ||
	    run_prstmt(ANNOTATE_ADD1, pkgorigin, tag, value) !=
	        SQLITE_DONE
	    ||
	    run_prstmt(ANNOTATE_DEL2) != SQLITE_DONE) {
		ERROR_SQLITE(db->sqlite);
		pkgdb_transaction_rollback(db->sqlite, NULL);

		return (EPKG_FATAL);
	}

	/* Something has gone very wrong if rows_changed != 1 here */

	rows_changed = sqlite3_changes(db->sqlite);

	if (pkgdb_transaction_commit(db->sqlite, NULL) != EPKG_OK)
		return (EPKG_FATAL);

	return (rows_changed == 1 ? EPKG_OK : EPKG_WARN);
}

int
pkgdb_delete_annotation(struct pkgdb *db, struct pkg *pkg, const char *tag)
{
	int		 rows_changed;
	bool		 result;
	const char	*pkgorigin;

	assert(pkg != NULL);
	assert(tag != NULL);

	if (!db->prstmt_initialized && prstmt_initialize(db) != EPKG_OK)
		return (EPKG_FATAL);

	if (pkgdb_transaction_begin(db->sqlite, NULL) != EPKG_OK)
		return (EPKG_FATAL);

	pkg_get(pkg, PKG_ORIGIN, &pkgorigin);

	result = (run_prstmt(ANNOTATE_DEL1, pkgorigin, tag)
		  == SQLITE_DONE);

	rows_changed = sqlite3_changes(db->sqlite);

	if (!result
	    ||
	    run_prstmt(ANNOTATE_DEL2) != SQLITE_DONE) {
		ERROR_SQLITE(db->sqlite);
		pkgdb_transaction_rollback(db->sqlite, NULL);
		return (EPKG_FATAL);
	}

	if (pkgdb_transaction_commit(db->sqlite, NULL) != EPKG_OK)
		return (EPKG_FATAL);

	return (rows_changed == 1 ? EPKG_OK : EPKG_WARN);
}


int
pkgdb_register_finale(struct pkgdb *db, int retcode)
{
	int	ret = EPKG_OK;

	assert(db != NULL);

	if (retcode == EPKG_OK) 
		ret = pkgdb_transaction_commit(db->sqlite, NULL);
	else
		ret = pkgdb_transaction_rollback(db->sqlite, NULL);

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
pkgdb_unregister_pkg(struct pkgdb *db, const char *origin)
{
	sqlite3_stmt	*stmt_del;
	unsigned int	 obj;
	int		 ret;
	const char	 sql[] = ""
		"DELETE FROM packages WHERE origin = ?1;";
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
	assert(origin != NULL);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt_del, NULL)
	    != SQLITE_OK){
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
		ERROR_SQLITE(s);
		sqlite3_free(errmsg);
		goto cleanup;
	}

	ret = EPKG_OK;

	cleanup:
	if (sqlbuf != NULL)
		sqlite3_free(sqlbuf);

	return (ret);
}

bool
pkgdb_is_attached(sqlite3 *s, const char *name)
{
	sqlite3_stmt	*stmt;
	const char	*dbname;
	int		 ret;

	assert(s != NULL);

	ret = sqlite3_prepare_v2(s, "PRAGMA database_list;", -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
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

int
pkgdb_sql_all_attached(sqlite3 *s, struct sbuf *sql, const char *multireposql,
    const char *compound)
{
	sqlite3_stmt	*stmt;
	const char	*dbname;
	int		 dbcount = 0;
	int		 ret;

	assert(s != NULL);
	assert(compound != NULL);

	ret = sqlite3_prepare_v2(s, "PRAGMA database_list;", -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(s);
		return (EPKG_FATAL);
	}

	while (sqlite3_step(stmt) != SQLITE_DONE) {
		dbname = sqlite3_column_text(stmt, 1);
		if ((strcmp(dbname, "main") == 0) ||
		    (strcmp(dbname, "temp") == 0))
			continue;

		dbcount++;

		if (dbcount > 1)
			sbuf_cat(sql, compound);

		/* replace any occurences of the dbname in the resulting SQL */
		sbuf_printf(sql, multireposql, dbname);
	}

	sqlite3_finalize(stmt);

	/* The generated SQL statement will not be syntactically
	 * correct unless there is at least one other attached DB than
	 * main or temp */

	return (dbcount > 0 ? EPKG_OK : EPKG_FATAL);
}

static void
pkgdb_detach_remotes(sqlite3 *s)
{
	sqlite3_stmt	*stmt;
	struct sbuf	*sql = NULL;
	const char	*dbname;
	int		 ret;

	assert(s != NULL);

	ret = sqlite3_prepare_v2(s, "PRAGMA database_list;", -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
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

int
get_pragma(sqlite3 *s, const char *sql, int64_t *res, bool silence)
{
	sqlite3_stmt	*stmt;
	int		 ret;
	int		 tries;

	assert(s != NULL && sql != NULL);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(s, sql, -1, &stmt, NULL) != SQLITE_OK) {
		if (!silence)
			ERROR_SQLITE(s);
		return (EPKG_OK);
	}

	for (tries = 0; tries < NTRIES; tries++) {
		ret = sqlite3_step(stmt);
		if (ret != SQLITE_BUSY)
			break;
		sqlite3_sleep(250);
	}

	if (ret == SQLITE_ROW)
		*res = sqlite3_column_int64(stmt, 0);

	sqlite3_finalize(stmt);

	if (ret != SQLITE_ROW) {
		if (!silence)
			ERROR_SQLITE(s);
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
		ERROR_SQLITE(s);
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
		ERROR_SQLITE(s);
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

static int
pkgdb_search_build_search_query(struct sbuf *sql, match_t match,
    pkgdb_field field, pkgdb_field sort)
{
	const char	*how = NULL;
	const char	*what = NULL;
	const char	*orderby = NULL;

	how = pkgdb_get_match_how(match);

	switch (field) {
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

	switch (sort) {
	case FIELD_NONE:
		orderby = NULL;
		break;
	case FIELD_ORIGIN:
		orderby = " ORDER BY origin";
		break;
	case FIELD_NAME:
		orderby = " ORDER BY name";
		break;
	case FIELD_NAMEVER:
		orderby = " ORDER BY name, version";
		break;
	case FIELD_COMMENT:
		orderby = " ORDER BY comment";
		break;
	case FIELD_DESC:
		orderby = " ORDER BY desc";
		break;
	}

	if (orderby != NULL)
		sbuf_cat(sql, orderby);

	return (EPKG_OK);
}

struct pkgdb_it *
pkgdb_search(struct pkgdb *db, const char *pattern, match_t match,
    pkgdb_field field, pkgdb_field sort, const char *reponame)
{
	sqlite3_stmt	*stmt = NULL;
	struct sbuf	*sql = NULL;
	int		 ret;
	const char	*rname;
	const char	*basesql = ""
		"SELECT id, origin, name, version, comment, "
		"prefix, desc, arch, maintainer, www, "
		"licenselogic, flatsize, pkgsize, "
		"cksum, path AS repopath ";
	const char	*multireposql = ""
		"SELECT id, origin, name, version, comment, "
		"prefix, desc, arch, maintainer, www, "
		"licenselogic, flatsize, pkgsize, "
		"cksum, path, '%1$s' AS dbname "
		"FROM '%1$s'.packages ";

	assert(db != NULL);
	assert(pattern != NULL && pattern[0] != '\0');
	assert(db->type == PKGDB_REMOTE);

	sql = sbuf_new_auto();
	sbuf_cat(sql, basesql);

	/* add the dbname column to the SELECT */
	sbuf_cat(sql, ", dbname FROM (");

	if (reponame != NULL) {
		if ((rname = pkgdb_get_reponame(db, reponame)) != NULL)
			sbuf_printf(sql, multireposql, rname, rname);
		else {
			pkg_emit_error("Repository %s can't be loaded",
					reponame);
			sbuf_delete(sql);
			return (NULL);
		}
	} else {
		if (pkg_repos_activated_count() == 0) {
			pkg_emit_error("No active repositories configured");
			sbuf_delete(sql);
			return (NULL);
		}
		/* test on all the attached databases */
		if (pkgdb_sql_all_attached(db->sqlite, sql,
		    multireposql, " UNION ALL ") != EPKG_OK) {
			sbuf_delete(sql);
			return (NULL);
		}
	}

	/* close the UNIONs and build the search query */
	sbuf_cat(sql, ") WHERE ");

	pkgdb_search_build_search_query(sql, match, field, sort);
	sbuf_cat(sql, ";");
	sbuf_finish(sql);

	pkg_debug(4, "Pkgdb: running '%s'", sbuf_get(sql));
	ret = sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		sbuf_delete(sql);
		return (NULL);
	}

	sbuf_delete(sql);

	sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE));
}

int
pkgdb_integrity_append(struct pkgdb *db, struct pkg *p,
		conflict_func_cb cb, void *cbdata)
{
	int		 ret = EPKG_OK;
	sqlite3_stmt	*stmt = NULL;
	sqlite3_stmt	*stmt_conflicts = NULL;
	struct pkg_file	*file = NULL;
	const char *porigin;

	const char	 sql[] = ""
		"INSERT INTO integritycheck (name, origin, version, path)"
		"values (?1, ?2, ?3, ?4);";
	const char	 sql_conflicts[] = ""
		"SELECT name, origin, version from integritycheck where path=?1;";

	assert(db != NULL && p != NULL);

	sql_exec(db->sqlite, "CREATE TEMP TABLE IF NOT EXISTS integritycheck ("
			"name TEXT, "
			"origin TEXT, "
			"version TEXT, "
			"path TEXT UNIQUE);"
		);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL)
	    != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg_get(p, PKG_ORIGIN, &porigin);

	pkg_debug(4, "Pkgdb: test conflicts for %s", porigin);
	while (pkg_files(p, &file) == EPKG_OK) {
		const char	*name, *origin, *version;
		const char	*pkg_path = pkg_file_path(file);
		struct pkg_event_conflict *conflicts_list = NULL, *cur;

		pkg_get(p, PKG_NAME, &name, PKG_ORIGIN, &origin,
		    PKG_VERSION, &version);
		sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, origin, -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 3, version, -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 4, pkg_path, -1, SQLITE_STATIC);

		if (sqlite3_step(stmt) != SQLITE_DONE) {

			pkg_debug(4, "Pkgdb: running '%s'", sql_conflicts);
			if (sqlite3_prepare_v2(db->sqlite, sql_conflicts,
			    -1, &stmt_conflicts, NULL) != SQLITE_OK) {
				ERROR_SQLITE(db->sqlite);
				sqlite3_finalize(stmt);
				return (EPKG_FATAL);
			}

			sqlite3_bind_text(stmt_conflicts, 1, pkg_path,
			    -1, SQLITE_STATIC);
			cur = conflicts_list;
			while (sqlite3_step(stmt_conflicts) != SQLITE_DONE) {

				cur = calloc(1, sizeof (struct pkg_event_conflict));
				cur->name = strdup(sqlite3_column_text(stmt_conflicts, 0));
				cur->origin = strdup(sqlite3_column_text(stmt_conflicts, 1));
				cur->version = strdup(sqlite3_column_text(stmt_conflicts, 2));
				pkg_debug(3, "found conflict between %s and %s on path %s",
						porigin, cur->origin, pkg_path);
				LL_PREPEND(conflicts_list, cur);

				if (cb != NULL)
					cb (porigin, cur->origin, cbdata);
			}
			sqlite3_finalize(stmt_conflicts);
			pkg_emit_integritycheck_conflict(name, version, origin, pkg_path, conflicts_list);
			cur = conflicts_list;
			while (cur) {
				free(cur->name);
				free(cur->origin);
				free(cur->version);
				cur = cur->next;
				free(conflicts_list);
				conflicts_list = cur;
			}
			ret = EPKG_CONFLICT;
		}
		sqlite3_reset(stmt);
	}
	sqlite3_finalize(stmt);

	return (ret);
}

int
pkgdb_integrity_check(struct pkgdb *db, conflict_func_cb cb, void *cbdata)
{
	int		 ret, retcode = EPKG_OK;
	sqlite3_stmt	*stmt;
	sqlite3_stmt	*stmt_conflicts;
	struct sbuf	*conflictmsg = NULL;
	struct sbuf *origin;

	assert (db != NULL);

	const char	 sql_local_conflict[] = ""
		"SELECT p.name, p.version, p.origin FROM packages AS p, files AS f "
		"WHERE p.id = f.package_id AND f.path = ?1;";

	const char	 sql_conflicts[] = ""
		"SELECT name, version, origin FROM integritycheck WHERE path = ?1;";

	const char sql_integrity_prepare[] = ""
		"SELECT f.path FROM files as f, integritycheck as i "
		"LEFT JOIN packages as p ON "
		"p.id = f.package_id "
		"WHERE f.path = i.path AND "
		"p.origin != i.origin GROUP BY f.path";

	pkg_debug(4, "Pkgdb: running '%s'", sql_integrity_prepare);
	if (sqlite3_prepare_v2(db->sqlite,
		sql_integrity_prepare,
		-1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	conflictmsg = sbuf_new_auto();
	origin = sbuf_new_auto();

	while (sqlite3_step(stmt) != SQLITE_DONE) {
		sbuf_clear(conflictmsg);
		sbuf_clear(origin);

		pkg_debug(4, "Pkgdb: running '%s'", sql_local_conflict);
		ret = sqlite3_prepare_v2(db->sqlite, sql_local_conflict, -1,
		    &stmt_conflicts, NULL);
		if (ret != SQLITE_OK) {
			ERROR_SQLITE(db->sqlite);
			sqlite3_finalize(stmt);
			sbuf_delete(origin);
			sbuf_delete(conflictmsg);
			return (EPKG_FATAL);
		}

		sqlite3_bind_text(stmt_conflicts, 1,
		    sqlite3_column_text(stmt, 0), -1, SQLITE_STATIC);

		sqlite3_step(stmt_conflicts);

		sbuf_printf(conflictmsg,
		    "WARNING: locally installed %s-%s conflicts on %s with:\n",
		    sqlite3_column_text(stmt_conflicts, 0),
		    sqlite3_column_text(stmt_conflicts, 1),
		    sqlite3_column_text(stmt, 0));
		sbuf_cpy(origin, sqlite3_column_text(stmt_conflicts, 2));
		sqlite3_finalize(stmt_conflicts);

		pkg_debug(4, "Pkgdb: running '%s'", sql_conflicts);
		ret = sqlite3_prepare_v2(db->sqlite, sql_conflicts, -1,
		    &stmt_conflicts, NULL);
		if (ret != SQLITE_OK) {
			ERROR_SQLITE(db->sqlite);
			sqlite3_finalize(stmt);
			sbuf_delete(conflictmsg);
			sbuf_delete(origin);
			return (EPKG_FATAL);
		}

		sbuf_finish(origin);

		sqlite3_bind_text(stmt_conflicts, 1,
		    sqlite3_column_text(stmt, 0), -1, SQLITE_STATIC);

		while (sqlite3_step(stmt_conflicts) != SQLITE_DONE) {
			sbuf_printf(conflictmsg, "\t- %s-%s\n",
			    sqlite3_column_text(stmt_conflicts, 0),
			    sqlite3_column_text(stmt_conflicts, 1));
			if (cb != NULL)
				cb (sbuf_data(origin), sqlite3_column_text(stmt_conflicts, 2), cbdata);
		}

		sbuf_finish(conflictmsg);
		sqlite3_finalize(stmt_conflicts);

		//pkg_emit_error("%s", sbuf_get(conflictmsg));
		retcode = EPKG_CONFLICT;
	}

	sqlite3_finalize(stmt);
	sbuf_delete(conflictmsg);
	sbuf_delete(origin);

	assert (sql_exec(db->sqlite, "DROP TABLE IF EXISTS integritycheck") == EPKG_OK);

	return (retcode);
}

struct pkgdb_it *
pkgdb_integrity_conflict_local(struct pkgdb *db, const char *origin)
{
	sqlite3_stmt	*stmt;
	int		 ret;

	assert(db != NULL && origin != NULL);

	const char	sql_conflicts [] = ""
		"SELECT DISTINCT p.id AS rowid, p.origin, p.name, p.version, "
		    "p.prefix "
		"FROM packages AS p, files AS f, integritycheck AS i "
		"WHERE p.id = f.package_id AND f.path = i.path "
		"AND i.origin = ?1 AND i.origin != p.origin";

	pkg_debug(4, "Pkgdb: running '%s'", sql_conflicts);
	ret = sqlite3_prepare_v2(db->sqlite, sql_conflicts, -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sqlite3_bind_text(stmt, 1, origin, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_INSTALLED, PKGDB_IT_FLAG_ONCE));
}

static int
pkgdb_vset(struct pkgdb *db, int64_t id, va_list ap)
{
	int		 attr;
	sqlite3_stmt	*stmt;
	int64_t		 automatic, flatsize, locked;
	char		*oldorigin;
	char		*neworigin;

	/* Ensure there is an entry for each of the pkg_set_attr enum values */
	const char *sql[PKG_SET_ORIGIN + 1] = {
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
	};

	while ((attr = va_arg(ap, int)) > 0) {
		pkg_debug(4, "Pkgdb: running '%s'", sql[attr]);
		if (sqlite3_prepare_v2(db->sqlite, sql[attr], -1, &stmt, NULL)
		    != SQLITE_OK) {
			ERROR_SQLITE(db->sqlite);
			return (EPKG_FATAL);
		}

		switch (attr) {
		case PKG_SET_FLATSIZE:
			flatsize = va_arg(ap, int64_t);
			sqlite3_bind_int64(stmt, 1, flatsize);
			sqlite3_bind_int64(stmt, 2, id);
			break;
		case PKG_SET_AUTOMATIC:
			automatic = (int64_t)va_arg(ap, int);
			if (automatic != 0 && automatic != 1) {
				sqlite3_finalize(stmt);
				continue;
			}
			sqlite3_bind_int64(stmt, 1, automatic);
			sqlite3_bind_int64(stmt, 2, id);
			break;
		case PKG_SET_LOCKED:
			locked = (int64_t)va_arg(ap, int);
			if (locked != 0 && locked != 1)
				continue;
			sqlite3_bind_int64(stmt, 1, locked);
			sqlite3_bind_int64(stmt, 2, id);
			break;
		case PKG_SET_DEPORIGIN:
			oldorigin = va_arg(ap, char *);
			neworigin = va_arg(ap, char *);
			sqlite3_bind_text(stmt, 1, neworigin, -1, SQLITE_STATIC);
			sqlite3_bind_int64(stmt, 2, id);
			sqlite3_bind_text(stmt, 3, oldorigin, -1, SQLITE_STATIC);
			break;
		case PKG_SET_ORIGIN:
			neworigin = va_arg(ap, char *);
			sqlite3_bind_text(stmt, 1, neworigin, -1, SQLITE_STATIC);
			sqlite3_bind_int64(stmt, 2, id);
			break;
		}

		if (sqlite3_step(stmt) != SQLITE_DONE) {
			ERROR_SQLITE(db->sqlite);
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
	int	ret = EPKG_OK;
	int64_t	id;

	va_list	ap;

	assert(pkg != NULL);

	va_start(ap, pkg);
	pkg_get(pkg, PKG_ROWID, &id);
	ret = pkgdb_vset(db, id, ap);
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
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}
	sqlite3_bind_text(stmt, 1, sha256, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, pkg_file_path(file), -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		ERROR_SQLITE(db->sqlite);
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
static int
sqlcmd_init(sqlite3 *db, __unused const char **err, 
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

	sqlite3_auto_extension((void(*)(void))sqlcmd_init);

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
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}
	sqlite3_bind_int64(stmt, 1, (int64_t)getpid());

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		ERROR_SQLITE(db->sqlite);
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
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}
	sqlite3_bind_int64(stmt, 1, pid);

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		ERROR_SQLITE(db->sqlite);
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

	ret = sqlite3_prepare_v2(db->sqlite, "SELECT pid FROM pkg_lock_pid;", -1,
			&stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	lpid = getpid();

	while (sqlite3_step(stmt) != SQLITE_DONE) {
		pid = sqlite3_column_int64(stmt, 0);
		if (pid != lpid) {
			if (kill((pid_t)pid, 0) == -1) {
				pkg_debug(1, "found stale pid %lld in lock database", pid);
				if (pkgdb_remove_lock_pid(db, pid) != EPKG_OK){
					sqlite3_finalize(stmt);
					return (EPKG_FATAL);
				}
			}
			else {
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
pkgdb_try_lock(struct pkgdb *db, const char *lock_sql,
		double delay, unsigned int retries, pkgdb_lock_t type,
		bool upgrade)
{
	unsigned int tries = 0;
	struct timespec ts;
	int ret = EPKG_END;

	while (tries <= retries) {
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
					return pkgdb_obtain_lock(db, type, delay, retries - tries);
				}
				continue;
			}
			else if (delay > 0) {
				ts.tv_sec = (int)delay;
				ts.tv_nsec = (delay - (int)delay) * 1000000000.;
				pkg_debug(1, "waiting for database lock for %d times, "
						"next try in %.2f seconds", tries, delay);
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
pkgdb_obtain_lock(struct pkgdb *db, pkgdb_lock_t type,
		double delay, unsigned int retries)
{
	int ret;
	const char table_sql[] = ""
			"CREATE TABLE pkg_lock "
			"(exclusive INTEGER(1), advisory INTEGER(1), read INTEGER(8));";
	const char pid_sql[] = ""
			"CREATE TABLE IF NOT EXISTS pkg_lock_pid "
			"(pid INTEGER PRIMARY KEY);";
	const char init_sql[] = ""
			"INSERT INTO pkg_lock VALUES(0,0,0);";
	const char readonly_lock_sql[] = ""
			"UPDATE pkg_lock SET read=read+1 WHERE exclusive=0;";
	const char advisory_lock_sql[] = ""
			"UPDATE pkg_lock SET advisory=1 WHERE exclusive=0 AND advisory=0;";
	const char exclusive_lock_sql[] = ""
			"UPDATE pkg_lock SET exclusive=1 WHERE exclusive=0 AND advisory=0 AND read=0;";
	const char *lock_sql = NULL;

	assert(db != NULL);

	ret = sqlite3_exec(db->sqlite, table_sql, NULL, NULL, NULL);
	if (ret == SQLITE_OK) {
		/* Need to initialize */
		ret = sqlite3_exec(db->sqlite, init_sql, NULL, NULL, NULL);
		if (ret != SQLITE_OK) {
			ERROR_SQLITE(db->sqlite);
			return (EPKG_FATAL);
		}
	}

	ret = sqlite3_exec(db->sqlite, pid_sql, NULL, NULL, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	switch (type) {
	case PKGDB_LOCK_READONLY:
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

	ret = pkgdb_try_lock(db, lock_sql, delay, retries, type, false);

	return (ret);
}

int
pkgdb_upgrade_lock(struct pkgdb *db, pkgdb_lock_t old_type, pkgdb_lock_t new_type,
		double delay, unsigned int retries)
{
	const char advisory_exclusive_lock_sql[] = ""
		"UPDATE pkg_lock SET exclusive=1,advisory=1 WHERE exclusive=0 AND advisory=1 AND read=0;";
	int ret = EPKG_FATAL;

	assert(db != NULL);

	if (old_type == PKGDB_LOCK_ADVISORY && new_type == PKGDB_LOCK_EXCLUSIVE) {
		pkg_debug(1, "want to upgrade advisory to exclusive lock");
		ret = pkgdb_try_lock(db, advisory_exclusive_lock_sql, delay, retries,
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
		sbuf_printf(sql, "SELECT COUNT(c) FROM ");

		/* open parentheses for the compound statement */
		sbuf_printf(sql, "(");

		/* execute on all databases */
		pkgdb_sql_all_attached(db->sqlite, sql,
		    "SELECT origin AS c FROM '%1$s'.packages", " UNION ");

		/* close parentheses for the compound statement */
		sbuf_printf(sql, ");");
		break;
	case PKG_STATS_REMOTE_COUNT:
		sbuf_printf(sql, "SELECT COUNT(c) FROM ");

		/* open parentheses for the compound statement */
		sbuf_printf(sql, "(");

		/* execute on all databases */
		pkgdb_sql_all_attached(db->sqlite, sql,
		    "SELECT origin AS c FROM '%1$s'.packages", " UNION ALL ");

		/* close parentheses for the compound statement */
		sbuf_printf(sql, ");");
		break;
	case PKG_STATS_REMOTE_SIZE:
		sbuf_printf(sql, "SELECT SUM(s) FROM ");

		/* open parentheses for the compound statement */
		sbuf_printf(sql, "(");

		/* execute on all databases */
		pkgdb_sql_all_attached(db->sqlite, sql,
		    "SELECT flatsize AS s FROM '%1$s'.packages", " UNION ALL ");

		/* close parentheses for the compound statement */
		sbuf_printf(sql, ");");
		break;
	case PKG_STATS_REMOTE_REPOS:
		sbuf_printf(sql, "SELECT COUNT(c) FROM ");

		/* open parentheses for the compound statement */
		sbuf_printf(sql, "(");

		/* execute on all databases */
		pkgdb_sql_all_attached(db->sqlite, sql,
		    "SELECT '%1$s' AS c", " UNION ALL ");

		/* close parentheses for the compound statement */
		sbuf_printf(sql, ");");
		break;
	}

	sbuf_finish(sql);
	pkg_debug(4, "Pkgdb: running '%s'", sbuf_data(sql));
	ret = sqlite3_prepare_v2(db->sqlite, sbuf_data(sql), -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		sbuf_free(sql);
		ERROR_SQLITE(db->sqlite);
		return (-1);
	}

	while (sqlite3_step(stmt) != SQLITE_DONE) {
		stats = sqlite3_column_int64(stmt, 0);
	}

	sbuf_free(sql);
	sqlite3_finalize(stmt);

	return (stats);
}
