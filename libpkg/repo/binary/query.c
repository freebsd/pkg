/* Copyright (c) 2014, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <errno.h>
#include <regex.h>
#include <grp.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include <sqlite3.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/utils.h"
#include "binary.h"

static struct pkg_repo_it* pkg_repo_binary_it_new(struct pkg_repo *repo,
	sqlite3_stmt *s, short flags);
static int pkg_repo_binary_it_next(struct pkg_repo_it *it, struct pkg **pkg_p, unsigned flags);
static void pkg_repo_binary_it_free(struct pkg_repo_it *it);
static void pkg_repo_binary_it_reset(struct pkg_repo_it *it);

static struct pkg_repo_it_ops pkg_repo_binary_it_ops = {
	.next = pkg_repo_binary_it_next,
	.free = pkg_repo_binary_it_free,
	.reset = pkg_repo_binary_it_reset
};

static struct pkg_repo_it*
pkg_repo_binary_it_new(struct pkg_repo *repo, sqlite3_stmt *s, short flags)
{
	struct pkg_repo_it *it;
	struct pkgdb fakedb;

	it = xmalloc(sizeof(*it));

	it->ops = &pkg_repo_binary_it_ops;
	it->flags = flags;
	it->repo = repo;

	fakedb.sqlite = PRIV_GET(repo);
	it->data = pkgdb_it_new_sqlite(&fakedb, s, PKG_REMOTE, flags);

	if (it->data == NULL) {
		free(it);
		return (NULL);
	}

	return (it);
}

static int
pkg_repo_binary_it_next(struct pkg_repo_it *it, struct pkg **pkg_p, unsigned flags)
{
	return (pkgdb_it_next(it->data, pkg_p, flags));
}

static void
pkg_repo_binary_it_free(struct pkg_repo_it *it)
{
	pkgdb_it_free(it->data);
	free(it);
}

static void
pkg_repo_binary_it_reset(struct pkg_repo_it *it)
{
	pkgdb_it_reset(it->data);
}

struct pkg_repo_it *
pkg_repo_binary_query(struct pkg_repo *repo, const char *cond, const char *pattern, match_t match)
{
	sqlite3 *sqlite = PRIV_GET(repo);
	sqlite3_stmt	*stmt = NULL;
	char *sql = NULL;
	const char	*comp = NULL;
	char basesql_quick[] = ""
		"SELECT DISTINCT p.id, origin, p.name, p.name as uniqueid, version, comment, "
		"prefix, desc, arch, maintainer, www, "
		"licenselogic, flatsize, pkgsize, "
		"cksum, manifestdigest, path AS repopath, '%s' AS dbname "
		"FROM packages  as p "
		" %s "
		"%s%s%s "
		"ORDER BY p.name;";
	char basesql[] = ""
		"WITH flavors AS "
		"  (SELECT package_id, value.annotation AS flavor FROM pkg_annotation "
		"   LEFT JOIN annotation tag ON pkg_annotation.tag_id = tag.annotation_id "
		"   LEFT JOIN annotation value ON pkg_annotation.value_id = value.annotation_id "
		"   WHERE tag.annotation = 'flavor') "

		"SELECT DISTINCT p.id, origin, p.name, p.name as uniqueid, version, comment, "
		"prefix, desc, arch, maintainer, www, "
		"licenselogic, flatsize, pkgsize, "
		"cksum, manifestdigest, path AS repopath, '%s' AS dbname "
		"FROM packages  as p "
		"LEFT JOIN pkg_categories ON p.id = pkg_categories.package_id "
		"LEFT JOIN categories ON categories.id = pkg_categories.category_id "
		"LEFT JOIN flavors ON flavors.package_id = p.id "
		" %s "
		"%s%s%s "
		"ORDER BY p.name;";
	char *bsql = basesql;

	if (match == MATCH_INTERNAL)
		bsql = basesql_quick;

	if (match != MATCH_ALL && (pattern == NULL || pattern[0] == '\0'))
		return (NULL);

	comp = pkgdb_get_pattern_query(pattern, match);
	if (comp == NULL)
		comp = "";
	if (cond == NULL)
		xasprintf(&sql, bsql, repo->name, comp, "", "", "");
	else
		xasprintf(&sql, bsql, repo->name, comp,
		    comp[0] != '\0' ? "AND (" : "WHERE ( ", cond + 7, " )");

	stmt = prepare_sql(sqlite, sql);
	free(sql);
	if (stmt == NULL)
		return (NULL);

	if (match != MATCH_ALL)
		sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
	pkg_debug(4, "Pkgdb: running '%s'", sqlite3_expanded_sql(stmt));

	return (pkg_repo_binary_it_new(repo, stmt, PKGDB_IT_FLAG_ONCE));
}

struct pkg_repo_it *
pkg_repo_binary_shlib_provide(struct pkg_repo *repo, const char *require)
{
	sqlite3_stmt	*stmt;
	sqlite3 *sqlite = PRIV_GET(repo);
	char *sql = NULL;
	const char	 basesql[] = ""
			"SELECT p.id, p.origin, p.name, p.version, p.comment, "
			"p.name as uniqueid, "
			"p.prefix, p.desc, p.arch, p.maintainer, p.www, "
			"p.licenselogic, p.flatsize, p.pkgsize, "
			"p.cksum, p.manifestdigest, p.path AS repopath, '%s' AS dbname "
			"FROM packages AS p INNER JOIN pkg_shlibs_provided AS ps ON "
			"p.id = ps.package_id "
			"WHERE ps.shlib_id IN (SELECT id FROM shlibs WHERE "
			"name BETWEEN ?1 AND ?1 || '.9');";

	xasprintf(&sql, basesql, repo->name);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	stmt = prepare_sql(sqlite, sql);
	free(sql);
	if (stmt == NULL)
		return (NULL);

	sqlite3_bind_text(stmt, 1, require, -1, SQLITE_TRANSIENT);

	return (pkg_repo_binary_it_new(repo, stmt, PKGDB_IT_FLAG_ONCE));
}

struct pkg_repo_it *
pkg_repo_binary_provide(struct pkg_repo *repo, const char *require)
{
	sqlite3_stmt	*stmt;
	sqlite3 *sqlite = PRIV_GET(repo);
	char *sql = NULL;
	const char	 basesql[] = ""
			"SELECT p.id, p.origin, p.name, p.version, p.comment, "
			"p.name as uniqueid, "
			"p.prefix, p.desc, p.arch, p.maintainer, p.www, "
			"p.licenselogic, p.flatsize, p.pkgsize, "
			"p.cksum, p.manifestdigest, p.path AS repopath, '%s' AS dbname "
			"FROM packages AS p INNER JOIN pkg_provides AS ps ON "
			"p.id = ps.package_id "
			"WHERE ps.provide_id IN (SELECT id from provides WHERE "
			"provide = ?1 );";

	xasprintf(&sql, basesql, repo->name);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	stmt = prepare_sql(sqlite, sql);
	free(sql);
	if (stmt == NULL)
		return (NULL);

	sqlite3_bind_text(stmt, 1, require, -1, SQLITE_TRANSIENT);

	return (pkg_repo_binary_it_new(repo, stmt, PKGDB_IT_FLAG_ONCE));
}

struct pkg_repo_it *
pkg_repo_binary_shlib_require(struct pkg_repo *repo, const char *provide)
{
	sqlite3_stmt	*stmt;
	sqlite3 *sqlite = PRIV_GET(repo);
	char *sql = NULL;
	const char	 basesql[] = ""
			"SELECT p.id, p.origin, p.name, p.version, p.comment, "
			"p.name as uniqueid, "
			"p.prefix, p.desc, p.arch, p.maintainer, p.www, "
			"p.licenselogic, p.flatsize, p.pkgsize, "
			"p.cksum, p.manifestdigest, p.path AS repopath, '%s' AS dbname "
			"FROM packages AS p INNER JOIN pkg_shlibs_required AS ps ON "
			"p.id = ps.package_id "
			"WHERE ps.shlib_id = (SELECT id FROM shlibs WHERE name=?1);";

	xasprintf(&sql, basesql, repo->name);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	stmt = prepare_sql(sqlite, sql);
	free(sql);
	if (stmt == NULL)
		return (NULL);

	pkg_debug(1, "> loading provides");
	sqlite3_bind_text(stmt, 1, provide, -1, SQLITE_TRANSIENT);

	return (pkg_repo_binary_it_new(repo, stmt, PKGDB_IT_FLAG_ONCE));
}

struct pkg_repo_it *
pkg_repo_binary_require(struct pkg_repo *repo, const char *provide)
{
	sqlite3_stmt	*stmt;
	sqlite3 *sqlite = PRIV_GET(repo);
	char *sql = NULL;
	const char	 basesql[] = ""
			"SELECT p.id, p.origin, p.name, p.version, p.comment, "
			"p.name as uniqueid, "
			"p.prefix, p.desc, p.arch, p.maintainer, p.www, "
			"p.licenselogic, p.flatsize, p.pkgsize, "
			"p.cksum, p.manifestdigest, p.path AS repopath, '%s' AS dbname "
			"FROM packages AS p INNER JOIN pkg_requires AS ps ON "
			"p.id = ps.package_id "
			"WHERE ps.require_id = (SELECT id FROM requires WHERE require=?1);";

	xasprintf(&sql, basesql, repo->name);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	stmt = prepare_sql(sqlite, sql);
	free(sql);
	if (stmt == NULL)
		return (NULL);

	sqlite3_bind_text(stmt, 1, provide, -1, SQLITE_TRANSIENT);

	return (pkg_repo_binary_it_new(repo, stmt, PKGDB_IT_FLAG_ONCE));
}
static const char *
pkg_repo_binary_search_how(match_t match)
{
	const char	*how = NULL;

	switch (match) {
	case MATCH_ALL:
		how = NULL;
		break;
	case MATCH_INTERNAL:
		how = "%s = ?1";
		break;
	case MATCH_EXACT:
		if (pkgdb_case_sensitive())
			how = "%s = ?1";
		else
			how = "%s = ?1 COLLATE NOCASE";
		break;
	case MATCH_GLOB:
		if (pkgdb_case_sensitive())
			how = "%s GLOB ?1";
		else
			how = "%s GLOB ?1 COLLATE NOCASE";
		break;
	case MATCH_REGEX:
		how = "%s REGEXP ?1";
		break;
	}

	return (how);
}

static int
pkg_repo_binary_build_search_query(xstring *sql, match_t match,
    pkgdb_field field, pkgdb_field sort)
{
	const char	*how = NULL;
	const char	*what = NULL;
	const char	*orderby = NULL;

	how = pkg_repo_binary_search_how(match);

	switch (field) {
	case FIELD_NONE:
		what = NULL;
		break;
	case FIELD_ORIGIN:
		what = "categories.name || substr(origin, instr(origin, '/'))";
		break;
	case FIELD_FLAVOR:
		what = "categories.name || substr(origin, instr(origin, '/')) || '@' || flavor";
		break;
	case FIELD_NAME:
		what = "p.name";
		break;
	case FIELD_NAMEVER:
		what = "p.name || '-' || version";
		break;
	case FIELD_COMMENT:
		what = "comment";
		break;
	case FIELD_DESC:
		what = "desc";
		break;
	}

	if (what != NULL && how != NULL)
		fprintf(sql->fp, how, what);

	switch (sort) {
	case FIELD_NONE:
		orderby = NULL;
		break;
	case FIELD_ORIGIN:
		orderby = " ORDER BY origin";
		break;
	case FIELD_FLAVOR:
		orderby = " ORDER BY p.name";
	case FIELD_NAME:
		orderby = " ORDER BY p.name";
		break;
	case FIELD_NAMEVER:
		orderby = " ORDER BY p.name, version";
		break;
	case FIELD_COMMENT:
		orderby = " ORDER BY comment";
		break;
	case FIELD_DESC:
		orderby = " ORDER BY desc";
		break;
	}

	if (orderby != NULL)
		fprintf(sql->fp, "%s", orderby);

	return (EPKG_OK);
}

struct pkg_repo_it *
pkg_repo_binary_search(struct pkg_repo *repo, const char *pattern, match_t match,
    pkgdb_field field, pkgdb_field sort)
{
	sqlite3 *sqlite = PRIV_GET(repo);
	sqlite3_stmt	*stmt = NULL;
	xstring	*sql = NULL;
	char *sqlcmd = NULL;
	const char	*multireposql = ""
		"WITH flavors AS "
		"  (SELECT package_id, value.annotation AS flavor FROM pkg_annotation "
		"   LEFT JOIN annotation tag ON pkg_annotation.tag_id = tag.annotation_id "
		"   LEFT JOIN annotation value ON pkg_annotation.value_id = value.annotation_id "
		"   WHERE tag.annotation = 'flavor') "
		"SELECT DISTINCT p.id, origin, p.name, version, comment, "
		"prefix, desc, arch, maintainer, www, "
		"licenselogic, flatsize, pkgsize, "
		"cksum, path AS repopath, '%1$s' AS dbname, '%2$s' AS repourl "
		"FROM packages  as p "
		"LEFT JOIN pkg_categories ON p.id = pkg_categories.package_id "
		"LEFT JOIN categories ON categories.id = pkg_categories.category_id "
		"LEFT JOIN flavors ON flavors.package_id = p.id ";

	if (pattern == NULL || pattern[0] == '\0')
		return (NULL);

	sql = xstring_new();
	fprintf(sql->fp, multireposql, repo->name, repo->url);

	/* close the UNIONs and build the search query */
	fprintf(sql->fp, "%s", "WHERE ");

	pkg_repo_binary_build_search_query(sql, match, field, sort);
	fprintf(sql->fp, "%s", ";");
	sqlcmd = xstring_get(sql);

	stmt = prepare_sql(sqlite, sqlcmd);
	free(sqlcmd);
	if (stmt == NULL)
		return (NULL);

	sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
	pkg_debug(4, "Pkgdb: running '%s'", sqlite3_expanded_sql(stmt));

	return (pkg_repo_binary_it_new(repo, stmt, PKGDB_IT_FLAG_ONCE));
}

