/*-
 * Copyright (c) 2011-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
 * Copyright (c) 2013 Gerald Pfeifer <gerald@pfeifer.com>
 * Copyright (c) 2013-2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2023 Serenity Cyber Security, LLC
 *                    Author: Gleb Popov <arrowd@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "pkg/vec.h"
#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

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

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/utils.h"

const char *
pkgdb_get_pattern_query(const char *pattern, match_t match)
{
	char		*checkorigin = NULL;
	char		*checkflavor = NULL;
	const char	*comp = NULL;

	if (pattern != NULL) {
		checkorigin = strchr(pattern, '/');
		if (checkorigin != NULL)
			checkflavor = strchr(checkorigin, '@');
	}

	switch (match) {
	case MATCH_ALL:
		comp = "";
		break;
	case MATCH_INTERNAL:
		comp = " WHERE p.name = ?1";
		break;
	case MATCH_EXACT:
		if (pkgdb_case_sensitive()) {
			if (checkorigin == NULL)
				comp = " WHERE (p.name = ?1 OR p.name || '-' || version = ?1)";
			else if (checkflavor == NULL)
				comp = " WHERE (origin = ?1 OR categories.name || substr(origin, instr(origin, '/')) = ?1)";
			else
				comp = "WHERE (categories.name || substr(origin, instr(origin, '/')) || '@' || flavor = ?1)";
		} else {
			if (checkorigin == NULL)
				comp = " WHERE (p.name = ?1 COLLATE NOCASE OR "
				"p.name || '-' || version = ?1 COLLATE NOCASE)";
			else if (checkflavor == NULL)
				comp = " WHERE (origin = ?1 COLLATE NOCASE OR categories.name || substr(origin, instr(origin, '/'))  = ?1 COLLATE NOCASE)";
			else
				comp = "WHERE (categories.name || substr(origin, instr(origin, '/')) || '@' || flavor = ?1 COLLATE NOCASE)";
		}
		break;
	case MATCH_GLOB:
		if (pkgdb_case_sensitive()) {
			if (checkorigin == NULL)
				comp = " WHERE (p.name GLOB ?1 "
					"OR p.name || '-' || version GLOB ?1)";
			else if (checkflavor == NULL)
				comp = " WHERE (origin GLOB ?1 OR categories.name || substr(origin, instr(origin, '/')) GLOB ?1)";
			else
				comp = "WHERE (categories.name || substr(origin, instr(origin, '/')) || '@' || flavor GLOB ?1)";
		} else  {
			if (checkorigin == NULL)
				comp = " WHERE (lower(p.name) GLOB lower(?1)  "
					"OR lower(p.name || '-' || version) GLOB lower(?1) )";
			else if (checkflavor == NULL)
				comp = " WHERE (lower(origin) GLOB lower(?1) OR lower(categories.name || substr(origin, instr(origin, '/'))) GLOB lower(?1))";
			else
				comp = "WHERE (lower(categories.name || substr(origin, instr(origin, '/')) || '@' || flavor) GLOB lower(?1))";
		}
		break;
	case MATCH_REGEX:
		if (checkorigin == NULL)
			comp = " WHERE (p.name REGEXP ?1 "
			    "OR p.name || '-' || version REGEXP ?1)";
		else if (checkflavor == NULL)
			comp = " WHERE (origin REGEXP ?1 OR categories.name || substr(origin, instr(origin, '/')) REGEXP ?1)";
		else
			comp = "WHERE (categories.name || substr(origin, instr(origin, '/')) || '@' || flavor REGEXP ?1)";
		break;
	}

	return (comp);
}

struct pkgdb_it *
pkgdb_query_cond(struct pkgdb *db, const char *cond, const char *pattern, match_t match)
{
	char		 sql[BUFSIZ];
	sqlite3_stmt	*stmt;
	const char	*comp = NULL;

	assert(db != NULL);

	if (match != MATCH_ALL && (pattern == NULL || pattern[0] == '\0'))
		return (NULL);

	comp = pkgdb_get_pattern_query(pattern, match);

	if (cond) {
		sqlite3_snprintf(sizeof(sql), sql,
				"WITH flavors AS "
				"  (SELECT package_id, value.annotation AS flavor FROM pkg_annotation "
				"   LEFT JOIN annotation tag ON pkg_annotation.tag_id = tag.annotation_id "
				"   LEFT JOIN annotation value ON pkg_annotation.value_id = value.annotation_id "
				"   WHERE tag.annotation = 'flavor') "
				"SELECT DISTINCT(p.id), origin, p.name, p.name as uniqueid, "
				"   version, comment, desc, "
				"   message, arch, maintainer, www, "
				"   prefix, flatsize, licenselogic, automatic, "
				"   locked, time, manifestdigest, vital "
				"   FROM packages AS p "
				"   LEFT JOIN pkg_categories ON p.id = pkg_categories.package_id "
				"   LEFT JOIN categories ON categories.id = pkg_categories.category_id "
				"   LEFT JOIN flavors ON flavors.package_id = p.id "
				"    %s %s (%s) ORDER BY p.name;",
					comp, pattern == NULL ? "WHERE" : "AND", cond + 7);
	} else if (match == MATCH_INTERNAL) {
		sqlite3_snprintf(sizeof(sql), sql,
				"SELECT DISTINCT(p.id), origin, p.name, p.name as uniqueid, "
					"version, comment, desc, "
					"message, arch, maintainer, www, "
					"prefix, flatsize, licenselogic, automatic, "
					"locked, time, manifestdigest, vital "
				"FROM packages AS p "
				"%s"
				" ORDER BY p.name", comp);
	} else {
		sqlite3_snprintf(sizeof(sql), sql,
				"WITH flavors AS "
				"  (SELECT package_id, value.annotation AS flavor FROM pkg_annotation "
				"   LEFT JOIN annotation tag ON pkg_annotation.tag_id = tag.annotation_id "
				"   LEFT JOIN annotation value ON pkg_annotation.value_id = value.annotation_id "
				"   WHERE tag.annotation = 'flavor') "
				"SELECT DISTINCT(p.id), origin, p.name, p.name as uniqueid, "
					"version, comment, desc, "
					"message, arch, maintainer, www, "
					"prefix, flatsize, licenselogic, automatic, "
					"locked, time, manifestdigest, vital "
				"FROM packages AS p "
				"LEFT JOIN pkg_categories ON p.id = pkg_categories.package_id "
				"LEFT JOIN categories ON categories.id = pkg_categories.category_id "
				"LEFT JOIN flavors ON flavors.package_id = p.id "
				"%s"
				" ORDER BY p.name", comp);
	}

	if ((stmt = prepare_sql(db->sqlite, sql)) == NULL)
		return (NULL);

	if (match != MATCH_ALL)
		sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
	pkgdb_debug(4, stmt);

	return (pkgdb_it_new_sqlite(db, stmt, PKG_INSTALLED, PKGDB_IT_FLAG_ONCE));
}

struct pkgdb_it *
pkgdb_query(struct pkgdb *db, const char *pattern, match_t match)
{
	return pkgdb_query_cond(db, NULL, pattern, match);
}

bool
pkgdb_file_exists(struct pkgdb *db, const char *path)
{
	sqlite3_stmt	*stmt;
	char	sql[BUFSIZ];
	bool	ret = false;

	assert(db != NULL);

	if (path == NULL)
		return (false);

	sqlite3_snprintf(sizeof(sql), sql,
	    "select path from files where path = ?1;");

	if ((stmt = prepare_sql(db->sqlite, sql)) == NULL)
		return (ret);

	sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
	pkgdb_debug(4, stmt);

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		ret = true;
	}

	sqlite3_finalize(stmt);
	return (ret);
}

struct pkgdb_it *
pkgdb_query_which(struct pkgdb *db, const char *path, bool glob)
{
	sqlite3_stmt	*stmt;
	char	sql[BUFSIZ];

	assert(db != NULL);

	if (path == NULL)
		return (NULL);

	sqlite3_snprintf(sizeof(sql), sql,
			"SELECT p.id, p.origin, p.name, p.name as uniqueid, "
			"p.version, p.comment, p.desc, "
			"p.message, p.arch, p.maintainer, p.www, "
			"p.prefix, p.flatsize, p.time "
			"FROM packages AS p "
			"LEFT JOIN files AS f ON p.id = f.package_id "
			"WHERE f.path %s ?1 GROUP BY p.id;", glob ? "GLOB" : "=");

	if ((stmt = prepare_sql(db->sqlite, sql)) == NULL)
		return (NULL);

	sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
	pkgdb_debug(4, stmt);

	return (pkgdb_it_new_sqlite(db, stmt, PKG_INSTALLED, PKGDB_IT_FLAG_ONCE));
}

struct pkgdb_it *
pkgdb_query_shlib_require(struct pkgdb *db, const char *shlib)
{
	sqlite3_stmt	*stmt;
	const char	 sql[] = ""
		"SELECT p.id, p.origin, p.name, p.name as uniqueid, "
			"p.version, p.comment, p.desc, "
			"p.message, p.arch, p.maintainer, p.www, "
			"p.prefix, p.flatsize, p.time "
			"FROM packages AS p, pkg_shlibs_required AS ps, shlibs AS s "
			"WHERE p.id = ps.package_id "
				"AND ps.shlib_id = s.id "
				"AND s.name = ?1;";

	assert(db != NULL);

	if ((stmt = prepare_sql(db->sqlite, sql)) == NULL)
		return (NULL);

	sqlite3_bind_text(stmt, 1, shlib, -1, SQLITE_TRANSIENT);
	pkgdb_debug(4, stmt);

	return (pkgdb_it_new_sqlite(db, stmt, PKG_INSTALLED, PKGDB_IT_FLAG_ONCE));
}

struct pkgdb_it *
pkgdb_query_shlib_provide(struct pkgdb *db, const char *shlib)
{
	sqlite3_stmt	*stmt;
	const char	 sql[] = ""
		"SELECT p.id, p.origin, p.name, p.name as uniqueid, "
			"p.version, p.comment, p.desc, "
			"p.message, p.arch, p.maintainer, p.www, "
			"p.prefix, p.flatsize, p.manifestdigest, p.time "
			"FROM packages AS p, pkg_shlibs_provided AS ps, shlibs AS s "
			"WHERE p.id = ps.package_id "
				"AND ps.shlib_id = s.id "
				"AND s.name = ?1;";

	assert(db != NULL);

	if ((stmt = prepare_sql(db->sqlite, sql)) == NULL)
		return (NULL);

	sqlite3_bind_text(stmt, 1, shlib, -1, SQLITE_TRANSIENT);
	pkgdb_debug(4, stmt);

	return (pkgdb_it_new_sqlite(db, stmt, PKG_INSTALLED, PKGDB_IT_FLAG_ONCE));
}

struct pkgdb_it *
pkgdb_query_require(struct pkgdb *db, const char *req)
{
	sqlite3_stmt	*stmt;
	const char	 sql[] = ""
		"SELECT p.id, p.origin, p.name, p.name as uniqueid, "
			"p.version, p.comment, p.desc, "
			"p.message, p.arch, p.maintainer, p.www, "
			"p.prefix, p.flatsize, p.time "
			"FROM packages AS p, pkg_requires AS ps, requires AS s "
			"WHERE p.id = ps.package_id "
				"AND ps.require_id = s.id "
				"AND s.require = ?1;";

	assert(db != NULL);

	if ((stmt = prepare_sql(db->sqlite, sql)) == NULL)
		return (NULL);

	sqlite3_bind_text(stmt, 1, req, -1, SQLITE_TRANSIENT);
	pkgdb_debug(4, stmt);

	return (pkgdb_it_new_sqlite(db, stmt, PKG_INSTALLED, PKGDB_IT_FLAG_ONCE));
}

struct pkgdb_it *
pkgdb_query_provide(struct pkgdb *db, const char *req)
{
	sqlite3_stmt	*stmt;
	const char	 sql[] = ""
		"SELECT p.id, p.origin, p.name, p.name as uniqueid, "
			"p.version, p.comment, p.desc, "
			"p.message, p.arch, p.maintainer, p.www, "
			"p.prefix, p.flatsize, p.time "
			"FROM packages AS p, pkg_provides AS ps, provides AS s "
			"WHERE p.id = ps.package_id "
				"AND ps.provide_id = s.id "
				"AND s.provide = ?1;";

	assert(db != NULL);

	if ((stmt = prepare_sql(db->sqlite, sql)) == NULL)
		return (NULL);

	sqlite3_bind_text(stmt, 1, req, -1, SQLITE_TRANSIENT);
	pkgdb_debug(4, stmt);

	return (pkgdb_it_new_sqlite(db, stmt, PKG_INSTALLED, PKGDB_IT_FLAG_ONCE));
}

static bool
consider_this_repo(c_charv_t *repos, const char *name)
{
	/* All repositories */
	if (repos == NULL)
		return (true);

	if (repos->len == 0)
		return (true);

	return (c_charv_contains(repos, name, true));
}

