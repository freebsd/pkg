#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <regex.h>
#include <sha256.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sqlite3.h>

#include "pkg.h"
#include "pkg_error.h"
#include "pkg_private.h"
#include "pkgdb.h"
#include "pkg_util.h"

#define PKG_DBDIR "/var/db/pkg"

static struct pkgdb_it * pkgdb_it_new(struct pkgdb *, sqlite3_stmt *);
static void pkgdb_regex(sqlite3_context *, int, sqlite3_value **, int);
static void pkgdb_regex_basic(sqlite3_context *, int, sqlite3_value **);
static void pkgdb_regex_extended(sqlite3_context *, int, sqlite3_value **);
static void pkgdb_regex_delete(void *);
static int get_pragma(sqlite3 *, const char *, int64_t *);

static void
pkgdb_regex_delete(void *p)
{
	regex_t *re = (regex_t *)p;

	regfree(re);
	free(re);
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

const char *
pkgdb_get_dir(void)
{
	const char *pkg_dbdir;

	if ((pkg_dbdir = getenv("PKG_DBDIR")) == NULL)
		pkg_dbdir = PKG_DBDIR;

	return pkg_dbdir;
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
 * exec.type can be:
 * - 0: exec
 * - 1: unexec
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
	"CREATE TABLE scripts ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"script TEXT,"
		"type INTEGER,"
		"PRIMARY KEY (package_id, type)"
	");"
	"CREATE INDEX scripts_package ON scripts(package_id);"
	"CREATE TABLE exec ("
		"package_id INTEGER REFERENCES packages(id) ON DELETE CASCADE"
			" ON UPDATE CASCADE,"
		"cmd TEXT,"
		"type INTEGER"
	");"
	"CREATE INDEX exec_package ON exec(package_id);"
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
	"CREATE TABLE mtree ("
		"id INTEGER PRIMARY KEY,"
		"sha256 TEXT UNIQUE,"
		"content TEXT"
	");"
	"CREATE TRIGGER clean_mtree AFTER DELETE ON packages BEGIN "
		"DELETE FROM mtree WHERE rowid NOT IN (SELECT DISTINCT mtree_id FROM packages);"
	"END;";

	if (sqlite3_exec(sdb, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
		pkg_error_set(EPKG_FATAL, "sqlite: %s", errmsg);
		sqlite3_free(errmsg);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

int
pkgdb_open(struct pkgdb **db)
{
	int retcode;
	struct stat st;
	char *errmsg;
	char fpath[MAXPATHLEN];

	snprintf(fpath, sizeof(fpath), "%s/local.sqlite", pkgdb_get_dir());

	if ((*db = calloc(1, sizeof(struct pkgdb))) == NULL)
		return (pkg_error_set(EPKG_FATAL, "calloc(): %s", strerror(errno)));

	if ((retcode = stat(fpath, &st)) == -1 && errno != ENOENT)
		return (pkg_error_set(EPKG_FATAL, "can not stat %s: %s", fpath,
							  strerror(errno)));

	if (sqlite3_open(fpath, &(*db)->sqlite) != SQLITE_OK)
		return (ERROR_SQLITE((*db)->sqlite));

	/* If the database is missing we have to initialize it */
	if (retcode == -1)
		if ((retcode = pkgdb_init((*db)->sqlite)) != EPKG_OK)
			return (retcode);

	sqlite3_create_function((*db)->sqlite, "regexp", 2, SQLITE_ANY, NULL,
							pkgdb_regex_basic, NULL, NULL);
	sqlite3_create_function((*db)->sqlite, "eregexp", 2, SQLITE_ANY, NULL,
							pkgdb_regex_extended, NULL, NULL);

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

	if (db->sqlite != NULL)
		sqlite3_close(db->sqlite);

	free(db);
}

static struct pkgdb_it *
pkgdb_it_new(struct pkgdb *db, sqlite3_stmt *s)
{
	struct pkgdb_it *it;

	if ((it = malloc(sizeof(struct pkgdb_it))) == NULL) {
		pkg_error_set(EPKG_FATAL, "malloc(): %s", strerror(errno));
		sqlite3_finalize(s);
		return (NULL);
	}

	it->db = db;
	it->stmt = s;
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
			pkg_new(pkg_p);
		else
			pkg_reset(*pkg_p);
		pkg = *pkg_p;

		pkg->type = PKG_INSTALLED;
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

		if (flags & PKG_LOAD_EXECS)
			if ((ret = pkgdb_loadexecs(it->db, pkg)) != EPKG_OK)
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
			comp = " WHERE p.name = ?1";
		else
			comp = " WHERE p.origin = ?1";
		break;
	case MATCH_GLOB:
		if (checkorigin == NULL)
			comp = " WHERE p.name GLOB ?1";
		else
			comp = " WHERE p.origin GLOB ?1";
		break;
	case MATCH_REGEX:
		if (checkorigin == NULL)
			comp = " WHERE p.name REGEXP ?1";
		else
			comp = " WHERE p.origin REGEXP ?1";
		break;
	case MATCH_EREGEX:
		if (checkorigin == NULL)
			comp = " WHERE EREGEXP(?1, p.name)";
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

	return (pkgdb_it_new(db, stmt));
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

	return (pkgdb_it_new(db, stmt));
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
		"WHERE package_id = ?1;";

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
pkgdb_loadexecs(struct pkgdb *db, struct pkg *pkg)
{
	sqlite3_stmt *stmt;
	struct pkg_exec *e;
	int ret;
	const char sql[] = ""
		"SELECT cmd, type "
		"FROM exec "
		"WHERE package_id = ?1";

	if (pkg->type != PKG_INSTALLED)
		return (ERROR_BAD_ARG("pkg"));

	if (pkg->flags & PKG_LOAD_EXECS)
		return (EPKG_OK);

	array_init(&pkg->exec, 5);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK)
		return (ERROR_SQLITE(db->sqlite));

	sqlite3_bind_int64(stmt, 1, pkg->rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_exec_new(&e);
		sbuf_set(&e->cmd, sqlite3_column_text(stmt, 0));
		e->type = sqlite3_column_int(stmt, 1);
		array_append(&pkg->exec, e);
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		array_reset(&pkg->exec, &pkg_exec_free_void);
		return (ERROR_SQLITE(db->sqlite));
	}

	pkg->flags |= PKG_LOAD_EXECS;
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
pkgdb_register_pkg(struct pkgdb *db, struct pkg *pkg)
{
	struct pkg **deps;
	struct pkg_file **files;
	struct pkg_conflict **conflicts;
	struct pkg_exec **execs;
	struct pkg_script **scripts;
	struct pkg_option **options;

	sqlite3 *s;
	sqlite3_stmt *stmt_sel_mtree = NULL;
	sqlite3_stmt *stmt_mtree = NULL;
	sqlite3_stmt *stmt_pkg = NULL;
	sqlite3_stmt *stmt_dep = NULL;
	sqlite3_stmt *stmt_conflict = NULL;
	sqlite3_stmt *stmt_file = NULL;
	sqlite3_stmt *stmt_exec = NULL;
	sqlite3_stmt *stmt_script = NULL;
	sqlite3_stmt *stmt_option = NULL;

	int i;
	int ret;
	int retcode = EPKG_OK;
	int64_t package_id;
	int64_t mtree_id;
	char mtree_sha256[65];
	char *errmsg;
	const char *mtree;

	const char sql_begin[] = "BEGIN TRANSACTION;";
	const char sql_sel_mtree[] = ""
		"SELECT id "
		"FROM mtree "
		"WHERE sha256 = ?1;";
	const char sql_mtree[] = ""
		"INSERT INTO mtree (sha256, content) "
		"VALUES (?1, ?2);";
	const char sql_pkg[] = ""
		"INSERT OR REPLACE INTO packages( "
			"origin, name, version, comment, desc, mtree_id, message, arch, "
			"osversion, maintainer, www, prefix, flatsize) "
		"VALUES( ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13);";
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
	const char sql_exec[] = ""
		"INSERT INTO exec (cmd, type, package_id) "
		"VALUES (?1, ?2, ?3);";
	const char sql_option[] = ""
		"INSERT INTO options (option, value, package_id) "
		"VALUES (?1, ?2, ?3);";

	s = db->sqlite;


	if (sqlite3_exec(s, sql_begin, NULL, NULL, &errmsg) != SQLITE_OK) {
		pkg_error_set(EPKG_FATAL, "sqlite: %s", errmsg);
		sqlite3_free(errmsg);
		return (EPKG_FATAL);
	}

	/*
	 * If this package has a mtree, insert it in the database.
	 * Compute the sha256 of the mtree.
	 * If there is no mtree, mtree_id is set to zero (NULL in the database).
	 */
	mtree = pkg_get(pkg, PKG_MTREE);
	if (mtree != NULL) {
		sha256_str(mtree, mtree_sha256);

		/* Try to find the mtree in the database */
		if (sqlite3_prepare_v2(s, sql_sel_mtree, -1, &stmt_sel_mtree, NULL)
			!= SQLITE_OK) {
			retcode = ERROR_SQLITE(s);
			goto cleanup;
		}

		sqlite3_bind_text(stmt_sel_mtree, 1, mtree_sha256, -1, SQLITE_STATIC);

		ret = sqlite3_step(stmt_sel_mtree);
		if (ret == SQLITE_ROW) {
			mtree_id = sqlite3_column_int64(stmt_sel_mtree, 0);
			ret = SQLITE_DONE;
		} else if (ret == SQLITE_DONE)
			/* no result found */
			mtree_id = 0;

		if (ret != SQLITE_DONE) {
			retcode = ERROR_SQLITE(s);
			goto cleanup;
		}

		/* if the mtree is not into the dabase, insert it */
		if (mtree_id == 0) {
			if (sqlite3_prepare_v2(s, sql_mtree, -1, &stmt_mtree, NULL) !=
								SQLITE_OK) {
				retcode = ERROR_SQLITE(s);
				goto cleanup;
			}

			sqlite3_bind_text(stmt_mtree, 1, mtree_sha256, -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_mtree, 2, mtree, -1, SQLITE_STATIC);

			ret = sqlite3_step(stmt_mtree);
			if (ret != SQLITE_DONE) {
				retcode = ERROR_SQLITE(s);
				goto cleanup;
			}

			mtree_id = sqlite3_last_insert_rowid(s);
		}
	} else {
		mtree_id = 0;
	}

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
	if (mtree_id > 0)
		sqlite3_bind_int64(stmt_pkg, 6, mtree_id);
	else
		sqlite3_bind_null(stmt_pkg, 6);
	sqlite3_bind_text(stmt_pkg, 7, pkg_get(pkg, PKG_MESSAGE), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 8, pkg_get(pkg, PKG_ARCH), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 9, pkg_get(pkg, PKG_OSVERSION), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 10, pkg_get(pkg, PKG_MAINTAINER), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 11, pkg_get(pkg, PKG_WWW), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 12, pkg_get(pkg, PKG_PREFIX), -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt_pkg, 13, pkg_flatsize(pkg));

	if (sqlite3_step(stmt_pkg) != SQLITE_DONE) {
		retcode = ERROR_SQLITE(s);
		goto cleanup;
	}

	/*
	 * Get the generated package_id
	 */
	package_id = sqlite3_last_insert_rowid(s);

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

		if (sqlite3_step(stmt_dep) != SQLITE_DONE) {
			retcode = ERROR_SQLITE(s);
			goto cleanup;
		}

		sqlite3_reset(stmt_dep);
	}

	/*
	 * Insert conflicts list
	 */

	if (sqlite3_prepare_v2(s, sql_conflict, -1, &stmt_conflict, NULL) !=
						SQLITE_OK) {
		retcode = ERROR_SQLITE(s);
		goto cleanup;
	}

	conflicts = pkg_conflicts(pkg);
	for (i = 0; conflicts[i] != NULL; i++) {
		sqlite3_bind_text(stmt_conflict, 1, pkg_conflict_glob(conflicts[i]), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt_conflict, 2, package_id);

		if (sqlite3_step(stmt_conflict) != SQLITE_DONE) {
			retcode = ERROR_SQLITE(s);
			goto cleanup;
		}

		sqlite3_reset(stmt_conflict);
	}

	/*
	 * Insert file
	 */

	if (sqlite3_prepare_v2(s, sql_file, -1, &stmt_file, NULL) != SQLITE_OK) {
		retcode = ERROR_SQLITE(s);
		goto cleanup;
	}

	files = pkg_files(pkg);
	for (i = 0; files[i] != NULL; i++) {
		sqlite3_bind_text(stmt_file, 1, pkg_file_path(files[i]), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_file, 2, pkg_file_sha256(files[i]), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt_file, 3, package_id);

		if ((ret = sqlite3_step(stmt_file)) != SQLITE_DONE) {
			if ( ret == SQLITE_CONSTRAINT)
				retcode = pkg_error_set(EPKG_FATAL, "constraint violation on "
										"path with %s", pkg_file_path(files[i]));
			else
				retcode = ERROR_SQLITE(s);
			goto cleanup;
		}

		sqlite3_reset(stmt_file);
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
			retcode = ERROR_SQLITE(s);
			goto cleanup;
		}

		sqlite3_reset(stmt_script);
	}

	/*
	 * Insert execs
	 */

	if (sqlite3_prepare_v2(s, sql_exec, -1, &stmt_exec, NULL) != SQLITE_OK) {
		retcode = ERROR_SQLITE(s);
		goto cleanup;
	}

	execs = pkg_execs(pkg);
	for (i = 0; execs[i] != NULL; i++) {
		sqlite3_bind_text(stmt_exec, 1, pkg_exec_cmd(execs[i]), -1, SQLITE_STATIC);
		sqlite3_bind_int(stmt_exec, 2, pkg_exec_type(execs[i]));
		sqlite3_bind_int64(stmt_exec, 3, package_id);

		if (sqlite3_step(stmt_exec) != SQLITE_DONE) {
			retcode = ERROR_SQLITE(s);
			goto cleanup;
		}

		sqlite3_reset(stmt_exec);
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

	/*
	 * Register the package for real
	 */
	if (sqlite3_exec(s, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK) {
		retcode = pkg_error_set(EPKG_FATAL, "sqlite: %s", errmsg);
		sqlite3_free(errmsg);
	}

	cleanup:

	if (stmt_sel_mtree != NULL)
		sqlite3_finalize(stmt_sel_mtree);

	if (stmt_mtree != NULL)
		sqlite3_finalize(stmt_mtree);

	if (stmt_pkg != NULL)
		sqlite3_finalize(stmt_pkg);

	if (stmt_dep != NULL)
		sqlite3_finalize(stmt_dep);

	if (stmt_conflict != NULL)
		sqlite3_finalize(stmt_conflict);

	if (stmt_file != NULL)
		sqlite3_finalize(stmt_file);

	if (stmt_script != NULL)
		sqlite3_finalize(stmt_script);

	if (stmt_exec != NULL)
		sqlite3_finalize(stmt_exec);

	if (stmt_option != NULL)
		sqlite3_finalize(stmt_option);

	if (retcode != EPKG_OK && sqlite3_exec(db->sqlite, "ROLLBACK;", NULL, NULL, &errmsg) !=
		SQLITE_OK)
		err(1, "Can not rollback: %s", errmsg);

	return (retcode);
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
