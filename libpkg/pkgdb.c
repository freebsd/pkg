#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sqlite3.h>

#include "pkg.h"
#include "pkg_error.h"
#include "pkg_private.h"
#include "pkgdb.h"
#include "pkg_util.h"

static struct pkgdb_it * pkgdb_it_new(struct pkgdb *, sqlite3_stmt *, int);
static void pkgdb_regex(sqlite3_context *, int, sqlite3_value **, int);
static void pkgdb_regex_basic(sqlite3_context *, int, sqlite3_value **);
static void pkgdb_regex_extended(sqlite3_context *, int, sqlite3_value **);
static void pkgdb_regex_delete(void *);
static void pkgdb_pkglt(sqlite3_context *, int, sqlite3_value **);
static void pkgdb_pkggt(sqlite3_context *, int, sqlite3_value **);
static int get_pragma(sqlite3 *, const char *, int64_t *);

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
 * 
 */

static int
pkgdb_init(sqlite3 *sdb)
{
	char *errmsg;
	const char sql[] = ""
	"CREATE TABLE packages ("
		"id INTEGER PRIMARY KEY,"
		"origin TEXT UNIQUE,"
		"name TEXT,"
		"version TEXT,"
		"comment TEXT,"
		"desc TEXT,"
		"mtree_id INTEGER REFERENCES mtree(id) ON DELETE RESTRICT"
			" ON UPDATE CASCADE,"
		"message TEXT,"
		"arch TEXT,"
		"osversion TEXT,"
		"maintainer TEXT,"
		"www TEXT,"
		"prefix TEXT,"
		"flatsize INTEGER,"
		"automatic INTEGER,"
		"pkg_format_version INTEGER"
	");"
	"CREATE TABLE mtree ("
		"id INTEGER PRIMARY KEY,"
		"content TEXT UNIQUE"
	");"
	"CREATE TRIGGER clean_mtree AFTER DELETE ON packages BEGIN "
		"DELETE FROM mtree WHERE id NOT IN (SELECT DISTINCT mtree_id FROM packages);"
	"END;"
	"CREATE VIEW pkg_mtree AS "
	"SELECT origin, name, version, comment, desc, mtree.content AS mtree, message, arch, osversion, "
	"maintainer, www, prefix, flatsize, automatic, pkg_format_version FROM packages "
	"INNER JOIN mtree ON packages.mtree_id = mtree.id;"
	"CREATE TRIGGER pkg_insert INSTEAD OF INSERT ON pkg_mtree "
	"FOR EACH ROW BEGIN "
		"INSERT OR IGNORE INTO mtree (content) VALUES (NEW.mtree);"
		"INSERT OR REPLACE INTO packages(origin, name, version, comment, desc, mtree_id, "
		"message, arch, osversion, maintainer, www, prefix, flatsize, automatic) "
		"VALUES (NEW.origin, NEW.name, NEW.version, NEW.comment, NEW.desc, "
		"(SELECT id FROM mtree WHERE content = NEW.mtree), "
		"NEW.message, NEW.arch, NEW.osversion, NEW.maintainer, NEW.www, NEW.prefix, "
		"NEW.flatsize NEW.automatic);"
	"END;"
	"CREATE TABLE scripts ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"script TEXT,"
		"type INTEGER,"
		"PRIMARY KEY (package_id, type)"
	");"
	"CREATE INDEX scripts_package ON scripts(package_id);"
	"CREATE TABLE options ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"option TEXT,"
		"value TEXT,"
		"PRIMARY KEY (package_id,option)"
	");"
	"CREATE INDEX options_package ON options(package_id);"
	"CREATE TABLE deps ("
		"origin TEXT,"
		"name TEXT,"
		"version TEXT,"
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"PRIMARY KEY (package_id,origin)"
	");"
	"CREATE INDEX deps_origin ON deps(origin);"
	"CREATE INDEX deps_package ON deps(package_id);"
	"CREATE TABLE files ("
		"path TEXT PRIMARY KEY,"
		"sha256 TEXT,"
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE"
	");"
	"CREATE INDEX files_package ON files(package_id);"
	"CREATE TABLE conflicts ("
		"name TEXT,"
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"PRIMARY KEY (package_id,name)"
	");"
	"CREATE INDEX conflicts_package ON conflicts(package_id);"
	"CREATE TABLE directories ("
		"id INTEGER PRIMARY KEY, "
		"path TEXT "
	");"
	"CREATE TABLE pkg_dirs_assoc ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE, "
		"directory_id INTEGER REFERENCES directories(id) ON DELETE RESTRICT"
			" ON UPDATE RESTRICT, "
		"PRIMARY KEY (package_id, directory_id)"
	");"
	"CREATE VIEW pkg_dirs AS SELECT origin, path FROM packages "
	"INNER JOIN pkg_dirs_assoc ON packages.id = pkg_dirs_assoc.package_id "
	"INNER JOIN directories ON pkg_dirs_assoc.directory_id = directories.id;"
	"CREATE TRIGGER pkg_dirs_clean AFTER DELETE ON packages BEGIN "
		"DELETE from directories WHERE id NOT IN (SELECT DISTINCT directory_id FROM pkg_dirs_assoc);"
	"END;"
	"CREATE TRIGGER dir_insert INSTEAD OF INSERT ON pkg_dirs "
	"FOR EACH ROW BEGIN "
		"INSERT OR IGNORE INTO directories (path) VALUES (NEW.path);"
		"INSERT INTO pkg_dirs_assoc (package_id, directory_id) VALUES "
			"((SELECT id FROM packages WHERE origin = NEW.origin), "
			"(SELECT id FROM directories WHERE path = NEW.path));"
	"END;"
	;

	if (sqlite3_exec(sdb, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
		pkg_error_set(EPKG_FATAL, "sqlite: %s", errmsg);
		sqlite3_free(errmsg);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

int
pkgdb_open(struct pkgdb **db, pkgdb_t type)
{
	int retcode;
	char *errmsg;
	char localpath[MAXPATHLEN];
	char remotepath[MAXPATHLEN];
	char sql[BUFSIZ];
	const char *dbdir;

	dbdir = pkg_config("PKG_DBDIR");

	if ((*db = calloc(1, sizeof(struct pkgdb))) == NULL)
		return (pkg_error_set(EPKG_FATAL, "calloc(): %s", strerror(errno)));

	(*db)->type = type;

	snprintf(localpath, sizeof(localpath), "%s/local.sqlite", dbdir);
	retcode = access(localpath, R_OK);
	if (retcode == -1) {
		if (errno != ENOENT)
			return (pkg_error_set(EPKG_FATAL, "%s: %s", localpath,
								  strerror(errno)));
		else if (eaccess(dbdir, W_OK) != 0)
			return (pkg_error_set(EPKG_FATAL, "can not initialize database in "
								 "%s: %s", dbdir, strerror(errno)));
	}

	if (sqlite3_open(localpath, &(*db)->sqlite) != SQLITE_OK)
		return (ERROR_SQLITE((*db)->sqlite));

	if (type == PKGDB_REMOTE) {
		snprintf(remotepath, sizeof(remotepath), "%s/repo.sqlite", dbdir);

		if (access(remotepath, R_OK) != 0)
			return (pkg_error_set(EPKG_FATAL, "repo.sqlite: %s",
								  strerror(errno)));

		sqlite3_snprintf(sizeof(sql), sql, "ATTACH \"%s\" as remote;", remotepath);

		if (sqlite3_exec((*db)->sqlite, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
			pkg_error_set(EPKG_FATAL, "%s", errmsg);
			sqlite3_free(errmsg);
			return (EPKG_FATAL);
		}
	}

	/* If the database is missing we have to initialize it */
	if (retcode == -1)
		if ((retcode = pkgdb_init((*db)->sqlite)) != EPKG_OK)
			return (ERROR_SQLITE((*db)->sqlite));

	sqlite3_create_function((*db)->sqlite, "regexp", 2, SQLITE_ANY, NULL,
							pkgdb_regex_basic, NULL, NULL);
	sqlite3_create_function((*db)->sqlite, "eregexp", 2, SQLITE_ANY, NULL,
							pkgdb_regex_extended, NULL, NULL);
	sqlite3_create_function((*db)->sqlite, "pkglt", 2, SQLITE_ANY, NULL,
			pkgdb_pkglt, NULL, NULL);
	sqlite3_create_function((*db)->sqlite, "pkggt", 2, SQLITE_ANY, NULL,
			pkgdb_pkggt, NULL, NULL);

	/*
	 * allow forign key option which will allow to have clean support for
	 * reinstalling
	 */
	if (sqlite3_exec((*db)->sqlite, "PRAGMA foreign_keys = ON;", NULL, NULL,
		&errmsg) != SQLITE_OK) {
		pkg_error_set(EPKG_FATAL, "sqlite: %s", errmsg);
		sqlite3_free(errmsg);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

void
pkgdb_close(struct pkgdb *db)
{
	if (db == NULL)
		return;

	if (db->sqlite != NULL) {
		if (db->type == PKGDB_REMOTE)
			sqlite3_exec(db->sqlite, "DETACH remote;", NULL, NULL, NULL);

		sqlite3_close(db->sqlite);
	}

	free(db);
}

static struct pkgdb_it *
pkgdb_it_new(struct pkgdb *db, sqlite3_stmt *s, int type)
{
	struct pkgdb_it *it;

	if ((it = malloc(sizeof(struct pkgdb_it))) == NULL) {
		pkg_error_set(EPKG_FATAL, "malloc(): %s", strerror(errno));
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

	if (it == NULL)
		return (ERROR_BAD_ARG("it"));

	switch (sqlite3_step(it->stmt)) {
	case SQLITE_ROW:
		if (*pkg_p == NULL)
			pkg_new(pkg_p, PKG_INSTALLED);
		else
			pkg_reset(*pkg_p, PKG_INSTALLED);
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

		if (it->type == IT_UPGRADE) {
			pkg->type = PKG_UPGRADE;

			pkg_set(pkg, PKG_NEWVERSION, sqlite3_column_text(it->stmt, 13));
			pkg_setnewflatsize(pkg, sqlite3_column_int64(it->stmt, 14));
			pkg_setnewpkgsize(pkg, sqlite3_column_int64(it->stmt, 15));
			pkg_set(pkg, PKG_REPOPATH, sqlite3_column_text(it->stmt, 16));
		}

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

		return (EPKG_OK);
	case SQLITE_DONE:
		return (EPKG_END);
	default:
		return (ERROR_SQLITE(it->db->sqlite));
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

	if (match != MATCH_ALL && pattern == NULL) {
		ERROR_BAD_ARG("pattern");
		return (NULL);
	}

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
				"p.prefix, p.flatsize "
			"FROM packages AS p%s "
			"ORDER BY p.name;", comp);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite);
		return (NULL);
	}

	if (match != MATCH_ALL)
		sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, IT_LOCAL));
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

	return (pkgdb_it_new(db, stmt, IT_LOCAL));
}

int
pkgdb_loaddeps(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	struct pkgdb_it it;
	struct pkg *p;
	int ret;
	const char sql[] = ""
	"SELECT p.rowid, p.origin, p.name, p.version, p.comment, p.desc, "
		"p.message, p.arch, p.osversion, p.maintainer, p.www, "
		"p.prefix, p.flatsize "
	"FROM packages AS p, deps AS d "
	"WHERE p.origin = d.origin "
		"AND d.package_id = ?1;";

	if (pkg->type != PKG_INSTALLED)
		return (ERROR_BAD_ARG("pkg"));

	if (pkg->flags & PKG_LOAD_DEPS)
		return (EPKG_OK);

	array_init(&pkg->deps, 10);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK)
		return (ERROR_SQLITE(db->sqlite));

	sqlite3_bind_int64(stmt, 1, pkg->rowid);

	it.stmt = stmt;
	it.db = db;

	p = NULL;
	while ((ret = pkgdb_it_next(&it, &p, PKG_LOAD_BASIC)) == EPKG_OK) {
		array_append(&pkg->deps, p);
		p = NULL;
	}

	sqlite3_finalize(stmt);

	if (ret != EPKG_END) {
		array_reset(&pkg->deps, &pkg_free_void);
		return (ret);
	}

	pkg->flags |= PKG_LOAD_DEPS;
	return (EPKG_OK);
}

int
pkgdb_loadrdeps(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	struct pkgdb_it it;
	struct pkg *p;
	int ret;
	const char sql[] = ""
		"SELECT p.rowid, p.origin, p.name, p.version, p.comment, p.desc, "
			"p.message, p.arch, p.osversion, p.maintainer, p.www, "
			"p.prefix, p.flatsize "
		"FROM packages AS p, deps AS d "
		"WHERE p.rowid = d.package_id "
			"AND d.origin = ?1;";

	if (pkg->type != PKG_INSTALLED)
		return (ERROR_BAD_ARG("pkg"));

	if (pkg->flags & PKG_LOAD_RDEPS)
		return (EPKG_OK);

	array_init(&pkg->rdeps, 5);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK)
		return (ERROR_SQLITE(db->sqlite));

	sqlite3_bind_text(stmt, 1, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);

	it.stmt = stmt;
	it.db = db;

	p = NULL;
	while ((ret = pkgdb_it_next(&it, &p, PKG_LOAD_BASIC)) == EPKG_OK) {
		array_append(&pkg->rdeps, p);
		p = NULL;
	}
	sqlite3_finalize(stmt);

	if (ret != EPKG_END) {
		array_reset(&pkg->rdeps, &pkg_free_void);
		return (ret);
	}

	pkg->flags |= PKG_LOAD_RDEPS;
	return (EPKG_OK);
}

int
pkgdb_loadconflicts(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	struct pkg_conflict *c;
	int ret;
	const char sql[] = ""
		"SELECT name "
		"FROM conflicts "
		"WHERE package_id = ?1;";

	if (pkg->type != PKG_INSTALLED)
		return (ERROR_BAD_ARG("pkg"));

	if (pkg->flags & PKG_LOAD_CONFLICTS)
		return (EPKG_OK);

	array_init(&pkg->conflicts, 5);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK)
		return (ERROR_SQLITE(db->sqlite));

	sqlite3_bind_int64(stmt, 1, pkg->rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_conflict_new(&c);
		sbuf_set(&c->glob, sqlite3_column_text(stmt, 0));
		array_append(&pkg->conflicts, c);
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		array_reset(&pkg->conflicts, &pkg_conflict_free_void);
		return (ERROR_SQLITE(db->sqlite));
	}

	pkg->flags |= PKG_LOAD_CONFLICTS;

	return (EPKG_OK);
}

