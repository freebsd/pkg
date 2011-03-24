#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fnmatch.h>
#include <sqlite3.h>
#include <fts.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "pkg.h"
#include "pkg_error.h"
#include "pkg_private.h"
#include "pkg_util.h"

int
pkg_create_repo(char *path, void (progress)(struct pkg *pkg, void *data), void *data)
{
	FTS *fts = NULL;
	FTSENT *ent = NULL;

	struct stat st;
	struct pkg *pkg = NULL;
	struct pkg **deps;
	char *ext = NULL;
	sqlite3 *sqlite = NULL;
	sqlite3_stmt *stmt_deps = NULL;
	sqlite3_stmt *stmt_pkg = NULL;
	int64_t package_id;
	char *errmsg = NULL;
	int retcode = EPKG_OK;

	int i;

	char *repopath[2];
	char repodb[MAXPATHLEN];

	const char initsql[] = ""
		"CREATE TABLE packages ("
			"id INTEGER PRIMARY KEY,"
			"origin TEXT UNIQUE,"
			"name TEXT,"
			"version TEXT,"
			"comment TEXT,"
			"desc TEXT,"
			"arch TEXT,"
			"osversion TEXT,"
			"maintainer TEXT,"
			"www TEXT,"
			"pkg_format_version INTEGER,"
			"pkgsize INTEGER,"
			"flatsize INTEGER"
		");"
		"CREATE TABLE deps ("
			"origin TEXT,"
			"name TEXT,"
			"version TEXT,"
			"package_id INTEGER REFERENCES packages(id),"
			"PRIMARY KEY (package_id, origin)"
		");"
		"CREATE INDEX deps_origin ON deps (origin);"
		"CREATE INDEX deps_package ON deps (package_id);";
	const char pkgsql[] = ""
		"INSERT INTO packages ("
				"origin, name, version, comment, desc, arch, osversion, "
				"maintainer, www, pkg_format_version, pkgsize, flatsize "
		")"
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12);";
	const char depssql[] = ""
		"INSERT INTO deps (origin, name, version, package_id) "
		"VALUES (?1, ?2, ?3, ?4);";

	if (!is_dir(path))
		return (pkg_error_set(EPKG_FATAL, "%s is not a directory", path));

	repopath[0] = path;
	repopath[1] = NULL;

	snprintf(repodb, MAXPATHLEN, "%s/repo.db", path);

	if (stat(repodb, &st) != -1)
		if (unlink(repodb) != 0)
			return (pkg_error_set(EPKG_FATAL, "can not unlink %s: %s", repodb,
								  strerror(errno)));

	if (sqlite3_open(repodb, &sqlite) != SQLITE_OK)
		return (EPKG_FATAL);

	if (sqlite3_exec(sqlite, initsql, NULL, NULL, &errmsg) != SQLITE_OK) {
		retcode = pkg_error_set(EPKG_FATAL, "%s", errmsg);
		goto cleanup;
	}

	if (sqlite3_exec(sqlite, "BEGIN TRANSACTION;", NULL, NULL, &errmsg) !=
		SQLITE_OK) {
		retcode = pkg_error_set(EPKG_FATAL, "%s", errmsg);
		goto cleanup;
	}

	if (sqlite3_prepare(sqlite, pkgsql, -1, &stmt_pkg, NULL) != SQLITE_OK) {
		retcode = ERROR_SQLITE(sqlite);
		goto cleanup;
	}

	if (sqlite3_prepare(sqlite, depssql, -1, &stmt_deps, NULL) != SQLITE_OK) {
		retcode = ERROR_SQLITE(sqlite);
		goto cleanup;
	}

	if ((fts = fts_open(repopath, FTS_PHYSICAL, NULL)) == NULL) {
		retcode = pkg_error_set(EPKG_FATAL, "can not open %s: %s", repopath,
								strerror(errno));
		goto cleanup;
	}

	while ((ent = fts_read(fts)) != NULL) {
		/* skip everything that is not a file */
		if (ent->fts_info != FTS_F)
			continue;

		ext = strrchr(ent->fts_name, '.');

		if (ext == NULL)
			continue;

		if (strcmp(ext, ".tgz") != 0 &&
				strcmp(ext, ".tbz") != 0 &&
				strcmp(ext, ".txz") != 0 &&
				strcmp(ext, ".tar") != 0)
			continue;

		if (pkg_open(&pkg, ent->fts_accpath) != EPKG_OK) {
			if (progress != NULL) {
				pkg_error_set(EPKG_WARN, "can not open %s: %s", ent->fts_name,
							  pkg_error_string());
				progress(NULL, data);
			}
			continue;
		}

		if (progress != NULL)
			progress(pkg, data);

		sqlite3_bind_text(stmt_pkg, 1, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 2, pkg_get(pkg, PKG_NAME), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 3, pkg_get(pkg, PKG_VERSION), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 4, pkg_get(pkg, PKG_COMMENT), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 5, pkg_get(pkg, PKG_DESC), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 6, pkg_get(pkg, PKG_ARCH), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 7, pkg_get(pkg, PKG_OSVERSION), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 8, pkg_get(pkg, PKG_MAINTAINER), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 9, pkg_get(pkg, PKG_WWW), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt_pkg, 11, ent->fts_statp->st_size);
		sqlite3_bind_int64(stmt_pkg, 12, pkg_flatsize(pkg));

		if (sqlite3_step(stmt_pkg) != SQLITE_DONE) {
			retcode = ERROR_SQLITE(sqlite);
			goto cleanup;
		}
		sqlite3_reset(stmt_pkg);

		package_id = sqlite3_last_insert_rowid(sqlite);

		deps = pkg_deps(pkg);
		for (i = 0; deps[i] != NULL; i++) {
			sqlite3_bind_text(stmt_deps, 1, pkg_get(deps[i], PKG_ORIGIN), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_deps, 2, pkg_get(deps[i], PKG_NAME), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_deps, 3, pkg_get(deps[i], PKG_VERSION), -1, SQLITE_STATIC);
			sqlite3_bind_int64(stmt_deps, 4, package_id);

			if (sqlite3_step(stmt_deps) != SQLITE_DONE) {
				retcode = ERROR_SQLITE(sqlite);
				goto cleanup;
			}
			sqlite3_reset(stmt_deps);
		}

	}

	if (sqlite3_exec(sqlite, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
		retcode = pkg_error_set(EPKG_FATAL, "%s", errmsg);

	cleanup:
	if (fts != NULL)
		fts_close(fts);

	if (pkg != NULL)
		pkg_free(pkg);

	if (stmt_pkg != NULL)
		sqlite3_finalize(stmt_pkg);

	if (stmt_deps != NULL)
		sqlite3_finalize(stmt_deps);

	if (sqlite != NULL)
		sqlite3_close(sqlite);

	if (errmsg != NULL)
		sqlite3_free(errmsg);

	return (retcode);
}