struct pkgdb_it *
pkgdb_repo_query_cond(struct pkgdb *db, const char *cond, const char *pattern, match_t match,
    const char *repo)
{
	c_charv_t r = vec_init();
	struct pkgdb_it *ret;

	if (repo != NULL)
		vec_push(&r, repo);

	ret = pkgdb_repo_query_cond2(db, cond, pattern, match, &r);
	vec_free(&r);

	return (ret);
}

struct pkgdb_it *
pkgdb_repo_query_cond2(struct pkgdb *db, const char *cond, const char *pattern, match_t match,
    c_charv_t *repos)
{
	struct pkgdb_it *it;
	struct pkg_repo_it *rit;

	it = pkgdb_it_new_repo(db);
	if (it == NULL)
		return (NULL);

	vec_foreach(db->repos, i) {
		if (consider_this_repo(repos, db->repos.d[i]->name)) {
			if (pattern != NULL && *pattern == '@')
				rit = db->repos.d[i]->ops->groupquery(db->repos.d[i], pattern + 1, match);
			else
				rit = db->repos.d[i]->ops->query(db->repos.d[i], cond, pattern, match);
			if (rit != NULL)
				pkgdb_it_repo_attach(it, rit);
		}
	}

	return (it);
}

struct pkgdb_it *pkgdb_repo_query(struct pkgdb *db, const char *pattern,
	match_t match, const char *repo)
{
	return pkgdb_repo_query_cond(db, NULL, pattern, match, repo);
}

