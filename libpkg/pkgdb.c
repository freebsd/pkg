#include <sys/param.h>
#include <sys/stat.h>
#include <sys/file.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

#include "pkg.h"
#include "pkg_private.h"
#include "pkgdb.h"

#ifdef DEBUG
#include <dirent.h>
#include "pkg_manifest.h"
#include "util.h"
#endif

#define PKGDB_LOCK "lock"
#define PKG_DBDIR "/var/db/pkg"

static void pkgdb_stmt_to_pkg(sqlite3_stmt *, struct pkg *);

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
		"desc TEXT"
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
	"CREATE INDEX files_package ON files (package_id);";

	if (sqlite3_exec(sdb, sql, NULL, NULL, &errmsg) != SQLITE_OK)
		errx(EXIT_FAILURE, "sqlite3_exec(): %s", errmsg);

#ifdef DEBUG
	struct dirent **dirs;
	struct pkg_manifest *m;
	sqlite3_stmt *stmt_pkg;
	sqlite3_stmt *stmt_dep;
	sqlite3_stmt *stmt_file;
	const char *dbdir;
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

	sqlite3_prepare(sdb, "INSERT INTO files (path, md5, package_id)"
			"VALUES (?1, ?2, ?3);",
			-1, &stmt_file, NULL);

	for (i = 0; i < nb_pkg; i++) {
		snprintf(mpath, sizeof(mpath), "%s/%s/+MANIFEST", dbdir, dirs[i]->d_name);
		if ((m = pkg_manifest_load_file(mpath)) == NULL)
			continue;

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
	sqlite3_finalize(stmt_file);

	sqlite3_exec(sdb, "COMMIT;", NULL, NULL, NULL);
#endif
}

/* TODO: register hook for REGEX and EREGEX */
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

	(*db)->stmt = NULL;
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

int
pkgdb_query_init(struct pkgdb *db, const char *pattern, match_t match)
{
	char sql[BUFSIZ];
	const char *comp = NULL;

	if (match != MATCH_ALL && pattern == NULL) {
		pkgdb_set_error(db, 0, "missing pattern");
		return (-1);
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
		comp = " WHERE name REGEX ?1";
		break;
	case MATCH_EREGEX:
		comp = " WHERE name EREGEX ?1";
		break;
	}

	snprintf(sql, sizeof(sql),
			"SELECT origin, name, version, comment, desc FROM packages%s;", comp);

	sqlite3_prepare(db->sqlite, sql, -1, &db->stmt, NULL);

	if (match != MATCH_ALL)
		sqlite3_bind_text(db->stmt, 1, pattern, -1, SQLITE_TRANSIENT);

	return (0);
}

void
pkgdb_query_free(struct pkgdb *db)
{
	sqlite3_finalize(db->stmt);
	db->stmt = NULL;
}

int
pkgdb_query(struct pkgdb *db, struct pkg *pkg)
{
	int retcode;

	pkg_reset(pkg);
	retcode = sqlite3_step(db->stmt);

	if (retcode == SQLITE_ROW) {
		pkgdb_stmt_to_pkg(db->stmt, pkg);
		pkg->pdb = db;
		return (0);
	} else if (retcode == SQLITE_DONE) {
		sqlite3_reset(db->stmt);
		return (1);
	} else {
		pkgdb_set_error(db, 0, "sqlite3_step(): %s", sqlite3_errmsg(db->sqlite));
		return (-1);
	}
}

int
pkgdb_query_which(struct pkgdb *db, const char *path, struct pkg *pkg)
{
	int retcode;

	pkg_reset(pkg);
	sqlite3_prepare(db->sqlite,
					"SELECT origin, name, version, comment, desc FROM packages, files "
					"WHERE origin = files.package_id "
					"AND files.path = ?1;", -1, &pkg->which_stmt, NULL);
	sqlite3_bind_text(pkg->which_stmt, 1, path, -1, SQLITE_STATIC);

	retcode = sqlite3_step(pkg->which_stmt);
	if (retcode == SQLITE_ROW) {
		pkgdb_stmt_to_pkg(pkg->which_stmt, pkg);
		pkg->pdb = db;
	}

	return ((retcode == SQLITE_ROW) ? 0 : 1);
}

int
pkgdb_query_dep(struct pkg *pkg, struct pkg *dep) {
	int retcode;

	if (pkg->deps_stmt == NULL) {
		sqlite3_prepare(pkg->pdb->sqlite,
						"SELECT p.origin, p.name, p.version, p.comment, p.desc FROM packages AS p, deps AS d "
						"WHERE p.origin = d.origin "
						"AND d.package_id = ?1;", -1, &pkg->deps_stmt, NULL);
		sqlite3_bind_text(pkg->deps_stmt, 1, pkg->origin, -1, SQLITE_STATIC);
	}

	retcode = sqlite3_step(pkg->deps_stmt);
	if (retcode == SQLITE_ROW) {
		pkgdb_stmt_to_pkg(pkg->deps_stmt, dep);
		dep->pdb = pkg->pdb;
		return (0);
	} else if (retcode == SQLITE_DONE) {
		sqlite3_reset(pkg->deps_stmt);
		return (1);
	} else {
		return (-1);
	}
}

int
pkgdb_query_rdep(struct pkg *pkg, struct pkg *rdep) {
	int retcode;

	if (pkg->rdeps_stmt == NULL) {
		sqlite3_prepare(pkg->pdb->sqlite,
						"SELECT p.origin, p.name, p.version, p.comment, p.desc FROM packages AS p, deps AS d "
						"WHERE p.origin = d.package_id "
						"AND d.origin = ?1;", -1, &pkg->rdeps_stmt, NULL);
		sqlite3_bind_text(pkg->rdeps_stmt, 1, pkg->origin, -1, SQLITE_STATIC);
	}

	retcode = sqlite3_step(pkg->rdeps_stmt);
	if (retcode == SQLITE_ROW) {
		pkgdb_stmt_to_pkg(pkg->rdeps_stmt, rdep);
		rdep->pdb = pkg->pdb;
		return (0);
	} else if (retcode == SQLITE_DONE) {
		sqlite3_reset(pkg->rdeps_stmt);
		return (1);
	} else {
		return (-1);
	}
}

static void
pkgdb_stmt_to_pkg(sqlite3_stmt *stmt, struct pkg *pkg)
{
		pkg->origin = sqlite3_column_text(stmt, 0);
		pkg->name = sqlite3_column_text(stmt, 1);
		pkg->version = sqlite3_column_text(stmt, 2);
		pkg->comment = sqlite3_column_text(stmt, 3);
		pkg->desc = sqlite3_column_text(stmt, 4);
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
