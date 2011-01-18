#include <sys/param.h>
#include <sys/stat.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sqlite3.h>

#include "pkg.h"
#include "pkg_private.h"
#include "pkgdb.h"
#include "util.h"

#define PKG_DBDIR "/var/db/pkg"

static struct pkgdb_it * pkgdb_it_new(struct pkgdb *, sqlite3_stmt *, pkgdb_it_t);
static void pkgdb_regex(sqlite3_context *, int, sqlite3_value **, int);
static void pkgdb_regex_basic(sqlite3_context *, int, sqlite3_value **);
static void pkgdb_regex_extended(sqlite3_context *, int, sqlite3_value **);
static void pkgdb_regex_delete(void *);

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
	const char *regex = NULL;
	const char *str;
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

static void
pkgdb_init(sqlite3 *sdb)
{
	char *errmsg;
	const char sql[] = ""
	"CREATE TABLE packages ("
		"origin TEXT PRIMARY KEY,"
		"name TEXT,"
		"version TEXT,"
		"comment TEXT,"
		"desc TEXT,"
		"mtree TEXT,"
		"message TEXT,"
		"automatic INTEGER,"
		"pkg_format_version INTEGER"
	");"
	"CREATE TABLE scripts ("
		"package_id TEXT REFERENCES packages(origin) ON DELETE CASCADE, "
		"script TEXT,"
		"type INTEGER,"
		"PRIMARY KEY (package_id, type)"
	");"
	"CREATE INDEX scripts_package ON scripts (package_id);"
	"CREATE TABLE exec ("
		"package_id TEXT REFERENCES packages(origin) ON DELETE CASCADE, "
		"cmd TEXT,"
		"type INTEGER"
	");"
	"CREATE INDEX exec_package ON exec (package_id);"
	"CREATE TABLE options ("
		"package_id TEXT REFERENCES packages(origin) ON DELETE CASCADE, "
		"name TEXT,"
		"with INTEGER,"
		"PRIMARY KEY (package_id,name)"
	");"
	"CREATE INDEX options_package ON options (package_id);"
	"CREATE TABLE deps ("
		"origin TEXT,"
		"name TEXT,"
		"version TEXT,"
		"package_id TEXT REFERENCES packages(origin) ON DELETE CASCADE, "
		"PRIMARY KEY (package_id,origin)"
	");"
	"CREATE INDEX deps_origin ON deps (origin);"
	"CREATE INDEX deps_package ON deps (package_id);"
	"CREATE TABLE files ("
		"path TEXT PRIMARY KEY,"
		"sha256 TEXT,"
		"package_id TEXT REFERENCES packages(origin) ON DELETE CASCADE"
	");"
	"CREATE INDEX files_package ON files (package_id);"
	"CREATE TABLE conflicts ("
		"name TEXT,"
		"package_id TEXT REFERENCES packages(origin) ON DELETE CASCADE, "
		"PRIMARY KEY (package_id,name)"
	");"
	"CREATE INDEX conflicts_package ON conflicts (package_id);";

	if (sqlite3_exec(sdb, sql, NULL, NULL, &errmsg) != SQLITE_OK)
		errx(EXIT_FAILURE, "sqlite3_exec(): %s", errmsg);
}

int
pkgdb_open(struct pkgdb **db)
{
	int retcode;
	struct stat st;
	char *errmsg;
	char fpath[MAXPATHLEN];

	snprintf(fpath, sizeof(fpath), "%s/pkg.db", pkgdb_get_dir());

	if ((*db = malloc(sizeof(struct pkgdb))) == NULL)
		err(EXIT_FAILURE, "malloc()");

	if ((retcode = stat(fpath, &st)) == -1 && errno != ENOENT) {
		pkgdb_set_error(*db, errno, NULL);
		return (-1);
	}

	if (sqlite3_open(fpath, &(*db)->sqlite) != SQLITE_OK) {
		pkgdb_set_error(*db, 0, "sqlite3_open(): %s", sqlite3_errmsg((*db)->sqlite));
		return (-1);
	}

	if (retcode == -1)
		pkgdb_init((*db)->sqlite);

	sqlite3_create_function((*db)->sqlite, "regexp", 2, SQLITE_ANY, NULL,
							pkgdb_regex_basic, NULL, NULL);
	sqlite3_create_function((*db)->sqlite, "eregexp", 2, SQLITE_ANY, NULL,
							pkgdb_regex_extended, NULL, NULL);

	/* 
	 * allow forign key option which will allow to have clean support for
	 * reinstalling
	 */
	if (sqlite3_exec((*db)->sqlite, "PRAGMA foreign_keys = ON;", NULL, NULL, &errmsg) != SQLITE_OK)
		errx(EXIT_FAILURE, "sqlite3_exec(): %s", errmsg);

	(*db)->errnum = 0;
	(*db)->errstring[0] = '\0';

	return (0);
}