struct pkgdb_it *pkgdb_repo_query2(struct pkgdb *db, const char *pattern,
	match_t match, c_charv_t *repos)
{
	return pkgdb_repo_query_cond2(db, NULL, pattern, match, repos);
}

struct pkgdb_it *
pkgdb_repo_shlib_require(struct pkgdb *db, const char *require, c_charv_t *repos)
{
	struct pkgdb_it *it;
	struct pkg_repo_it *rit;

	it = pkgdb_it_new_repo(db);
	if (it == NULL)
		return (NULL);

	vec_foreach(db->repos, i) {
		if (consider_this_repo(repos, db->repos.d[i]->name)) {
			if (db->repos.d[i]->ops->shlib_required != NULL) {
				rit = db->repos.d[i]->ops->shlib_required(db->repos.d[i], require);
				if (rit != NULL)
					pkgdb_it_repo_attach(it, rit);
			}
		}
	}

	return (it);
}

struct pkgdb_it *
pkgdb_repo_shlib_provide(struct pkgdb *db, const char *require, c_charv_t *repos)
{
	struct pkgdb_it *it;
	struct pkg_repo_it *rit;

	it = pkgdb_it_new_repo(db);
	if (it == NULL)
		return (NULL);

	vec_foreach(db->repos, i) {
		if (consider_this_repo(repos, db->repos.d[i]->name)) {
			if (db->repos.d[i]->ops->shlib_required != NULL) {
				rit = db->repos.d[i]->ops->shlib_provided(db->repos.d[i], require);
				if (rit != NULL)
					pkgdb_it_repo_attach(it, rit);
			}
		}
	}

	return (it);
}