int
pkgdb_loadfiles(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;

	struct pkg_file *f;
	int ret;
	const char sql[] = ""
		"SELECT path, sha256 "
		"FROM files "
		"WHERE package_id = ?1 "
		"ORDER BY PATH ASC";

	if (pkg->type != PKG_INSTALLED)
		return (ERROR_BAD_ARG("pkg"));

	if (pkg->flags & PKG_LOAD_FILES)
		return (EPKG_OK);

	array_init(&pkg->files, 10);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK)
		return (ERROR_SQLITE(db->sqlite));

	sqlite3_bind_int64(stmt, 1, pkg->rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_file_new(&f);
		strlcpy(f->path, sqlite3_column_text(stmt, 0), sizeof(f->path));
		strlcpy(f->sha256, sqlite3_column_text(stmt, 1), sizeof(f->sha256));
		array_append(&pkg->files, f);
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		array_reset(&pkg->files, &free);
		return (ERROR_SQLITE(db->sqlite));
	}

	pkg->flags |= PKG_LOAD_FILES;
	return (EPKG_OK);
}

int
pkgdb_loaddirs(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	char *path;
	int ret;

	const char sql[] = ""
		"SELECT path "
		"FROM pkg_dirs "
		"WHERE origin = ?1 "
		"ORDER by path DESC";

	if (pkg->flags & PKG_LOAD_DIRS)
		return (EPKG_OK);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK)
		return (ERROR_SQLITE(db->sqlite));

	array_init(&pkg->dirs, 5);

	sqlite3_bind_text(stmt, 1, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		path = strdup(sqlite3_column_text(stmt, 0));
		array_append(&pkg->dirs, path);
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		array_reset(&pkg->dirs, &free);
		return (ERROR_SQLITE(db->sqlite));
	}

	pkg->flags |= PKG_LOAD_DIRS;
	return (EPKG_OK);
}