void
pkgdb_close(struct pkgdb *db)
{
	sqlite3_close(db->sqlite);
	free(db);
}

static struct pkgdb_it *
pkgdb_it_new(struct pkgdb *db, sqlite3_stmt *s, pkgdb_it_t t)
{
	struct pkgdb_it *it = malloc(sizeof(struct pkgdb_it));
	if (it == NULL)
		return (NULL);

	it->db = db;
	it->stmt = s;
	it->type = t;
	return (it);
}

int
pkgdb_it_next_pkg(struct pkgdb_it *it, struct pkg **pkg_p, int flags)
{
	struct pkg *pkg;
	struct pkg *p;
	struct pkg_conflict *c;
	struct pkg_file *f;
	struct pkg_exec *e;
	struct pkgdb_it *i;

	assert(it->type == IT_PKG);

	switch (sqlite3_step(it->stmt)) {
	case SQLITE_ROW:
		if (*pkg_p == NULL)
			pkg_new(pkg_p);
		else
			pkg_reset(*pkg_p);
		pkg = *pkg_p;

		pkg->type = PKG_INSTALLED;
		pkg_set(pkg, PKG_ORIGIN, sqlite3_column_text(it->stmt, 0));
		pkg_set(pkg, PKG_NAME, sqlite3_column_text(it->stmt, 1));
		pkg_set(pkg, PKG_VERSION, sqlite3_column_text(it->stmt, 2));
		pkg_set(pkg, PKG_COMMENT, sqlite3_column_text(it->stmt, 3));
		pkg_set(pkg, PKG_DESC, sqlite3_column_text(it->stmt, 4));
		pkg_set(pkg, PKG_MTREE, sqlite3_column_text(it->stmt, 5));

		if (flags & PKG_DEPS) {
			array_init(&pkg->deps, 10);

			i = pkgdb_query_dep(it->db, pkg_get(pkg, PKG_ORIGIN));
			p = NULL;
			while (pkgdb_it_next_pkg(i, &p, PKG_BASIC) == 0) {
				array_append(&pkg->deps, p);
				p = NULL;
			}
			pkgdb_it_free(i);
		}

		if (flags & PKG_RDEPS) {
			array_init(&pkg->rdeps, 5);

			i = pkgdb_query_rdep(it->db, pkg_get(pkg, PKG_ORIGIN));
			p = NULL;
			while (pkgdb_it_next_pkg(i, &p, PKG_BASIC) == 0) {
				array_append(&pkg->rdeps, p);
				p = NULL;
			}
			pkgdb_it_free(i);
		}

		if (flags & PKG_CONFLICTS) {
			array_init(&pkg->conflicts, 5);

			i = pkgdb_query_conflicts(it->db, pkg_get(pkg, PKG_ORIGIN));
			c = NULL;
			while (pkgdb_it_next_conflict(i, &c) == 0) {
				array_append(&pkg->conflicts, c);
				c = NULL;
			}
			pkgdb_it_free(i);
		}

		if (flags & PKG_FILES) {
			array_init(&pkg->files, 10);

			i = pkgdb_query_files(it->db, pkg_get(pkg, PKG_ORIGIN));
			f = NULL;
			while (pkgdb_it_next_file(i, &f) == 0) {
				array_append(&pkg->files, f);
				f = NULL;
			}
			pkgdb_it_free(i);
		}

		if (flags & PKG_EXECS) {
			array_init(&pkg->exec, 5);

			i = pkgdb_query_execs(it->db, pkg_get(pkg, PKG_ORIGIN));
			e = NULL;
			while (pkgdb_it_next_exec(i, &e) == 0) {
				array_append(&pkg->exec, e);
				e = NULL;
			}
			pkgdb_it_free(i);
		}
		return (0);
	case SQLITE_DONE:
		return (1);
	default:
		pkgdb_set_error(it->db, 0, "sqlite3_step(): %s", sqlite3_errmsg(it->db->sqlite));
		return (-1);
	}
}

