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

#ifdef DEBUG
#include <dirent.h>
#include "pkg_compat.h"
#endif

#define PKG_DBDIR "/var/db/pkg"

static struct pkgdb_it * pkgdb_it_new(struct pkgdb *, sqlite3_stmt *, pkgdb_it_t);
static void pkgdb_regex(sqlite3_context *, int, sqlite3_value **, int);
static void pkgdb_regex_basic(sqlite3_context *, int, sqlite3_value **);
static void pkgdb_regex_extended(sqlite3_context *, int, sqlite3_value **);
static void pkgdb_regex_delete(void *);

static void
pkgdb_regex_delete(void *ctx)
{
	regex_t *re = NULL;

	if ((re = (regex_t *)sqlite3_get_auxdata(ctx, 0)) != NULL) {
		regfree(re);
		free(re);
	}
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
		"automatic INTEGER"
	");"
	"CREATE TABLE options ("
		"package_id TEXT,"
		"name TEXT,"
		"with INTEGER,"
		"PRIMARY KEY (package_id,name)"
	");"
	"CREATE INDEX options_package ON options (package_id);"
	"CREATE TABLE deps ("
		"origin TEXT,"
		"name TEXT,"
		"version TEXT,"
		"package_id TEXT,"
		"PRIMARY KEY (package_id,origin)"
	");"
	"CREATE INDEX deps_origin ON deps (origin);"
	"CREATE INDEX deps_package ON deps (package_id);"
	"CREATE TABLE files ("
		"path TEXT PRIMARY KEY,"
		"md5 TEXT,"
		"package_id TEXT"
	");"
	"CREATE INDEX files_package ON files (package_id);"
	"CREATE TABLE conflicts ("
		"name TEXT,"
		"package_id TEXT,"
		"PRIMARY KEY (package_id,name)"
	");"
	"CREATE INDEX conflicts_package ON conflicts (package_id);";

	if (sqlite3_exec(sdb, sql, NULL, NULL, &errmsg) != SQLITE_OK)
		errx(EXIT_FAILURE, "sqlite3_exec(): %s", errmsg);

#ifdef DEBUG
	struct dirent **dirs;
	struct pkg_manifest *m;
	sqlite3_stmt *stmt_pkg;
	sqlite3_stmt *stmt_dep;
	sqlite3_stmt *stmt_conflicts;
	sqlite3_stmt *stmt_file;
	const char *dbdir;
	const char *conflict;
	char mpath[MAXPATHLEN];
	int nb_pkg;
	int i;

	dbdir = pkgdb_get_dir();
	nb_pkg = scandir(dbdir, &dirs, select_dir, alphasort);

	sqlite3_exec(sdb, "BEGIN TRANSACTION;", NULL, NULL, NULL);

	sqlite3_prepare(sdb, "INSERT INTO packages (origin, name, version, comment, desc)"
			"VALUES (?1, ?2, ?3, ?4, ?5);",
			-1, &stmt_pkg, NULL);

	sqlite3_prepare(sdb, "INSERT INTO deps (origin, name, version, package_id)"
			"VALUES (?1, ?2, ?3, ?4);",
			-1, &stmt_dep, NULL);

	sqlite3_prepare(sdb, "INSERT INTO conflicts (name, package_id)"
			"VALUES (?1, ?2, ?3, ?4);",
			-1, &stmt_conflicts, NULL);

	sqlite3_prepare(sdb, "INSERT INTO files (path, md5, package_id)"
			"VALUES (?1, ?2, ?3);",
			-1, &stmt_file, NULL);

	for (i = 0; i < nb_pkg; i++) {
		snprintf(mpath, sizeof(mpath), "%s/%s/+MANIFEST", dbdir, dirs[i]->d_name);
		if ((m = pkg_manifest_load_file(mpath)) == NULL &&
                    (m = pkg_compat_convert_installed(dbdir, dirs[i]->d_name, mpath)) == NULL) {
               continue;
          }

		sqlite3_bind_text(stmt_pkg, 1, pkg_manifest_value(m, "origin"), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 2, pkg_manifest_value(m, "name"), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 3, pkg_manifest_value(m, "version"), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 4, pkg_manifest_value(m, "comment"), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 5, pkg_manifest_value(m, "desc"), -1, SQLITE_STATIC);

		sqlite3_step(stmt_pkg);
		sqlite3_reset(stmt_pkg);

		pkg_manifest_dep_init(m);
		while (pkg_manifest_dep_next(m) == 0) {
			sqlite3_bind_text(stmt_dep, 1, pkg_manifest_dep_origin(m), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_dep, 2, pkg_manifest_dep_name(m), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_dep, 3, pkg_manifest_dep_version(m), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_dep, 4, pkg_manifest_value(m, "origin"), -1, SQLITE_STATIC);

			sqlite3_step(stmt_dep);
			sqlite3_reset(stmt_dep);
		}

		pkg_manifest_conflict_init(m);
		while ((conflict = pkg_manifest_conflict_next(m)) != NULL) {
			sqlite3_bind_text(stmt_conflicts, 1, conflict, -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_conflicts, 2, pkg_manifest_value(m, "origin"), -1,
							  SQLITE_STATIC);

			sqlite3_step(stmt_conflicts);
			sqlite3_reset(stmt_conflicts);
		}

		pkg_manifest_file_init(m);
		while (pkg_manifest_file_next(m) == 0) {
			sqlite3_bind_text(stmt_file, 1, pkg_manifest_file_path(m), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_file, 2, pkg_manifest_file_md5(m), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_file, 3, pkg_manifest_value(m, "origin"), -1, SQLITE_STATIC);

			sqlite3_step(stmt_file);
			sqlite3_reset(stmt_file);
		}

		pkg_manifest_free(m);
		free(dirs[i]);
	}
	free(dirs);

	sqlite3_finalize(stmt_pkg);
	sqlite3_finalize(stmt_dep);
	sqlite3_finalize(stmt_conflicts);
	sqlite3_finalize(stmt_file);

	sqlite3_exec(sdb, "COMMIT;", NULL, NULL, NULL);
#endif
}