int
pkgdb_loadscripts(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	struct pkg_script *s;
	int ret;
	const char sql[] = ""
		"SELECT script, type "
		"FROM scripts "
		"WHERE package_id = ?1";

	if (pkg->type != PKG_INSTALLED)
		return (ERROR_BAD_ARG("pkg"));

	if (pkg->flags & PKG_LOAD_SCRIPTS)
		return (EPKG_OK);

	array_init(&pkg->scripts, 6);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK)
		return (ERROR_SQLITE(db->sqlite));

	sqlite3_bind_int64(stmt, 1, pkg->rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_script_new(&s);
		sbuf_set(&s->data, sqlite3_column_text(stmt, 0));
		s->type = sqlite3_column_int(stmt, 1);
		array_append(&pkg->scripts, s);
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		array_reset(&pkg->scripts, &pkg_script_free_void);
		return (ERROR_SQLITE(db->sqlite));
	}

	pkg->flags |= PKG_LOAD_SCRIPTS;
	return (EPKG_OK);
}

int
pkgdb_loadoptions(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	struct pkg_option *o;
	int ret;
	const char sql[] = ""
		"SELECT option, value "
		"FROM options "
		"WHERE package_id = ?1";

	if (pkg->type != PKG_INSTALLED)
		return (ERROR_BAD_ARG("pkg"));

	if (pkg->flags & PKG_LOAD_OPTIONS)
		return (EPKG_OK);

	array_init(&pkg->options, 5);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK)
		return (ERROR_SQLITE(db->sqlite));

	sqlite3_bind_int64(stmt, 1, pkg->rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_option_new(&o);
		sbuf_set(&o->opt, sqlite3_column_text(stmt, 0));
		sbuf_set(&o->value, sqlite3_column_text(stmt, 1));
		array_append(&pkg->options, o);
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		array_reset(&pkg->options, &pkg_option_free_void);
		return (ERROR_SQLITE(db->sqlite));
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

	if (pkg->type != PKG_INSTALLED)
		return (ERROR_BAD_ARG("pkg"));

	if (pkg->flags & PKG_LOAD_MTREE)
		return (EPKG_OK);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK)
		return (ERROR_SQLITE(db->sqlite));

	sqlite3_bind_int64(stmt, 1, pkg->rowid);

	ret = sqlite3_step(stmt);
	if (ret == SQLITE_ROW) {
		sbuf_set(&pkg->fields[PKG_MTREE].value,
				 sqlite3_column_text(stmt, 0));
		ret = SQLITE_DONE;
	}

	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE)
		return (ERROR_SQLITE(db->sqlite));

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
	struct pkg **deps;
	struct pkg_file **files;
	const char **dirs;
	struct pkg_conflict **conflicts;
	struct pkg_script **scripts;
	struct pkg_option **options;

	sqlite3 *s;
	sqlite3_stmt *stmt_pkg = NULL;
	sqlite3_stmt *stmt_sel_pkg = NULL;
	sqlite3_stmt *stmt_dep = NULL;
	sqlite3_stmt *stmt_conflict = NULL;
	sqlite3_stmt *stmt_file = NULL;
	sqlite3_stmt *stmt_script = NULL;
	sqlite3_stmt *stmt_option = NULL;
	sqlite3_stmt *stmt_dirs = NULL;

	int i;
	int ret;
	int retcode = EPKG_OK;
	const char *path;
	int64_t package_id;
	char *errmsg;

	const char sql_begin[] = "BEGIN TRANSACTION;";
	const char sql_pkg[] = ""
		"INSERT INTO pkg_mtree( "
			"origin, name, version, comment, desc, mtree, message, arch, "
			"osversion, maintainer, www, prefix, flatsize, automatic) "
		"VALUES( ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14);";
	const char sql_sel_pkg[] = ""
		"SELECT id FROM packages "
		"WHERE origin = ?1;";
	const char sql_dep[] = ""
		"INSERT INTO deps (origin, name, version, package_id) "
		"VALUES (?1, ?2, ?3, ?4);";
	const char sql_conflict[] = ""
		"INSERT INTO conflicts (name, package_id) "
		"VALUES (?1, ?2);";
	const char sql_file[] = ""
		"INSERT INTO files (path, sha256, package_id) "
		"VALUES (?1, ?2, ?3);";
	const char sql_script[] = ""
		"INSERT INTO scripts (script, type, package_id) "
		"VALUES (?1, ?2, ?3);";
	const char sql_option[] = ""
		"INSERT INTO options (option, value, package_id) "
		"VALUES (?1, ?2, ?3);";
	const char sql_dir[] = ""
		"INSERT INTO pkg_dirs(origin, path) "
		"VALUES (?1, ?2);";

	if (pkgdb_has_flag(db, PKGDB_FLAG_IN_FLIGHT)) {
		pkg_error_set(EPKG_FATAL, "tried to register a package with an in-flight SQL command");
		return (EPKG_FATAL);
	}

	s = db->sqlite;

	if (sqlite3_exec(s, sql_begin, NULL, NULL, &errmsg) != SQLITE_OK) {
		pkg_error_set(EPKG_FATAL, "sqlite: %s", errmsg);
		sqlite3_free(errmsg);
		return (EPKG_FATAL);
	}
	PKGDB_SET_FLAG(db, PKGDB_FLAG_IN_FLIGHT);

	/*
	 * Insert package record
	 */
	if (sqlite3_prepare_v2(s, sql_pkg, -1, &stmt_pkg, NULL) != SQLITE_OK) {
		retcode = ERROR_SQLITE(s);
		goto cleanup;
	}
	sqlite3_bind_text(stmt_pkg, 1, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 2, pkg_get(pkg, PKG_NAME), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 3, pkg_get(pkg, PKG_VERSION), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 4, pkg_get(pkg, PKG_COMMENT), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 5, pkg_get(pkg, PKG_DESC), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 6, pkg_get(pkg, PKG_MTREE), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 7, pkg_get(pkg, PKG_MESSAGE), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 8, pkg_get(pkg, PKG_ARCH), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 9, pkg_get(pkg, PKG_OSVERSION), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 10, pkg_get(pkg, PKG_MAINTAINER), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 11, pkg_get(pkg, PKG_WWW), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 12, pkg_get(pkg, PKG_PREFIX), -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt_pkg, 13, pkg_flatsize(pkg));
	sqlite3_bind_int(stmt_pkg, 14, pkg_isautomatic(pkg));

	if ((ret = sqlite3_step(stmt_pkg)) != SQLITE_DONE) {
		if ( ret == SQLITE_CONSTRAINT)
			retcode = pkg_error_set(EPKG_FATAL, "constraint violation on "
					"pkg with %s", pkg_get(pkg, PKG_ORIGIN));
		else
			retcode = ERROR_SQLITE(s);
		goto cleanup;
	}

	/*
	 * Get the generated package_id
	 */

	if (sqlite3_prepare_v2(s, sql_sel_pkg, -1, &stmt_sel_pkg, NULL) != SQLITE_OK) {
		retcode = ERROR_SQLITE(s);
		goto cleanup;
	}
	sqlite3_bind_text(stmt_sel_pkg, 1, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);
	ret = sqlite3_step(stmt_sel_pkg);
	if (ret == SQLITE_ROW) {
		package_id = sqlite3_column_int64(stmt_sel_pkg, 0);
		ret = SQLITE_DONE;
	} else {
		retcode = ERROR_SQLITE(s);
		goto cleanup;
	}

	/*
	 * Insert dependencies list
	 */

	if (sqlite3_prepare_v2(s, sql_dep, -1, &stmt_dep, NULL) != SQLITE_OK) {

		retcode = ERROR_SQLITE(s);
		goto cleanup;
	}

	deps = pkg_deps(pkg);
	for (i = 0; deps[i] != NULL; i++) {
		sqlite3_bind_text(stmt_dep, 1, pkg_get(deps[i], PKG_ORIGIN), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_dep, 2, pkg_get(deps[i], PKG_NAME), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_dep, 3, pkg_get(deps[i], PKG_VERSION), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt_dep, 4, package_id);

		if ((ret = sqlite3_step(stmt_dep)) != SQLITE_DONE) {
			if ( ret == SQLITE_CONSTRAINT)
				retcode = pkg_error_set(EPKG_FATAL, "constraint violation on "
						"deps with %s", pkg_get(deps[i], PKG_ORIGIN));
			else
				retcode = ERROR_SQLITE(s);
			goto cleanup;
		}

		sqlite3_reset(stmt_dep);
	}

	/*
	 * Insert conflicts list
	 */

	if (sqlite3_prepare_v2(s, sql_conflict, -1, &stmt_conflict, NULL) != SQLITE_OK) {
		retcode = ERROR_SQLITE(s);
		goto cleanup;
	}

	conflicts = pkg_conflicts(pkg);
	for (i = 0; conflicts[i] != NULL; i++) {
		sqlite3_bind_text(stmt_conflict, 1, pkg_conflict_glob(conflicts[i]), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt_conflict, 2, package_id);

		if ((ret = sqlite3_step(stmt_conflict)) != SQLITE_DONE) {
			if ( ret == SQLITE_CONSTRAINT)
				retcode = pkg_error_set(EPKG_FATAL, "constraint violation on "
						"conflicts with %s", pkg_conflict_glob(conflicts[i]));
			else
				retcode = ERROR_SQLITE(s);
			goto cleanup;
		}

		sqlite3_reset(stmt_conflict);
	}

	/*
	 * Insert files.
	 */

	if (sqlite3_prepare_v2(s, sql_file, -1, &stmt_file, NULL) != SQLITE_OK) {
		retcode = ERROR_SQLITE(s);
		goto cleanup;
	}

	files = pkg_files(pkg);
	for (i = 0; files[i] != NULL; i++) {
		path = pkg_file_path(files[i]);
		sqlite3_bind_text(stmt_file, 1, pkg_file_path(files[i]), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_file, 2, pkg_file_sha256(files[i]), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt_file, 3, package_id);

		if ((ret = sqlite3_step(stmt_file)) != SQLITE_DONE) {
			if (ret == SQLITE_CONSTRAINT)
				retcode = pkg_error_set(EPKG_FATAL, "constraint violation on "
						"path with %s", pkg_file_path(files[i]));
			else
				retcode = ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt_file);
	}

	/*
	 * Insert dirs.
	 */

	if (sqlite3_prepare_v2(s, sql_dir, -1, &stmt_dirs, NULL) != SQLITE_OK) {
		retcode = ERROR_SQLITE(s);
		goto cleanup;
	}

	dirs = pkg_dirs(pkg);
	for (i = 0; dirs[i] != NULL; i++) {
		sqlite3_bind_text(stmt_dirs, 1, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_dirs, 2, dirs[i], -1, SQLITE_STATIC);
			
		if ((ret = sqlite3_step(stmt_dirs)) != SQLITE_DONE) {
			if ( ret == SQLITE_CONSTRAINT)
				retcode = pkg_error_set(EPKG_FATAL, "constraint violation on "
						"dirs with %s", dirs[i]);
			else
				retcode = ERROR_SQLITE(s);
			goto cleanup;
		}
		sqlite3_reset(stmt_dirs);
	}

	/*
	 * Insert scripts
	 */

	if (sqlite3_prepare_v2(s, sql_script, -1, &stmt_script, NULL) != SQLITE_OK) {
		retcode = ERROR_SQLITE(s);
		goto cleanup;
	}

	scripts = pkg_scripts(pkg);
	for (i = 0; scripts[i] != NULL; i++) {
		sqlite3_bind_text(stmt_script, 1, pkg_script_data(scripts[i]), -1, SQLITE_STATIC);
		sqlite3_bind_int(stmt_script, 2, pkg_script_type(scripts[i]));
		sqlite3_bind_int64(stmt_script, 3, package_id);

		if (sqlite3_step(stmt_script) != SQLITE_DONE) {
			if ( ret == SQLITE_CONSTRAINT)
				retcode = pkg_error_set(EPKG_FATAL, "constraint violation on "
						"scripts with %s", pkg_script_data(scripts[i]));
			else
				retcode = ERROR_SQLITE(s);
			goto cleanup;
		}

		sqlite3_reset(stmt_script);
	}

	/*
	 * Insert options
	 */

	options = pkg_options(pkg);
	if (sqlite3_prepare_v2(s, sql_option, -1, &stmt_option, NULL) != SQLITE_OK) {
		retcode = ERROR_SQLITE(s);
		goto cleanup;
	}

	for (i = 0; options[i] != NULL; i++) {
		sqlite3_bind_text(stmt_option, 1, pkg_option_opt(options[i]), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_option, 2, pkg_option_value(options[i]), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt_option, 3, package_id);

		if (sqlite3_step(stmt_option) != SQLITE_DONE) {
			retcode = ERROR_SQLITE(s);
			goto cleanup;
		}

		sqlite3_reset(stmt_option);
	}

	cleanup:

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

	return (retcode);
}