int
pkgdb_it_next_conflict(struct pkgdb_it *it, struct pkg_conflict **c_p)
{
	struct pkg_conflict *c;

	assert(it->type == IT_CONFLICT);

	switch (sqlite3_step(it->stmt)) {
	case SQLITE_ROW:
		if (*c_p == NULL)
			pkg_conflict_new(c_p);
		else
			pkg_conflict_reset(*c_p);
		c = *c_p;

		sbuf_set(&c->glob, sqlite3_column_text(it->stmt, 0));

		return (0);
	case SQLITE_DONE:
		return (1);
	default:
		pkgdb_set_error(it->db, 0, "sqlite3_step(): %s", sqlite3_errmsg(it->db->sqlite));
		return (-1);
	}
}

int
pkgdb_it_next_file(struct pkgdb_it *it, struct pkg_file **file_p)
{
	struct pkg_file *file;

	assert(it->type == IT_FILE);

	switch (sqlite3_step(it->stmt)) {
	case SQLITE_ROW:
		if (*file_p == NULL)
			pkg_file_new(file_p);
		else
			pkg_file_reset(*file_p);
		file = *file_p;

		strlcpy(file->path, sqlite3_column_text(it->stmt, 0), sizeof(file->path));
		strlcpy(file->sha256, sqlite3_column_text(it->stmt, 1), sizeof(file->sha256));
		return (0);
	case SQLITE_DONE:
		return (1);
	default:
		pkgdb_set_error(it->db, 0, "sqlite3_step(): %s", sqlite3_errmsg(it->db->sqlite));
		return (-1);
	}
}

int
pkgdb_it_next_exec(struct pkgdb_it *it, struct pkg_exec **exec_p)
{
	struct pkg_exec *exec;

	assert(it->type == IT_EXEC);

	switch (sqlite3_step(it->stmt)) {
		case SQLITE_ROW:
			if (*exec_p == NULL)
				pkg_exec_new(exec_p);
			else
				pkg_exec_reset(*exec_p);

			exec = *exec_p;
			sbuf_set(&exec->cmd, sqlite3_column_text(it->stmt, 0));
			exec->type = sqlite3_column_int(it->stmt, 1);
			return (0);
		case SQLITE_DONE:
			return (1);
		default:
			pkgdb_set_error(it->db, 0, "sqlite3_step(): %s", sqlite3_errmsg(it->db->sqlite));
			return (-1);
	}
}

void
pkgdb_it_free(struct pkgdb_it *it)
{
	sqlite3_finalize(it->stmt);
	free(it);
}

struct pkgdb_it *
pkgdb_query(struct pkgdb *db, const char *pattern, match_t match)
{
	char sql[BUFSIZ];
	sqlite3_stmt *stmt;
	const char *comp = NULL;
	char *checkorigin = NULL;

	if (match != MATCH_ALL && pattern == NULL) {
		pkgdb_set_error(db, 0, "missing pattern");
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
			comp = " WHERE name = ?1";
		else
			comp = " WHERE origin = ?1";
		break;
	case MATCH_GLOB:
		if (checkorigin == NULL)
			comp = " WHERE name GLOB ?1";
		else
			comp = " WHERE origin GLOB ?1";
		break;
	case MATCH_REGEX:
		if (checkorigin == NULL)
			comp = " WHERE name REGEXP ?1";
		else
			comp = " WHERE origin REGEXP ?1";
		break;
	case MATCH_EREGEX:
		if (checkorigin == NULL)
			comp = " WHERE EREGEXP(?1, name)";
		else
			comp = " WHERE EREGEXP(?1, origin)";
		break;
	}

	snprintf(sql, sizeof(sql),
			"SELECT origin, name, version, comment, desc, mtree FROM packages%s;", comp);

	sqlite3_prepare(db->sqlite, sql, -1, &stmt, NULL);

	if (match != MATCH_ALL)
		sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, IT_PKG));
}