int
pkg_repo_binary_ensure_loaded(struct pkg_repo *repo,
	struct pkg *pkg, unsigned flags)
{
	sqlite3 *sqlite = PRIV_GET(repo);
	struct pkg_manifest_key *keys = NULL;
	struct pkg *cached = NULL;
	char path[MAXPATHLEN];
	int rc;

	flags &= PKG_LOAD_FILES|PKG_LOAD_DIRS;
	/*
	 * If info is already present, done.
	 */
	if ((pkg->flags & flags) == flags) {
		return EPKG_OK;
	}
	if (pkg->type == PKG_INSTALLED) {
		pkg_emit_error("cached package %s-%s: "
			       "attempting to load info from an installed package",
			       pkg->name, pkg->version);
		return EPKG_FATAL;

		/* XXX If package is installed, get info from SQLite ???  */
		rc = pkgdb_ensure_loaded_sqlite(sqlite, pkg, flags);
		if (rc != EPKG_OK) {
			return rc;
		}
		/* probably unnecessary */
		if ((pkg->flags & flags) != flags) {
			return EPKG_FATAL;
		}
		return rc;
	}
	/*
	 * Try to get that information from fetched package in cache
	 */
	pkg_manifest_keys_new(&keys);

	if (pkg_repo_cached_name(pkg, path, sizeof(path)) != EPKG_OK)
		return (EPKG_FATAL);