int
pkgdb_open(struct pkgdb **db)
{
	int retcode;
	struct stat st;
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
	struct pkgdb_it *i;

	assert(it->type == IT_PKG);

	switch (sqlite3_step(it->stmt)) {
	case SQLITE_ROW:
		if (*pkg_p == NULL)
			pkg_new(pkg_p);
		else
			pkg_reset(*pkg_p);
		pkg = *pkg_p;

		strlcpy(pkg->origin, sqlite3_column_text(it->stmt, 0), sizeof(pkg->origin));
		strlcpy(pkg->name, sqlite3_column_text(it->stmt, 1), sizeof(pkg->name));
		strlcpy(pkg->version, sqlite3_column_text(it->stmt, 2), sizeof(pkg->version));
		strlcpy(pkg->comment, sqlite3_column_text(it->stmt, 3), sizeof(pkg->comment));
		pkg->desc = strdup(sqlite3_column_text(it->stmt, 4));

		if (flags & PKG_DEPS) {
			array_init(&pkg->deps, 10);

			i = pkgdb_query_dep(it->db, pkg->origin);
			p = NULL;
			while (pkgdb_it_next_pkg(i, &p, PKG_BASIC) == 0) {
				array_append(&pkg->deps, p);
				p = NULL;
			}
			pkgdb_it_free(i);

			array_append(&pkg->deps, NULL);
		}

		if (flags & PKG_RDEPS) {
			array_init(&pkg->rdeps, 5);

			i = pkgdb_query_rdep(it->db, pkg->origin);
			p = NULL;
			while (pkgdb_it_next_pkg(i, &p, PKG_BASIC) == 0) {
				array_append(&pkg->rdeps, p);
				p = NULL;
			}
			pkgdb_it_free(i);

			array_append(&pkg->rdeps, NULL);
		}

		if (flags & PKG_CONFLICTS) {
			array_init(&pkg->conflicts, 5);

			i = pkgdb_query_conflicts(it->db, pkg->origin);
			c = NULL;
			while (pkgdb_it_next_conflict(i, &c) == 0) {
				array_append(&pkg->conflicts, c);
				c = NULL;
			}
			pkgdb_it_free(i);

			array_append(&pkg->conflicts, NULL);
		}

		if (flags & PKG_FILES) {
			array_init(&pkg->files, 10);

			i = pkgdb_query_files(it->db, pkg->origin);
			f = NULL;
			while (pkgdb_it_next_file(i, &f) == 0) {
				array_append(&pkg->files, f);
				f = NULL;
			}
			pkgdb_it_free(i);

			array_append(&pkg->files, NULL);
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

		strlcpy(c->origin, sqlite3_column_text(it->stmt, 0), sizeof(c->origin));
		strlcpy(c->name, sqlite3_column_text(it->stmt, 1), sizeof(c->name));
		strlcpy(c->version, sqlite3_column_text(it->stmt, 2), sizeof(c->version));
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
		strlcpy(file->md5, sqlite3_column_text(it->stmt, 1), sizeof(file->md5));
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

	if (match != MATCH_ALL && pattern == NULL) {
		pkgdb_set_error(db, 0, "missing pattern");
		return (NULL);
	}

	switch (match) {
	case MATCH_ALL:
		comp = "";
		break;
	case MATCH_EXACT:
		comp = " WHERE name = ?1";
		break;
	case MATCH_GLOB:
		comp = " WHERE name GLOB ?1";
		break;
	case MATCH_REGEX:
		comp = " WHERE name REGEXP ?1";
		break;
	case MATCH_EREGEX:
		comp = " WHERE EREGEXP(?1, name)";
		break;
	}

	snprintf(sql, sizeof(sql),
			"SELECT origin, name, version, comment, desc FROM packages%s;", comp);

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
					"SELECT origin, name, version, comment, desc FROM packages, files "
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
					"SELECT path, md5 "
					"FROM files "
					"WHERE package_id = ?1;", -1, &stmt, NULL);
	sqlite3_bind_text(stmt, 1, origin, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, IT_FILE));

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