struct pkgdb_it *
pkgdb_query_which(struct pkgdb *db, const char *path)
{
	sqlite3_stmt *stmt;

	sqlite3_prepare(db->sqlite,
					"SELECT origin, name, version, comment, desc, mtree FROM packages, files "
					"WHERE origin = files.package_id "
					"AND files.path = ?1;", -1, &stmt, NULL);
	sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, IT_PKG));
}

struct pkgdb_it *
pkgdb_query_dep(struct pkgdb *db, const char *origin) {
	sqlite3_stmt *stmt;

	sqlite3_prepare(db->sqlite,
					"SELECT p.origin, p.name, p.version, p.comment, p.desc "
					"FROM packages AS p, deps AS d "
					"WHERE p.origin = d.origin "
					"AND d.package_id = ?1;", -1, &stmt, NULL);
	sqlite3_bind_text(stmt, 1, origin, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, IT_PKG));
}

struct pkgdb_it *
pkgdb_query_rdep(struct pkgdb *db, const char *origin) {
	sqlite3_stmt *stmt;

	sqlite3_prepare(db->sqlite,
					"SELECT p.origin, p.name, p.version, p.comment, p.desc "
					"FROM packages AS p, deps AS d "
					"WHERE p.origin = d.package_id "
					"AND d.origin = ?1;", -1, &stmt, NULL);
	sqlite3_bind_text(stmt, 1, origin, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, IT_PKG));
}

struct pkgdb_it *
pkgdb_query_conflicts(struct pkgdb *db, const char *origin) {
	sqlite3_stmt *stmt;

	sqlite3_prepare(db->sqlite,
					"SELECT name, origin, version "
					"FROM conflicts "
					"WHERE package_id = ?1;", -1, &stmt, NULL);
	sqlite3_bind_text(stmt, 1, origin, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, IT_CONFLICT));
}

struct pkgdb_it *
pkgdb_query_files(struct pkgdb *db, const char *origin) {
	sqlite3_stmt *stmt;

	sqlite3_prepare(db->sqlite,
					"SELECT path, sha256 "
					"FROM files "
					"WHERE package_id = ?1;", -1, &stmt, NULL);
	sqlite3_bind_text(stmt, 1, origin, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, IT_FILE));

}

struct pkgdb_it *
pkgdb_query_execs(struct pkgdb *db, const char *origin) {
	sqlite3_stmt *stmt;
	sqlite3_prepare(db->sqlite, 
			"SELECT cmd, type "
			"FROM exec "
			"WHERE package_id = ?1", -1, &stmt, NULL);

	sqlite3_bind_text(stmt, 1, origin, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, IT_EXEC));
}

void
pkgdb_set_error(struct pkgdb *db, int errnum, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(db->errstring, sizeof(db->errstring), fmt, args);
	va_end(args);

	db->errnum = errnum;
}

void
pkgdb_warn(struct pkgdb *db)
{
	warnx("%s %s", db->errstring, (db->errnum > 0) ? strerror(db->errnum) : "");
}

int
pkgdb_errnum(struct pkgdb *db)
{
	return (db->errnum);
}