struct pkgdb_it *
pkgdb_repo_require(struct pkgdb *db, const char *require, c_charv_t *repo)
{
	struct pkgdb_it *it;
	struct pkg_repo_it *rit;

	it = pkgdb_it_new_repo(db);
	if (it == NULL)
		return (NULL);

	vec_foreach(db->repos, i) {
		if (consider_this_repo(repo, db->repos.d[i]->name)) {
			if (db->repos.d[i]->ops->required != NULL) {
				rit = db->repos.d[i]->ops->required(db->repos.d[i], require);
				if (rit != NULL)
					pkgdb_it_repo_attach(it, rit);
			}
		}
	}

	return (it);
}

struct pkgdb_it *
pkgdb_repo_provide(struct pkgdb *db, const char *require, c_charv_t *repo)
{
	struct pkgdb_it *it;
	struct pkg_repo_it *rit;

	it = pkgdb_it_new_repo(db);
	if (it == NULL)
		return (NULL);

	vec_foreach(db->repos, i) {
		if (consider_this_repo(repo, db->repos.d[i]->name)) {
			if (db->repos.d[i]->ops->required != NULL) {
				rit = db->repos.d[i]->ops->provided(db->repos.d[i], require);
				if (rit != NULL)
					pkgdb_it_repo_attach(it, rit);
			}
		}
	}

	return (it);
}