	pkg_debug(1, "Binary> loading %s", path);
	if (pkg_open(&cached, path, keys, PKG_OPEN_TRY) != EPKG_OK) {
		pkg_free(cached);
		return EPKG_FATAL;
	}

	/* Now move required elements to the provided package */
	pkg_list_free(pkg, PKG_FILES);
	pkg_list_free(pkg, PKG_DIRS);
	pkg->files = cached->files;
	pkg->filehash = cached->filehash;
	pkg->dirs = cached->dirs;
	pkg->dirhash = cached->dirhash;
	cached->files = NULL;
	cached->filehash = NULL;
	cached->dirs = NULL;
	cached->dirhash = NULL;

	pkg_free(cached);
	pkg->flags |= flags;

	return EPKG_OK;
}


int64_t
pkg_repo_binary_stat(struct pkg_repo *repo, pkg_stats_t type)
{
	sqlite3 *sqlite = PRIV_GET(repo);
	sqlite3_stmt	*stmt = NULL;
	int64_t		 stats = 0;
	const char *sql = NULL;

	switch(type) {
	case PKG_STATS_LOCAL_COUNT:
	case PKG_STATS_REMOTE_REPOS:
	case PKG_STATS_LOCAL_SIZE:
		return (stats);
	case PKG_STATS_REMOTE_UNIQUE:
		sql = "SELECT COUNT(id) FROM main.packages;";
		break;
	case PKG_STATS_REMOTE_COUNT:
		sql = "SELECT COUNT(id) FROM main.packages;";
		break;
	case PKG_STATS_REMOTE_SIZE:
		sql = "SELECT SUM(pkgsize) FROM main.packages;";
		break;
	}

	pkg_debug(4, "binary_repo: running '%s'", sql);
	stmt = prepare_sql(sqlite, sql);

	if (stmt == NULL)
		return (stats);

	while (sqlite3_step(stmt) != SQLITE_DONE) {
		stats = sqlite3_column_int64(stmt, 0);
	}

	sqlite3_finalize(stmt);

	return (stats);
}
