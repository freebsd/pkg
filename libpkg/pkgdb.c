#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sqlite3.h>

#include "pkg.h"
#include "pkg_event.h"
#include "pkg_private.h"
#include "pkgdb.h"
#include "pkg_util.h"

#include "db_upgrades.h"
#define DBVERSION 4

static struct pkgdb_it * pkgdb_it_new(struct pkgdb *, sqlite3_stmt *, int);
static struct pkgdb_it * pkgdb_repos_new(struct pkgdb *);
static void pkgdb_regex(sqlite3_context *, int, sqlite3_value **, int);
static void pkgdb_regex_basic(sqlite3_context *, int, sqlite3_value **);
static void pkgdb_regex_extended(sqlite3_context *, int, sqlite3_value **);
static void pkgdb_regex_delete(void *);
static void pkgdb_pkglt(sqlite3_context *, int, sqlite3_value **);
static void pkgdb_pkggt(sqlite3_context *, int, sqlite3_value **);
static int get_pragma(sqlite3 *, const char *, int64_t *);
static int sql_exec(sqlite3 *, const char *);
static int pkgdb_upgrade(sqlite3 *);
static int pkgdb_rquery_build_search_query(struct sbuf *, match_t, unsigned int);

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
pkgdb_upgrade(sqlite3 *sdb)
{
	int64_t db_version = -1;
	char sql[30];
	int i;

	if (get_pragma(sdb, "PRAGMA user_version;", &db_version) != EPKG_OK)
		return (EPKG_FATAL);

	if (db_version == DBVERSION)
		return (EPKG_OK);
	else if (db_version > DBVERSION) {
		EMIT_PKG_ERROR("%s", "database version is newer than libpkg(3)");
		return (EPKG_FATAL);
	}

	while (db_version < DBVERSION) {
		db_version++;

		i = 0;
		while (db_upgrades[i].version != -1) {
			if (db_upgrades[i].version == db_version) {
				if (sql_exec(sdb, db_upgrades[i].sql) != EPKG_OK)
					return (EPKG_FATAL);

				i = 0;
				break;
			}
			i++;
		}

		/*
		 * We can't find the statements to upgrade to the next version,
		 * maybe because the current version is too old and upgrade support has
		 * been removed.
		 */
		if (i != 0) {
			EMIT_PKG_ERROR("can not upgrade to db version %" PRId64,
						   db_version);
			return (EPKG_FATAL);
		}

		snprintf(sql, sizeof(sql), "PRAGMA user_version = %" PRId64 ";", db_version);
		if (sql_exec(sdb, sql) != EPKG_OK)
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
		"osversion TEXT NOT NULL,"
		"maintainer TEXT NOT NULL,"
		"www TEXT,"
		"prefix TEXT NOT NULL,"
		"flatsize INTEGER NOT NULL,"
		"automatic INTEGER NOT NULL,"
		"licenselogic INTEGER NOT NULL,"
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
	"CREATE TABLE conflicts ("
		"name TEXT NOT NULL,"
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"PRIMARY KEY (package_id,name)"
	");"
	"CREATE TABLE directories ("
		"id INTEGER PRIMARY KEY, "
		"path TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE pkg_directories ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE, "
		"directory_id INTEGER REFERENCES directories(id) ON DELETE RESTRICT"
			" ON UPDATE RESTRICT, "
		"PRIMARY KEY (package_id, directory_id)"
	");"
	"CREATE TABLE categories ("
		"id INTEGER PRIMARY KEY, "
		"name TEXT NOT NULL UNIQUE"
	");"
	"CREATE TABLE pkg_categories ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE, "
		"category_id INTEGER REFERENCES categories(id) ON DELETE RESTRICT"
			" ON UPDATE RESTRICT, "
		"PRIMARY KEY (package_id, category_id)"
	");"
	"CREATE TABLE licenses ("
		"id INTEGER PRIMARY KEY, "
		"name TEXT NOT NULL UNIQUE "
	");"
	"CREATE TABLE pkg_licenses ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE, "
		"license_id INTEGER REFERENCES licenses(id) ON DELETE RESTRICT"
			" ON UPDATE RESTRICT, "
		"PRIMARY KEY (package_id, license_id)"
	");"
	"PRAGMA user_version = 4;"
	;

	return (sql_exec(sdb, sql));
}

static struct pkgdb_it *
pkgdb_repos_new(struct pkgdb *db)
{
	sqlite3_stmt *stmt = NULL;

	if (sqlite3_prepare_v2(db->sqlite, "PRAGMA database_list;", -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}
	
	return(pkgdb_it_new(db, stmt, PKG_REMOTE));
}

int
pkgdb_open(struct pkgdb **db, pkgdb_t type)
{
	int retcode = EPKG_OK;
	char *errmsg = NULL;
	char localpath[MAXPATHLEN];
	char remotepath[MAXPATHLEN];
	char tmpbuf[BUFSIZ];
	const char *dbdir = NULL;
	const char *repo_name = NULL;
	struct sbuf *sql = NULL;
	struct pkg_repos *repos = NULL;
	struct pkg_repos_entry *re = NULL;

	dbdir = pkg_config("PKG_DBDIR");

	if ((*db = calloc(1, sizeof(struct pkgdb))) == NULL) {
		EMIT_ERRNO("calloc", "pkgdb");
		return EPKG_FATAL;
	}

	(*db)->type = PKGDB_DEFAULT;

	snprintf(localpath, sizeof(localpath), "%s/local.sqlite", dbdir);
	retcode = access(localpath, R_OK);
	if (retcode == -1) {
		if (errno != ENOENT) {
			EMIT_ERRNO("access", localpath);
			free(*db);
			return (EPKG_FATAL);
		}
		else if (eaccess(dbdir, W_OK) != 0) {
			EMIT_ERRNO("eaccess", dbdir);
			free(*db);
			return (EPKG_FATAL);
		}
	}

	sqlite3_initialize();
	if (sqlite3_open(localpath, &(*db)->sqlite) != SQLITE_OK) {
		ERROR_SQLITE((*db)->sqlite);
		free(*db);
		return (EPKG_FATAL);
	}

	if (type == PKGDB_REMOTE) {
		if ((strcmp(pkg_config("PKG_MULTIREPOS"), "true") == 0) && \
				(pkg_config("PACKAGESITE") == NULL)) {
			fprintf(stderr, "\t/!\\		   WARNING WARNING WARNING		/!\\\n");
			fprintf(stderr, "\t/!\\	     WORKING ON MULTIPLE REPOSITORIES		/!\\\n");
			fprintf(stderr, "\t/!\\  THIS FEATURE IS STILL CONSIDERED EXPERIMENTAL	/!\\\n");
			fprintf(stderr, "\t/!\\		     YOU HAVE BEEN WARNED		/!\\\n\n");

			if (pkg_repos_conf_new(&repos) != EPKG_OK) {
				EMIT_PKG_ERROR("pkg_repos_new: %s", "cannot create multi repo object");
				return (EPKG_FATAL);
			}

			if (pkg_repos_conf_load(repos) != EPKG_OK) {
				EMIT_PKG_ERROR("pkg_repos_load: %s", "cannot load repositories");
				return (EPKG_FATAL);
			}

			sql = sbuf_new_auto();

			while (pkg_repos_conf_next(repos, &re) == EPKG_OK) {
				repo_name = pkg_repos_get_name(re);
				snprintf(remotepath, sizeof(remotepath), "%s/%s.sqlite",
						dbdir, repo_name);

				if (access(remotepath, R_OK) != 0) {
					EMIT_ERRNO("access", remotepath);
					sbuf_finish(sql);
					sbuf_delete(sql);
					return (EPKG_FATAL);
				}

				snprintf(tmpbuf, sizeof(tmpbuf), "ATTACH '%s' AS '%s';", remotepath, repo_name);
				sbuf_cat(sql, tmpbuf);
			}

			sbuf_finish(sql);

			if (sqlite3_exec((*db)->sqlite, sbuf_get(sql), NULL, NULL, &errmsg) != SQLITE_OK) {
				EMIT_PKG_ERROR("sqlite: %s", errmsg);
				sbuf_delete(sql);
				return (EPKG_FATAL);
			}

			sbuf_delete(sql);
			pkg_repos_conf_free(repos);
		} else {
			/*
			 * Working on a single remote repository
			 */

			snprintf(remotepath, sizeof(remotepath), "%s/repo.sqlite", dbdir);

			if (access(remotepath, R_OK) != 0) {
				EMIT_ERRNO("access", remotepath);
				return (EPKG_FATAL);
			}

			sqlite3_snprintf(sizeof(tmpbuf), tmpbuf, "ATTACH '%s' AS remote;", remotepath);

			if (sqlite3_exec((*db)->sqlite, tmpbuf, NULL, NULL, &errmsg) != SQLITE_OK) {
				EMIT_PKG_ERROR("sqlite: %s", errmsg);
				return (EPKG_FATAL);
			}
		}

		(*db)->type = PKGDB_REMOTE;
	}

	/* If the database is missing we have to initialize it */
	if (retcode == -1)
		if ((retcode = pkgdb_init((*db)->sqlite)) != EPKG_OK) {
			ERROR_SQLITE((*db)->sqlite);
			pkgdb_close(*db);
			return (EPKG_FATAL);
		}

	pkgdb_upgrade((*db)->sqlite);

	sqlite3_create_function((*db)->sqlite, "regexp", 2, SQLITE_ANY, NULL,
							pkgdb_regex_basic, NULL, NULL);
	sqlite3_create_function((*db)->sqlite, "eregexp", 2, SQLITE_ANY, NULL,
							pkgdb_regex_extended, NULL, NULL);
	sqlite3_create_function((*db)->sqlite, "pkglt", 2, SQLITE_ANY, NULL,
			pkgdb_pkglt, NULL, NULL);
	sqlite3_create_function((*db)->sqlite, "pkggt", 2, SQLITE_ANY, NULL,
			pkgdb_pkggt, NULL, NULL);

	/*
	 * allow foreign key option which will allow to have clean support for
	 * reinstalling
	 */
	return (sql_exec((*db)->sqlite, "PRAGMA foreign_keys = ON;"));
}

void
pkgdb_close(struct pkgdb *db)
{
	const char *repo_name = NULL;
	char   tmpbuf[BUFSIZ];
	struct sbuf *sql = NULL;
	struct pkgdb_it *it = NULL;

	if (db == NULL)
		return;

	if (db->sqlite != NULL) {
		if (db->type == PKGDB_REMOTE) {
			if ((strcmp(pkg_config("PKG_MULTIREPOS"), "true") == 0) && \
					(pkg_config("PACKAGESITE") == NULL)) {
				/*
				 * Working on multiple remote repositories.
				 * Detach the remote repositories from the main database
				 */
				if ((it = pkgdb_repos_new(db)) == NULL) {
					EMIT_PKG_ERROR("pkgdb_repos_new: %s", "cannot get the attached databases");
					return;
				}

				sql = sbuf_new_auto();

				while ((repo_name = pkgdb_repos_next(it)) != NULL) {
					snprintf(tmpbuf, sizeof(tmpbuf), "DETACH '%s';", repo_name);
					sbuf_cat(sql, tmpbuf);
				}

				sbuf_finish(sql);

				sqlite3_exec(db->sqlite, sbuf_get(sql), NULL, NULL, NULL);

				sbuf_delete(sql);
				pkgdb_it_free(it);
			} else {
				/*
				 * Working on a single remote repository.
				 * Detach it from the main database
				 */
				sqlite3_exec(db->sqlite, "DETACH remote;", NULL, NULL, NULL);
			}
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

	assert(db != NULL);

	if ((it = malloc(sizeof(struct pkgdb_it))) == NULL) {
		EMIT_ERRNO("malloc", "");
		sqlite3_finalize(s);
		return (NULL);
	}

	it->db = db;
	it->stmt = s;
	it->type = type;
	return (it);
}

const char *
pkgdb_repos_next(struct pkgdb_it *it)
{
	const char *dbname = NULL;

	assert(it != NULL);

	/* 
	 * Skip the 'main' and 'temp' databases
	 */
	switch(sqlite3_step(it->stmt)) {
		case SQLITE_ROW:
			dbname = sqlite3_column_text(it->stmt, 1);
			if ((strcmp(dbname, "main") == 0) || strcmp(dbname, "temp") == 0)
				return (pkgdb_repos_next(it));
			else
				return (dbname);
			break;
		case SQLITE_DONE:
		default:
			return (NULL);
	}

	return (NULL);
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

		pkg->rowid = sqlite3_column_int64(it->stmt, 0);
		pkg_set(pkg, PKG_ORIGIN, sqlite3_column_text(it->stmt, 1));
		pkg_set(pkg, PKG_NAME, sqlite3_column_text(it->stmt, 2));
		pkg_set(pkg, PKG_VERSION, sqlite3_column_text(it->stmt, 3));
		pkg_set(pkg, PKG_COMMENT, sqlite3_column_text(it->stmt, 4));
		pkg_set(pkg, PKG_DESC, sqlite3_column_text(it->stmt, 5));
		pkg_set(pkg, PKG_MESSAGE, sqlite3_column_text(it->stmt, 6));
		pkg_set(pkg, PKG_ARCH, sqlite3_column_text(it->stmt, 7));
		pkg_set(pkg, PKG_OSVERSION, sqlite3_column_text(it->stmt, 8));
		pkg_set(pkg, PKG_MAINTAINER, sqlite3_column_text(it->stmt, 9));
		pkg_set(pkg, PKG_WWW, sqlite3_column_text(it->stmt, 10));
		pkg_set(pkg, PKG_PREFIX, sqlite3_column_text(it->stmt, 11));
		pkg_setflatsize(pkg, sqlite3_column_int64(it->stmt, 12));

		if (it->type != PKG_REMOTE && it->type != PKG_UPGRADE)
			pkg_set_licenselogic(pkg, sqlite3_column_int64(it->stmt, 13));

		if (it->type == PKG_REMOTE) {
			pkg->type = PKG_REMOTE;
			pkg_setnewflatsize(pkg, sqlite3_column_int64(it->stmt, 11));
			pkg_setnewpkgsize(pkg, sqlite3_column_int64(it->stmt, 12));
			if ((strcmp(pkg_config("PKG_MULTIREPOS"), "true") == 0) && (pkg_config("PACKAGESITE") == NULL))
				pkg_addnewrepo(pkg, sqlite3_column_text(it->stmt, 15));
		}

		if (it->type == PKG_UPGRADE) {
			pkg->type = PKG_UPGRADE;
			pkg_set(pkg, PKG_NEWVERSION, sqlite3_column_text(it->stmt, 13));
			pkg_setnewflatsize(pkg, sqlite3_column_int64(it->stmt, 14));
			pkg_setnewpkgsize(pkg, sqlite3_column_int64(it->stmt, 15));
			pkg_set(pkg, PKG_REPOPATH, sqlite3_column_text(it->stmt, 16));
		}

		/* load only for PKG_INSTALLED */
		if (it->type != PKG_INSTALLED)
			return (EPKG_OK);

		if (flags & PKG_LOAD_DEPS)
			if ((ret = pkgdb_loaddeps(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_RDEPS)
			if ((ret = pkgdb_loadrdeps(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_CONFLICTS)
			if ((ret = pkgdb_loadconflicts(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_FILES)
			if ((ret = pkgdb_loadfiles(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_DIRS)
			if ((ret = pkgdb_loaddirs(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_SCRIPTS)
			if ((ret = pkgdb_loadscripts(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_OPTIONS)
			if ((ret = pkgdb_loadoptions(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_MTREE)
			if ((ret = pkgdb_loadmtree(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_CATEGORIES)
			if ((ret = pkgdb_loadcategory(it->db, pkg)) != EPKG_OK)
				return (ret);

		if (flags & PKG_LOAD_LICENSES)
			if ((ret = pkgdb_loadlicense(it->db, pkg)) != EPKG_OK)
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
	if (it != NULL) {
		sqlite3_finalize(it->stmt);
		free(it);
	}
}

struct pkgdb_it *
pkgdb_query(struct pkgdb *db, const char *pattern, match_t match)
{
	char sql[BUFSIZ];
	sqlite3_stmt *stmt;
	const char *comp = NULL;
	char *checkorigin = NULL;

	assert(match == MATCH_ALL || pattern != NULL);

	if (pattern != NULL)
		checkorigin = strchr(pattern, '/');

	switch (match) {
	case MATCH_ALL:
		comp = "";
		break;
	case MATCH_EXACT:
		if (checkorigin == NULL)
			comp = " WHERE p.name = ?1 "
				"OR p.name || \"-\" || p.version = ?1";
		else
			comp = " WHERE p.origin = ?1";
		break;
	case MATCH_GLOB:
		if (checkorigin == NULL)
			comp = " WHERE p.name GLOB ?1 "
				"OR p.name || \"-\" || p.version GLOB ?1";
		else
			comp = " WHERE p.origin GLOB ?1";
		break;
	case MATCH_REGEX:
		if (checkorigin == NULL)
			comp = " WHERE p.name REGEXP ?1 "
				"OR p.name || \"-\" || p.version REGEXP ?1";
		else
			comp = " WHERE p.origin REGEXP ?1";
		break;
	case MATCH_EREGEX:
		if (checkorigin == NULL)
			comp = " WHERE EREGEXP(?1, p.name) "
				"OR EREGEXP(?1, p.name || \"-\" || p.version)";
		else
			comp = " WHERE EREGEXP(?1, p.origin)";
		break;
	}

	snprintf(sql, sizeof(sql),
			"SELECT p.rowid, p.origin, p.name, p.version, p.comment, p.desc, "
				"p.message, p.arch, p.osversion, p.maintainer, p.www, "
				"p.prefix, p.flatsize, p.licenselogic "
			"FROM packages AS p%s "
			"ORDER BY p.name;", comp);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	if (match != MATCH_ALL)
		sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_INSTALLED));
}

struct pkg *
pkgdb_query_remote(struct pkgdb *db, const char *pattern)
{
	sqlite3_stmt *stmt = NULL;
	sqlite3_stmt *stmt_deps = NULL;
	struct pkg *pkg = NULL;
	struct sbuf *sql = NULL;
	struct pkgdb_it *it = NULL;
	int ret, multi_repos = 0;
	char  tmpbuf[BUFSIZ];
	const char *dbname = NULL;
	const char *basesql = ""
		"SELECT rowid, origin, name, version, comment, desc, "
			"arch, osversion, maintainer, www, pkgsize, "
			"flatsize, cksum, path ";
	const char *multireposql = ""
		"SELECT rowid, origin, name, version, comment, desc, "
			"arch, osversion, maintainer, www, pkgsize, "
			"flatsize, cksum, path, '%s' AS dbname "
		"FROM '%s'.packages "
		"WHERE origin = ?1 ";
	const char *sql_deps = ""
		"SELECT d.name, d.origin, d.version "
		"FROM '%s'.deps AS d "
		"WHERE d.package_id = ?1 "
			"AND NOT EXISTS (SELECT 1 FROM main.packages AS p "
			"WHERE p.origin = d.origin)";

	assert(db != NULL && db->type == PKGDB_REMOTE);

	sql = sbuf_new_auto();
	sbuf_cat(sql, basesql);

	if ((strcmp(pkg_config("PKG_MULTIREPOS"), "true") == 0) && \
			(pkg_config("PACKAGESITE") == NULL)) {
		/*
		 * Working on multiple remote repositories
		 */

		multi_repos = 1;

		/* add the dbname column to the SELECT */
		sbuf_cat(sql, ", dbname FROM ");

		if ((it = pkgdb_repos_new(db)) == NULL) {
			EMIT_PKG_ERROR("%s", "cannot get the attached databases");
			return (NULL);
		}

		/* get the first repository entry (attached database) */
		if ((dbname = pkgdb_repos_next(it)) != NULL) {
			sbuf_cat(sql, "(");
			snprintf(tmpbuf, sizeof(tmpbuf), multireposql, dbname, dbname);
			sbuf_cat(sql, tmpbuf);
		} else {
			/* there are no remote databases attached */
			sbuf_finish(sql);
			sbuf_delete(sql);
			pkgdb_it_free(it);
			return (NULL);
		}

		while ((dbname = pkgdb_repos_next(it)) != NULL) {
			sbuf_cat(sql, "UNION ");
			snprintf(tmpbuf, sizeof(tmpbuf), multireposql, dbname, dbname);
			sbuf_cat(sql, tmpbuf);
		}

		sbuf_cat(sql, ");");
		sbuf_finish(sql);
		pkgdb_it_free(it);
	} else {
		/* 
		 * Working on a single remote repository
		 */

		sbuf_cat(sql, "FROM remote.packages WHERE origin = ?1;");
		sbuf_finish(sql);
	}

	if (sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sbuf_delete(sql);

	sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);

	ret = sqlite3_step(stmt);
	if (ret != SQLITE_ROW) {
		if (ret != SQLITE_DONE)
			ERROR_SQLITE(db->sqlite);
		goto cleanup;
	}

	pkg_new(&pkg, PKG_REMOTE);

	pkg->rowid = sqlite3_column_int64(stmt, 0);
	pkg_set(pkg, PKG_ORIGIN, sqlite3_column_text(stmt, 1));
	pkg_set(pkg, PKG_NAME, sqlite3_column_text(stmt, 2));
	pkg_set(pkg, PKG_VERSION, sqlite3_column_text(stmt, 3));
	pkg_set(pkg, PKG_COMMENT, sqlite3_column_text(stmt, 4));
	pkg_set(pkg, PKG_DESC, sqlite3_column_text(stmt, 5));
	pkg_set(pkg, PKG_ARCH, sqlite3_column_text(stmt, 6));
	pkg_set(pkg, PKG_OSVERSION, sqlite3_column_text(stmt, 7));
	pkg_set(pkg, PKG_MAINTAINER, sqlite3_column_text(stmt, 8));
	pkg_set(pkg, PKG_WWW, sqlite3_column_text(stmt, 9));
	pkg_setnewpkgsize(pkg, sqlite3_column_int64(stmt, 10));
	pkg_setnewflatsize(pkg, sqlite3_column_int64(stmt, 11));
	pkg_set(pkg, PKG_CKSUM, sqlite3_column_text(stmt, 12));
	pkg_set(pkg, PKG_REPOPATH, sqlite3_column_text(stmt, 13));

	if (multi_repos == 1) {
		pkg_addnewrepo(pkg, sqlite3_column_text(stmt, 14));
		/* we do the search of deps only in the repository of pkg */
		snprintf(tmpbuf, sizeof(tmpbuf), sql_deps, sqlite3_column_text(stmt, 14));
	} else {
		/* the search of deps is only in the 'remote' database (PACKAGESITE is set) */
		snprintf(tmpbuf, sizeof(tmpbuf), sql_deps, "remote");
	}

	if (sqlite3_prepare_v2(db->sqlite, tmpbuf, -1, &stmt_deps, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sqlite3_bind_int64(stmt_deps, 1, pkg->rowid);
	while ((ret = sqlite3_step(stmt_deps)) == SQLITE_ROW) {
		pkg_adddep(pkg, sqlite3_column_text(stmt_deps, 0),
				   sqlite3_column_text(stmt_deps, 1),
				   sqlite3_column_text(stmt_deps, 2));
	}

	cleanup:
	if (stmt != NULL)
		sqlite3_finalize(stmt);
	if (stmt_deps != NULL)
		sqlite3_finalize(stmt_deps);
	return (pkg);
}

struct pkgdb_it *
pkgdb_query_which(struct pkgdb *db, const char *path)
{
	sqlite3_stmt *stmt;
	const char sql[] = ""
		"SELECT p.rowid, p.origin, p.name, p.version, p.comment, p.desc, "
			"p.message, p.arch, p.osversion, p.maintainer, p.www, "
			"p.prefix, p.flatsize "
			"FROM packages AS p, files AS f "
			"WHERE p.rowid = f.package_id "
				"AND f.path = ?1;";

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_INSTALLED));
}

int
pkgdb_loaddeps(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	int ret;
	const char sql[] = ""
	"SELECT p.name, p.origin, p.version "
	"FROM packages AS p, deps AS d "
	"WHERE p.origin = d.origin "
		"AND d.package_id = ?1;";

	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_DEPS)
		return (EPKG_OK);

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
		pkg_freedeps(pkg);
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_DEPS;
	return (EPKG_OK);
}

int
pkgdb_loadrdeps(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	int ret;
	const char sql[] = ""
		"SELECT p.name, p.origin, p.version "
		"FROM packages AS p, deps AS d "
		"WHERE p.rowid = d.package_id "
			"AND d.origin = ?1;";

	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_RDEPS)
		return (EPKG_OK);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	sqlite3_bind_text(stmt, 1, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addrdep(pkg, sqlite3_column_text(stmt, 0), sqlite3_column_text(stmt, 1),
				   sqlite3_column_text(stmt, 2));
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		pkg_freerdeps(pkg);
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_RDEPS;
	return (EPKG_OK);
}

int
pkgdb_loadfiles(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	int ret;
	const char sql[] = ""
		"SELECT path, sha256 "
		"FROM files "
		"WHERE package_id = ?1 "
		"ORDER BY PATH ASC";

	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_FILES)
		return (EPKG_OK);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	sqlite3_bind_int64(stmt, 1, pkg->rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addfile(pkg, sqlite3_column_text(stmt, 0), sqlite3_column_text(stmt, 1));
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		pkg_freefiles(pkg);
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_FILES;
	return (EPKG_OK);
}

int
pkgdb_loaddirs(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	int ret;
	const char sql[] = ""
		"SELECT path "
		"FROM pkg_directories, directories "
		"WHERE package_id = (SELECT id from packages where origin = ?1 ) "
		"AND directory_id = directories.id "
		"ORDER by path DESC";

	if (pkg->flags & PKG_LOAD_DIRS)
		return (EPKG_OK);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	sqlite3_bind_text(stmt, 1, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_adddir(pkg, sqlite3_column_text(stmt, 0));
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		pkg_freedirs(pkg);
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_DIRS;
	return (EPKG_OK);
}

int
pkgdb_loadlicense(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	int ret;
	const char sql[] = ""
		"SELECT name "
		"FROM pkg_licenses, licenses "
		"WHERE package_id = (select id from packages where origin = ?1) "
		"AND license_id = licenses.id "
		"ORDER by name DESC";

	if (pkg->flags & PKG_LOAD_LICENSES)
		return (EPKG_OK);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	sqlite3_bind_text(stmt, 1, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);

	while (( ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addlicense(pkg, sqlite3_column_text(stmt, 0));
	}

	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		pkg_freelicenses(pkg);
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_LICENSES;
	return (EPKG_OK);
}

int
pkgdb_loadcategory(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	int ret;
	const char sql[] = ""
		"SELECT categories.name "
		"FROM pkg_categories, categories "
		"WHERE package_id = (select id from packages where origin = ?1) "
		"AND category_id = categories.id "
		"ORDER by name DESC";

	if (pkg->flags & PKG_LOAD_CATEGORIES)
		return (EPKG_OK);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	sqlite3_bind_text(stmt, 1, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);

	while (( ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addcategory(pkg, sqlite3_column_text(stmt, 0));
	}

	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		pkg_freecategories(pkg);
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_CATEGORIES;
	return (EPKG_OK);
}

int
pkgdb_loadconflicts(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	int ret;
	const char sql[] = ""
		"SELECT name "
		"FROM conflicts "
		"WHERE package_id = ?1;";

	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_CONFLICTS)
		return (EPKG_OK);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	sqlite3_bind_int64(stmt, 1, pkg->rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addconflict(pkg, sqlite3_column_text(stmt, 0));
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		pkg_freeconflicts(pkg);
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_CONFLICTS;

	return (EPKG_OK);
}

int
pkgdb_loadscripts(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	int ret;
	const char sql[] = ""
		"SELECT script, type "
		"FROM scripts "
		"WHERE package_id = ?1";

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
		pkg_freescripts(pkg);
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_SCRIPTS;
	return (EPKG_OK);
}

int
pkgdb_loadoptions(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	int ret;
	const char sql[] = ""
		"SELECT option, value "
		"FROM options "
		"WHERE package_id = ?1";

	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_OPTIONS)
		return (EPKG_OK);

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
		pkg_freeoptions(pkg);
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_OPTIONS;
	return (EPKG_OK);
}

int
pkgdb_loadmtree(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	int ret;
	const char sql[] = ""
		"SELECT m.content "
		"FROM mtree AS m, packages AS p "
		"WHERE m.id = p.mtree_id "
			" AND p.id = ?1;";

	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_MTREE)
		return (EPKG_OK);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_FATAL);
	}

	sqlite3_bind_int64(stmt, 1, pkg->rowid);

	ret = sqlite3_step(stmt);
	if (ret == SQLITE_ROW) {
		sbuf_set(&pkg->fields[PKG_MTREE].value,
				 sqlite3_column_text(stmt, 0));
		ret = SQLITE_DONE;
	}

	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		ERROR_SQLITE(db->sqlite);
		return (EPKG_OK);
	}

	pkg->flags |= PKG_LOAD_MTREE;
	return (EPKG_OK);
}

int
pkgdb_has_flag(struct pkgdb *db, int flag)
{
	return (db->flags & flag);
}

#define	PKGDB_SET_FLAG(db, flag) \
	(db)->flags |= (flag)
#define	PKGDB_UNSET_FLAG(db, flag) \
	(db)->flags &= ~(flag)

int
pkgdb_register_pkg(struct pkgdb *db, struct pkg *pkg)
{
	struct pkg_dep *dep = NULL;
	struct pkg_file *file = NULL;
	struct pkg_dir *dir = NULL;
	struct pkg_conflict *conflict = NULL;
	struct pkg_script *script = NULL;
	struct pkg_option *option = NULL;
	struct pkg_category *category = NULL;
	struct pkg_license *license = NULL;

	sqlite3 *s;
	sqlite3_stmt *stmt_pkg = NULL;
	sqlite3_stmt *stmt_mtree = NULL;
	sqlite3_stmt *stmt_sel_pkg = NULL;
	sqlite3_stmt *stmt_dep = NULL;
	sqlite3_stmt *stmt_conflict = NULL;
	sqlite3_stmt *stmt_file = NULL;
	sqlite3_stmt *stmt_script = NULL;
	sqlite3_stmt *stmt_option = NULL;
	sqlite3_stmt *stmt_dirs = NULL;
	sqlite3_stmt *stmt_dir = NULL;
	sqlite3_stmt *stmt_categories = NULL;
	sqlite3_stmt *stmt_cat = NULL;
	sqlite3_stmt *stmt_licenses = NULL;
	sqlite3_stmt *stmt_lic = NULL;

	int ret;
	int retcode = EPKG_FATAL;
	int64_t package_id;

	const char sql_begin[] = "BEGIN;";
	const char sql_mtree[] = "INSERT OR IGNORE INTO mtree(content) VALUES(?1);";
	const char sql_dirs[] = "INSERT OR IGNORE INTO directories(path) VALUES(?1);";
	const char sql_pkg[] = ""
		"INSERT OR REPLACE INTO packages( "
			"origin, name, version, comment, desc, message, arch, "
			"osversion, maintainer, www, prefix, flatsize, automatic, licenselogic, "
			"mtree_id) "
		"VALUES( ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, "
		"(SELECT id from mtree where content = ?15));";
	const char sql_sel_pkg[] = ""
		"SELECT id FROM packages "
		"WHERE origin = ?1;";
	const char sql_dep[] = ""
		"INSERT OR ROLLBACK INTO deps (origin, name, version, package_id) "
		"VALUES (?1, ?2, ?3, ?4);";
	const char sql_conflict[] = ""
		"INSERT OR ROLLBACK INTO conflicts (name, package_id) "
		"VALUES (?1, ?2);";
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
		"INSERT OR ROLLBACK INTO pkg_directories(package_id, directory_id) "
		"VALUES (?1, "
		"(SELECT id FROM directories WHERE path = ?2));";
	const char sql_cat[] = "INSERT OR IGNORE INTO categories(name) VALUES(?1);";
	const char sql_category[] = ""
		"INSERT OR ROLLBACK INTO pkg_categories(package_id, category_id) "
		"VALUES (?1, (SELECT id FROM categories WHERE name = ?2));";
	const char sql_lic[] = "INSERT OR IGNORE INTO licenses(name) VALUES(?1);";
	const char sql_license[] = ""
		"INSERT OR ROLLBACK INTO pkg_licenses(package_id, license_id) "
		"VALUES (?1, (SELECT id FROM licenses WHERE name = ?2));";

	if (pkgdb_has_flag(db, PKGDB_FLAG_IN_FLIGHT)) {
		EMIT_PKG_ERROR("%s", "tried to register a package with an in-flight SQL command");
		return (EPKG_FATAL);
	}

	s = db->sqlite;

	if (sql_exec(s, sql_begin) != EPKG_OK)
		return (EPKG_FATAL);

	PKGDB_SET_FLAG(db, PKGDB_FLAG_IN_FLIGHT);

	/* insert mtree record if any */
	if (sqlite3_prepare_v2(s, sql_mtree, -1, &stmt_mtree, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	sqlite3_bind_text(stmt_mtree, 1, pkg_get(pkg, PKG_MTREE), -1, SQLITE_STATIC);

	if ((ret = sqlite3_step(stmt_mtree)) != SQLITE_DONE) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	/* Insert package record */
	if (sqlite3_prepare_v2(s, sql_pkg, -1, &stmt_pkg, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}
	sqlite3_bind_text(stmt_pkg, 1, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 2, pkg_get(pkg, PKG_NAME), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 3, pkg_get(pkg, PKG_VERSION), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 4, pkg_get(pkg, PKG_COMMENT), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 5, pkg_get(pkg, PKG_DESC), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 6, pkg_get(pkg, PKG_MESSAGE), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 7, pkg_get(pkg, PKG_ARCH), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 8, pkg_get(pkg, PKG_OSVERSION), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 9, pkg_get(pkg, PKG_MAINTAINER), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 10, pkg_get(pkg, PKG_WWW), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 11, pkg_get(pkg, PKG_PREFIX), -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt_pkg, 12, pkg_flatsize(pkg));
	sqlite3_bind_int(stmt_pkg, 13, pkg_isautomatic(pkg));
	sqlite3_bind_int64(stmt_pkg, 14, pkg_licenselogic(pkg));
	sqlite3_bind_text(stmt_pkg, 15, pkg_get(pkg, PKG_MTREE), -1, SQLITE_STATIC);

	if ((ret = sqlite3_step(stmt_pkg)) != SQLITE_DONE) {
		ERROR_SQLITE(s);
		goto cleanup;
	}
	/*
	 * Get the generated package_id
	 */

	if (sqlite3_prepare_v2(s, sql_sel_pkg, -1, &stmt_sel_pkg, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}
	sqlite3_bind_text(stmt_sel_pkg, 1, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);
	ret = sqlite3_step(stmt_sel_pkg);
	if (ret == SQLITE_ROW) {
		package_id = sqlite3_column_int64(stmt_sel_pkg, 0);
	} else {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	/*
	 * Insert dependencies list
	 */

	if (sqlite3_prepare_v2(s, sql_dep, -1, &stmt_dep, NULL) != SQLITE_OK) {

		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		sqlite3_bind_text(stmt_dep, 1, pkg_dep_origin(dep), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_dep, 2, pkg_dep_name(dep), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_dep, 3, pkg_dep_version(dep), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt_dep, 4, package_id);

		if ((ret = sqlite3_step(stmt_dep)) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt_dep);
	}

	/*
	 * Insert conflicts list
	 */

	if (sqlite3_prepare_v2(s, sql_conflict, -1, &stmt_conflict, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_conflicts(pkg, &conflict) == EPKG_OK) {
		sqlite3_bind_text(stmt_conflict, 1, pkg_conflict_glob(conflict), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt_conflict, 2, package_id);

		if ((ret = sqlite3_step(stmt_conflict)) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt_conflict);
	}

	/*
	 * Insert files.
	 */

	if (sqlite3_prepare_v2(s, sql_file, -1, &stmt_file, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_files(pkg, &file) == EPKG_OK) {
		sqlite3_bind_text(stmt_file, 1, pkg_file_path(file), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_file, 2, pkg_file_sha256(file), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt_file, 3, package_id);

		if ((ret = sqlite3_step(stmt_file)) != SQLITE_DONE) {
			if (ret == SQLITE_CONSTRAINT) {
				    EMIT_PKG_ERROR("sqlite: constraint violation on files.path:"
								   " %s", pkg_file_path(file));
			} else {
				ERROR_SQLITE(s);
			}
			goto cleanup;
		}
		sqlite3_reset(stmt_file);
	}

	/*
	 * Insert dirs.
	 */

	if (sqlite3_prepare_v2(s, sql_dirs, -1, &stmt_dirs, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	if (sqlite3_prepare_v2(s, sql_dir, -1, &stmt_dir, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		sqlite3_bind_text(stmt_dirs, 1, pkg_dir_path(dir), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt_dir, 1, package_id);
		sqlite3_bind_text(stmt_dir, 2, pkg_dir_path(dir), -1, SQLITE_STATIC);
			
		if ((ret = sqlite3_step(stmt_dirs)) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		if ((ret = sqlite3_step(stmt_dir)) != SQLITE_DONE) {
			if ( ret == SQLITE_CONSTRAINT) {
				EMIT_PKG_ERROR("sqlite: constraint violation on dirs.path: %s",
			 					pkg_dir_path(dir));
			} else
				ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt_dir);
		sqlite3_reset(stmt_dirs);
	}

	/*
	 * Insert categories
	 */

	if (sqlite3_prepare_v2(s, sql_category, -1, &stmt_cat, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}
	if (sqlite3_prepare_v2(s, sql_cat, -1, &stmt_categories, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_categories(pkg, &category) == EPKG_OK) {
		sqlite3_bind_text(stmt_categories, 1, pkg_category_name(category), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt_cat, 1, package_id);
		sqlite3_bind_text(stmt_cat, 2, pkg_category_name(category), -1, SQLITE_STATIC);

		if ((ret = sqlite3_step(stmt_cat)) != SQLITE_DONE) {
			if (ret == SQLITE_CONSTRAINT) {
				EMIT_PKG_ERROR("sqlite: constraint violation on categories.name: %s",
						pkg_category_name(category));
			} else
				ERROR_SQLITE(s);
			goto cleanup;
		}
		if (( ret = sqlite3_step(stmt_categories)) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt_cat);
		sqlite3_reset(stmt_categories);
	}

	/*
	 * Insert licenses
	 */
	if (sqlite3_prepare_v2(s, sql_lic, -1, &stmt_licenses, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}
	if (sqlite3_prepare_v2(s, sql_license, -1, &stmt_lic, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_licenses(pkg, &license) == EPKG_OK) {
		sqlite3_bind_text(stmt_licenses, 1, pkg_license_name(license), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt_lic, 1, package_id);
		sqlite3_bind_text(stmt_lic, 2, pkg_license_name(license), -1, SQLITE_STATIC);

		if ((ret = sqlite3_step(stmt_lic)) != SQLITE_DONE) {
			if (ret == SQLITE_CONSTRAINT) {
				EMIT_PKG_ERROR("sqlite: constraint violation on licenses.name: %s",
						pkg_license_name(license));
			} else
				ERROR_SQLITE(s);
			goto cleanup;
		}
		if (( ret = sqlite3_step(stmt_licenses)) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt_lic);
		sqlite3_reset(stmt_licenses);
	}

	/*
	 * Insert scripts
	 */

	if (sqlite3_prepare_v2(s, sql_script, -1, &stmt_script, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_scripts(pkg, &script) == EPKG_OK) {
		sqlite3_bind_text(stmt_script, 1, pkg_script_data(script), -1, SQLITE_STATIC);
		sqlite3_bind_int(stmt_script, 2, pkg_script_type(script));
		sqlite3_bind_int64(stmt_script, 3, package_id);

		if (sqlite3_step(stmt_script) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt_script);
	}

	/*
	 * Insert options
	 */

	if (sqlite3_prepare_v2(s, sql_option, -1, &stmt_option, NULL) != SQLITE_OK) {
		ERROR_SQLITE(s);
		goto cleanup;
	}

	while (pkg_options(pkg, &option) == EPKG_OK) {
		sqlite3_bind_text(stmt_option, 1, pkg_option_opt(option), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_option, 2, pkg_option_value(option), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt_option, 3, package_id);

		if (sqlite3_step(stmt_option) != SQLITE_DONE) {
			ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt_option);
	}

	retcode = EPKG_OK;

	cleanup:

	if (stmt_mtree != NULL)
		sqlite3_finalize(stmt_mtree);

	if (stmt_pkg != NULL)
		sqlite3_finalize(stmt_pkg);

	if (stmt_sel_pkg != NULL)
		sqlite3_finalize(stmt_sel_pkg);

	if (stmt_dep != NULL)
		sqlite3_finalize(stmt_dep);

	if (stmt_conflict != NULL)
		sqlite3_finalize(stmt_conflict);

	if (stmt_file != NULL)
		sqlite3_finalize(stmt_file);

	if (stmt_script != NULL)
		sqlite3_finalize(stmt_script);

	if (stmt_option != NULL)
		sqlite3_finalize(stmt_option);
	
	if (stmt_dirs != NULL)
		sqlite3_finalize(stmt_dirs);

	if (stmt_dir != NULL)
		sqlite3_finalize(stmt_dir);

	if (stmt_cat != NULL)
		sqlite3_finalize(stmt_cat);

	if (stmt_categories != NULL)
		sqlite3_finalize(stmt_categories);

	if (stmt_lic != NULL)
		sqlite3_finalize(stmt_lic);

	if (stmt_licenses != NULL)
		sqlite3_finalize(stmt_licenses);

	return (retcode);
}

int
pkgdb_register_finale(struct pkgdb *db, int retcode)
{
	int ret = EPKG_OK;
	const char *commands[] = { "COMMIT;", "ROLLBACK;", NULL };
	const char *command;

	if (!pkgdb_has_flag(db, PKGDB_FLAG_IN_FLIGHT)) {
		EMIT_PKG_ERROR("%s", "database command not in flight (misuse)");
		return EPKG_FATAL;
	}

	command = (retcode == EPKG_OK) ? commands[0] : commands[1];
	ret = sql_exec(db->sqlite, command);

	PKGDB_UNSET_FLAG(db, PKGDB_FLAG_IN_FLIGHT);

	return ret;
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
	if (sql_exec(db->sqlite, "DELETE from directories WHERE id NOT IN (SELECT DISTINCT directory_id FROM pkg_dirs_assoc);") != EPKG_OK)
		return (EPKG_FATAL);

	if (sql_exec(db->sqlite, "DELETE from categories WHERE id NOT IN (SELECT DISTINCT category_id FROM pkg_categories_assoc);") != EPKG_OK)
		return (EPKG_FATAL);

	if (sql_exec(db->sqlite, "DELETE from licenses WHERE id NOT IN (SELECT DISTINCT license_id FROM pkg_licenses_assoc);") != EPKG_OK)
		return (EPKG_FATAL);

	if (sql_exec(db->sqlite, "DELETE FROM mtree WHERE id NOT IN (SELECT DISTINCT mtree_id FROM packages);") != EPKG_OK)
		return (EPKG_FATAL);

	return (EPKG_OK);
}

static int sql_exec(sqlite3 *s, const char *sql)
{
	char *errmsg;

	if (sqlite3_exec(s, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
		EMIT_PKG_ERROR("sqlite: %s", errmsg);
		sqlite3_free(errmsg);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

static int get_pragma(sqlite3 *s, const char *sql, int64_t *res) {
	sqlite3_stmt *stmt;
	int ret;

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

struct pkgdb_it *
pkgdb_query_upgrades(struct pkgdb *db)
{
	sqlite3_stmt *stmt;

	if (db->type != PKGDB_REMOTE) {
		EMIT_PKG_ERROR("%s", "remote database not attached (misuse)");
		return (NULL);
	}

	const char sql[] = ""
		"SELECT l.id, l.origin, l.name, l.version, l.comment, l.desc, "
		"l.message, l.arch, l.osversion, l.maintainer, "
		"l.www, l.prefix, l.flatsize, r.version, r.flatsize, r.pkgsize, r.path "
		"FROM main.packages AS l, "
		"remote.packages AS r "
		"WHERE l.origin = r.origin "
		"AND PKGLT(l.version, r.version)";

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	return (pkgdb_it_new(db, stmt, PKG_UPGRADE));
}

struct pkgdb_it *
pkgdb_query_downgrades(struct pkgdb *db)
{
	sqlite3_stmt *stmt;

	if (db->type != PKGDB_REMOTE) {
		EMIT_PKG_ERROR("%s", "remote database not attached (misuse)");
		return (NULL);
	}

	const char sql[] = ""
		"SELECT l.id, l.origin, l.name, l.version, l.comment, l.desc, "
		"l.message, l.arch, l.osversion, l.maintainer, "
		"l.www, l.prefix, l.flatsize, r.version, r.flatsize, r.pkgsize, r.path "
		"FROM main.packages AS l, "
		"remote.packages AS r "
		"WHERE l.origin = r.origin "
		"AND PKGGT(l.version, r.version)";

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	return (pkgdb_it_new(db, stmt, PKG_UPGRADE));
}

struct pkgdb_it *
pkgdb_query_autoremove(struct pkgdb *db)
{
	sqlite3_stmt *stmt;

	const char sql[] = ""
		"SELECT id, origin, name, version, comment, desc, "
		"message, arch, osversion, maintainer, www, prefix, "
		"flatsize FROM packages WHERE automatic=1 AND "
		"(SELECT deps.origin FROM deps where deps.origin = packages.origin) "
		"IS NULL";

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	return (pkgdb_it_new(db, stmt, PKG_INSTALLED));
}

static int
pkgdb_rquery_build_search_query(struct sbuf *sql, match_t match, unsigned int field)
{
	switch (match) {
		case MATCH_ALL:
		case MATCH_EXACT:
			sbuf_cat(sql, "name LIKE ?1 ");
			if (field & REPO_SEARCH_COMMENT)
				sbuf_cat(sql, "OR comment LIKE ?1 ");
			else if (field & REPO_SEARCH_DESCRIPTION)
				sbuf_cat(sql, "OR desc LIKE ?1 ");
			break;
		case MATCH_GLOB:
			sbuf_cat(sql, "name GLOB ?1 ");
			if (field & REPO_SEARCH_COMMENT)
				sbuf_cat(sql, "OR comment GLOB ?1 ");
			else if (field & REPO_SEARCH_DESCRIPTION)
				sbuf_cat(sql, "OR desc GLOB ?1 ");
			break;
		case MATCH_REGEX:
			sbuf_cat(sql, "name REGEXP ?1 ");
			if (field & REPO_SEARCH_COMMENT)
				sbuf_cat(sql, "OR comment REGEXP ?1 ");
			else if (field & REPO_SEARCH_DESCRIPTION)
				sbuf_cat(sql, "OR desc REGEXP ?1 ");
			break;
		case MATCH_EREGEX:
			sbuf_cat(sql, "EREGEXP(?1, name) ");
			if (field & REPO_SEARCH_COMMENT)
				sbuf_cat(sql, "OR EREGEXP(?1, comment) ");
			else if (field & REPO_SEARCH_DESCRIPTION)
				sbuf_cat(sql, "OR EREGEXP(?1, desc) ");
			break;
	}

	return (EPKG_OK);
}

struct pkgdb_it *
pkgdb_rquery(struct pkgdb *db, const char *pattern, match_t match, unsigned int field)
{
	const char *dbname = NULL;
	char tmpbuf[BUFSIZ];
	sqlite3_stmt *stmt = NULL;
	struct sbuf *sql = NULL;
	struct pkgdb_it *it = NULL;
	const char *basesql      = "SELECT rowid, origin, name, version, comment, "
					"desc, arch, arch, osversion, maintainer, www, "
					"flatsize, pkgsize, cksum, path ";
	const char *multireposql = "SELECT rowid, origin, name, version, comment, "
					"desc, arch, arch, osversion, maintainer, www, "
					"flatsize, pkgsize, cksum, path, '%s' AS dbname FROM '%s'.packages WHERE ";

	assert(pattern != NULL && pattern[0] != '\0');

	if (db->type != PKGDB_REMOTE) {
		EMIT_PKG_ERROR("%s", "remote database not attached (misuse)");
		return (NULL);
	}

	sql = sbuf_new_auto();
	sbuf_cat(sql, basesql);

	if ((strcmp(pkg_config("PKG_MULTIREPOS"), "true") == 0) && \
			(pkg_config("PACKAGESITE") == NULL)) {
		/*
		 * Working on multiple remote repositories
		 */

		/* add the dbname column to the SELECT */
		sbuf_cat(sql, ", dbname FROM ");

		if ((it = pkgdb_repos_new(db)) == NULL) {
			EMIT_PKG_ERROR("%s", "cannot get the attached databases");
			return (NULL);
		}

		/* get the first repository entry */
		if ((dbname = pkgdb_repos_next(it)) != NULL) {
			sbuf_cat(sql, "(");
			snprintf(tmpbuf, sizeof(tmpbuf), multireposql, dbname, dbname);

			sbuf_cat(sql, tmpbuf);
			pkgdb_rquery_build_search_query(sql, match, field);
		} else {
			/* there are no remote databases attached */
			sbuf_finish(sql);
			sbuf_delete(sql);
			pkgdb_it_free(it);
			return (NULL);
		}

		while ((dbname = pkgdb_repos_next(it)) != NULL) {
			sbuf_cat(sql, "UNION ");
			snprintf(tmpbuf, sizeof(tmpbuf), multireposql, dbname, dbname);

			sbuf_cat(sql, tmpbuf);
			pkgdb_rquery_build_search_query(sql, match, field);
		}

		sbuf_cat(sql, ");");
		sbuf_finish(sql);
		pkgdb_it_free(it);
	} else {
		/* 
		 * Working on a single remote repository
		 */

		sbuf_cat(sql, "FROM remote.packages WHERE ");
		pkgdb_rquery_build_search_query(sql, match, field);
		sbuf_cat(sql, ";");
		sbuf_finish(sql);
	}

	if (sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	sbuf_delete(sql);

	sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_REMOTE));
}