struct pkgdb_it *
pkgdb_repo_search(struct pkgdb *db, const char *pattern, match_t match,
    pkgdb_field field, pkgdb_field sort, const char *repo)
{
	c_charv_t r = vec_init();
	struct pkgdb_it *ret;

	if (repo != NULL)
		vec_push(&r, repo);

	ret = pkgdb_repo_search2(db, pattern, match, field, sort, &r);
	vec_free(&r);

	return (ret);
}

struct pkgdb_it *
pkgdb_repo_search2(struct pkgdb *db, const char *pattern, match_t match,
    pkgdb_field field, pkgdb_field sort, c_charv_t *repos)
{
	struct pkgdb_it *it;
	struct pkg_repo_it *rit;

	it = pkgdb_it_new_repo(db);
	if (it == NULL)
		return (NULL);

	vec_foreach(db->repos, i) {
		if (consider_this_repo(repos, db->repos.d[i]->name)) {
			if (db->repos.d[i]->ops->search != NULL) {
				rit = db->repos.d[i]->ops->search(db->repos.d[i], pattern, match,
					field, sort);
				if (rit != NULL)
					pkgdb_it_repo_attach(it, rit);
			}
			if (db->repos.d[i]->ops->groupsearch != NULL) {
				rit = db->repos.d[i]->ops->groupsearch(db->repos.d[i], pattern, match, field);
				if (rit != NULL)
					pkgdb_it_repo_attach(it, rit);
			}
		}
	}

	return (it);
}

struct pkgdb_it *
pkgdb_all_search(struct pkgdb *db, const char *pattern, match_t match,
    pkgdb_field field, pkgdb_field sort, const char *repo)
{
	c_charv_t r = vec_init();
	struct pkgdb_it *ret;

	if (repo != NULL)
		vec_push(&r, repo);

	ret = pkgdb_all_search2(db, pattern, match, field, sort, &r);

	vec_free(&r);

	return (ret);
}

struct pkgdb_it *
pkgdb_all_search2(struct pkgdb *db, const char *pattern, match_t match,
    pkgdb_field field, pkgdb_field sort, c_charv_t *repos)
{
	struct pkgdb_it *it;
	struct pkg_repo_it *rit;


	it = pkgdb_query(db, pattern, match);

	vec_foreach(db->repos, i) {
		if (consider_this_repo(repos, db->repos.d[i]->name)) {
			if (db->repos.d[i]->ops->search != NULL) {
				rit = db->repos.d[i]->ops->search(db->repos.d[i], pattern, match,
					field, sort);
				if (rit != NULL)
					pkgdb_it_repo_attach(it, rit);
			}
		}
	}

	return (it);
}