int
pkgdb_register_finale(struct pkgdb *db, int retcode)
{
	int ret = EPKG_OK;
	const char *commands[] = { "COMMIT;", "ROLLBACK;", NULL };
	const char *command;
	char *errmsg;

	if (!pkgdb_has_flag(db, PKGDB_FLAG_IN_FLIGHT)) {
		ret = pkg_error_set(EPKG_FATAL, "database command not in flight");
		return ret;
	}

	command = (retcode == EPKG_OK) ? commands[0] : commands[1];
	if (sqlite3_exec(db->sqlite, command, NULL, NULL, &errmsg) != SQLITE_OK) {
		ret = pkg_error_set(EPKG_FATAL, "sqlite: %s", errmsg);
		sqlite3_free(errmsg);
	}
	PKGDB_UNSET_FLAG(db, PKGDB_FLAG_IN_FLIGHT);

	return ret;
}

int
pkgdb_unregister_pkg(struct pkgdb *db, const char *origin)
{
	sqlite3_stmt *stmt_del;
	int ret;
	const char sql[] = "DELETE FROM packages WHERE origin = ?1;";

	if (db == NULL)
		return (ERROR_BAD_ARG("db"));

	if (origin == NULL)
		return (ERROR_BAD_ARG("origin"));

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt_del, NULL) != SQLITE_OK)
		return (ERROR_SQLITE(db->sqlite));

	sqlite3_bind_text(stmt_del, 1, origin, -1, SQLITE_STATIC);

	ret = sqlite3_step(stmt_del);
	sqlite3_finalize(stmt_del);

	if (ret != SQLITE_DONE)
		return (ERROR_SQLITE(db->sqlite));

	return (EPKG_OK);
}