int
pkgdb_register_pkg(struct pkgdb *db, struct pkg *pkg)
{
	struct pkg **deps;
	struct pkg_file **files;
	struct pkg_conflict **conflicts;
	struct pkg_exec **execs;
	struct pkg_script **scripts;
	sqlite3_stmt *stmt_pkg;
	sqlite3_stmt *stmt_dep;
	sqlite3_stmt *stmt_conflicts;
	sqlite3_stmt *stmt_file;
	sqlite3_stmt *stmt_exec;
	sqlite3_stmt *stmt_scripts;
	int i;

	sqlite3_exec(db->sqlite, "BEGIN TRANSACTION;", NULL, NULL, NULL);

	sqlite3_prepare(db->sqlite, "INSERT OR REPLACE INTO packages (origin, name, version, comment, desc, mtree, message) "
			"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
			-1, &stmt_pkg, NULL);

	sqlite3_prepare(db->sqlite, "INSERT INTO deps (origin, name, version, package_id)"
			"VALUES (?1, ?2, ?3, ?4);",
			-1, &stmt_dep, NULL);

	sqlite3_prepare(db->sqlite, "INSERT INTO conflicts (name, package_id)"
			"VALUES (?1, ?2, ?3, ?4);",
			-1, &stmt_conflicts, NULL);

	sqlite3_prepare(db->sqlite, "INSERT INTO files (path, sha256, package_id)"
			"VALUES (?1, ?2, ?3);",
			-1, &stmt_file, NULL);

	sqlite3_prepare(db->sqlite, "INSERT INTO scripts (script, type, package_id)"
			"values (?1, ?2, ?3);",
			-1, &stmt_scripts, NULL);

	sqlite3_prepare(db->sqlite, "INSERT INTO exec (cmd, type, package_id)"
			"values (?1, ?2, ?3);",
			-1, &stmt_exec, NULL);


	sqlite3_bind_text(stmt_pkg, 1, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 2, pkg_get(pkg, PKG_NAME), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 3, pkg_get(pkg, PKG_VERSION), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 4, pkg_get(pkg, PKG_COMMENT), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 5, pkg_get(pkg, PKG_DESC), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 6, pkg_get(pkg, PKG_MTREE), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt_pkg, 7, pkg_get(pkg, PKG_MESSAGE), -1, SQLITE_STATIC);

	sqlite3_step(stmt_pkg);

	deps = pkg_deps(pkg);

	if (deps != NULL)
		for (i = 0; deps[i] != NULL; i++) {
			sqlite3_bind_text(stmt_dep, 1, pkg_get(deps[i], PKG_ORIGIN), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_dep, 2, pkg_get(deps[i], PKG_NAME), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_dep, 3, pkg_get(deps[i], PKG_VERSION), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_dep, 4, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);

			sqlite3_step(stmt_dep);
			sqlite3_reset(stmt_dep);
		}

	conflicts = pkg_conflicts(pkg);
	if (conflicts != NULL)
		for (i = 0; conflicts[i] != NULL; i++) {
			sqlite3_bind_text(stmt_conflicts, 1, pkg_conflict_glob(conflicts[i]), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_conflicts, 2, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);
		}


	files = pkg_files(pkg);
	if (files != NULL)
		for (i = 0; files[i] != NULL; i++) {
			sqlite3_bind_text(stmt_file, 1, pkg_file_path(files[i]), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_file, 2, pkg_file_sha256(files[i]), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_file, 3, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);

			sqlite3_step(stmt_file);
			sqlite3_reset(stmt_file);
		}

	scripts = pkg_scripts(pkg);
	if (scripts != NULL)
		for (i = 0; scripts[i] != NULL; i++) {
			sqlite3_bind_text(stmt_scripts, 1, pkg_script_data(scripts[i]), -1, SQLITE_STATIC);
			sqlite3_bind_int(stmt_scripts, 2, pkg_script_type(scripts[i]));
			sqlite3_bind_text(stmt_scripts, 3, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);

			sqlite3_step(stmt_scripts);
			sqlite3_reset(stmt_scripts);
		}
	execs = pkg_execs(pkg);
	if (execs != NULL)
		for (i = 0; execs[i] != NULL; i++) {
			sqlite3_bind_text(stmt_exec, 1, pkg_exec_cmd(execs[i]), -1, SQLITE_STATIC);
			sqlite3_bind_int(stmt_exec, 2, pkg_exec_type(execs[i]));
			sqlite3_bind_text(stmt_exec, 3, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);
			
			sqlite3_step(stmt_exec);
			sqlite3_reset(stmt_exec);
		}
	sqlite3_finalize(stmt_pkg);
	sqlite3_finalize(stmt_dep);
	sqlite3_finalize(stmt_conflicts);
	sqlite3_finalize(stmt_file);
	sqlite3_finalize(stmt_exec);
	sqlite3_finalize(stmt_scripts);

	sqlite3_exec(db->sqlite, "COMMIT;", NULL, NULL, NULL);

	
	return (0);
}