static int get_pragma(sqlite3 *s, const char *sql, int64_t *res) {
	sqlite3_stmt *stmt;
	int ret;

	if (sqlite3_prepare_v2(s, sql, -1, &stmt, NULL) != SQLITE_OK)
		return (ERROR_SQLITE(s));

	ret = sqlite3_step(stmt);

	if (ret == SQLITE_ROW)
		*res = sqlite3_column_int64(stmt, 0);

	sqlite3_finalize(stmt);

	if (ret != SQLITE_ROW)
		return (ERROR_SQLITE(s));

	return (EPKG_OK);
}

int
pkgdb_compact(struct pkgdb *db)
{
	int64_t page_count = 0;
	int64_t freelist_count = 0;
	char *errmsg;
	int retcode = EPKG_OK;

	if (db == NULL)
		return (ERROR_BAD_ARG("db"));

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

	if (sqlite3_exec(db->sqlite, "VACUUM;", NULL, NULL, &errmsg) != SQLITE_OK){
		retcode = pkg_error_set(EPKG_FATAL, "%s", errmsg);
		sqlite3_free(errmsg);
	}

	return (retcode);
}

struct pkgdb_it *
pkgdb_query_upgrades(struct pkgdb *db)
{
	sqlite3_stmt *stmt;

	if (db->type != PKGDB_REMOTE) {
		pkg_error_set(EPKG_FATAL, "remote database not attached");
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

	return (pkgdb_it_new(db, stmt, IT_UPGRADE));
}

struct pkgdb_it *
pkgdb_query_downgrades(struct pkgdb *db)
{

	sqlite3_stmt *stmt;

	if (db->type != PKGDB_REMOTE) {
		pkg_error_set(EPKG_FATAL, "remote database not attached");
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

	return (pkgdb_it_new(db, stmt, IT_UPGRADE));
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

	return (pkgdb_it_new(db, stmt, IT_LOCAL));
}
