/*-
 * Copyright (c) 2011-2022 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
 * Copyright (c) 2013 Gerald Pfeifer <gerald@pfeifer.com>
 * Copyright (c) 2013-2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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
	char		*checkuid = NULL;
	char		*checkflavor = NULL;
	const char	*comp = NULL;

	if (pattern != NULL) {
		checkuid = strchr(pattern, '~');
		if (checkuid == NULL)
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
			if (checkuid == NULL) {
				if (checkorigin == NULL)
					comp = " WHERE (p.name = ?1 OR p.name || '-' || version = ?1)";
				else if (checkflavor == NULL)
					comp = " WHERE (origin = ?1 OR categories.name || substr(origin, instr(origin, '/')) = ?1)";
				else
					comp = "WHERE (categories.name || substr(origin, instr(origin, '/')) || '@' || flavor = ?1)";
			} else {
				comp = " WHERE p.name = ?1";
			}
		} else {
			if (checkuid == NULL) {
				if (checkorigin == NULL)
					comp = " WHERE (p.name = ?1 COLLATE NOCASE OR "
					"p.name || '-' || version = ?1 COLLATE NOCASE)";
				else if (checkflavor == NULL)
					comp = " WHERE (origin = ?1 COLLATE NOCASE OR categories.name || substr(origin, instr(origin, '/'))  = ?1 COLLATE NOCASE)";
				else
					comp = "WHERE (categories.name || substr(origin, instr(origin, '/')) || '@' || flavor = ?1 COLLATE NOCASE)";
			} else {
				comp = " WHERE p.name = ?1 COLLATE NOCASE";
			}
		}
		break;
	case MATCH_GLOB:
		if (pkgdb_case_sensitive()) {
			if (checkuid == NULL) {
				if (checkorigin == NULL)
					comp = " WHERE (p.name GLOB ?1 "
						"OR p.name || '-' || version GLOB ?1)";
				else if (checkflavor == NULL)
					comp = " WHERE (origin GLOB ?1 OR categories.name || substr(origin, instr(origin, '/')) GLOB ?1)";
				else
					comp = "WHERE (categories.name || substr(origin, instr(origin, '/')) || '@' || flavor GLOB ?1)";
			} else {
				comp = " WHERE p.name GLOB ?1";
			}
		} else  {
			if (checkuid == NULL) {
				if (checkorigin == NULL)
					comp = " WHERE (p.name GLOB ?1 COLLATE NOCASE "
						"OR p.name || '-' || version GLOB ?1 COLLATE NOCASE)";
				else if (checkflavor == NULL)
					comp = " WHERE (origin GLOB ?1 COLLATE NOCASE OR categories.name || substr(origin, instr(origin, '/')) GLOB ?1 COLLATE NOCASE)";
				else
					comp = "WHERE (categories.name || substr(origin, instr(origin, '/')) || '@' || flavor GLOB ?1 COLLATE NOCASE)";
			} else {
				comp = " WHERE p.name GLOB ?1 COLLATE NOCASE";
			}
		}
		break;
	case MATCH_REGEX:
		if (checkuid == NULL) {
			if (checkorigin == NULL)
				comp = " WHERE (p.name REGEXP ?1 "
				    "OR p.name || '-' || version REGEXP ?1)";
			else if (checkflavor == NULL)
				comp = " WHERE (origin REGEXP ?1 OR categories.name || substr(origin, instr(origin, '/')) REGEXP ?1)";
			else
				comp = "WHERE (categories.name || substr(origin, instr(origin, '/')) || '@' || flavor REGEXP ?1)";
		} else {
			comp = " WHERE p.name REGEXP ?1";
		}
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
				"SELECT DISTINCT p.id, origin, p.name, p.name as uniqueid, "
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
				"SELECT DISTINCT p.id, origin, p.name, p.name as uniqueid, "
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
				"SELECT DISTINCT p.id, origin, p.name, p.name as uniqueid, "
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

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sql);
		return (NULL);
	}

	if (match != MATCH_ALL)
		sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
	pkg_debug(4, "Pkgdb: running '%s'", sqlite3_expanded_sql(stmt));

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
	pkg_debug(4, "Pkgdb: running '%s'", sql);

	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sql);
	}

	sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);

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

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sql);
		return (NULL);
	}

	sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);

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

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sql);
		return (NULL);
	}

	sqlite3_bind_text(stmt, 1, shlib, -1, SQLITE_TRANSIENT);

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

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sql);
		return (NULL);
	}

	sqlite3_bind_text(stmt, 1, shlib, -1, SQLITE_TRANSIENT);

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

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sql);
		return (NULL);
	}

	sqlite3_bind_text(stmt, 1, req, -1, SQLITE_TRANSIENT);

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

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sql);
		return (NULL);
	}

	sqlite3_bind_text(stmt, 1, req, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new_sqlite(db, stmt, PKG_INSTALLED, PKGDB_IT_FLAG_ONCE));
}

struct pkgdb_it *
pkgdb_repo_query_cond(struct pkgdb *db, const char *cond, const char *pattern, match_t match,
    const char *repo)
{
	struct pkgdb_it *it;
	struct pkg_repo_it *rit;

	it = pkgdb_it_new_repo(db);
	if (it == NULL)
		return (NULL);

	tll_foreach(db->repos, cur) {
		if (repo == NULL || strcasecmp(cur->item->name, repo) == 0) {
			rit = cur->item->ops->query(cur->item, cond, pattern, match);
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

struct pkgdb_it *
pkgdb_repo_shlib_require(struct pkgdb *db, const char *require, const char *repo)
{
	struct pkgdb_it *it;
	struct pkg_repo_it *rit;

	it = pkgdb_it_new_repo(db);
	if (it == NULL)
		return (NULL);

	tll_foreach(db->repos, cur) {
		if (repo == NULL || strcasecmp(cur->item->name, repo) == 0) {
			if (cur->item->ops->shlib_required != NULL) {
				rit = cur->item->ops->shlib_required(cur->item, require);
				if (rit != NULL)
					pkgdb_it_repo_attach(it, rit);
			}
		}
	}

	return (it);
}

struct pkgdb_it *
pkgdb_repo_shlib_provide(struct pkgdb *db, const char *require, const char *repo)
{
	struct pkgdb_it *it;
	struct pkg_repo_it *rit;

	it = pkgdb_it_new_repo(db);
	if (it == NULL)
		return (NULL);

	tll_foreach(db->repos, cur) {
		if (repo == NULL || strcasecmp(cur->item->name, repo) == 0) {
			if (cur->item->ops->shlib_required != NULL) {
				rit = cur->item->ops->shlib_provided(cur->item, require);
				if (rit != NULL)
					pkgdb_it_repo_attach(it, rit);
			}
		}
	}

	return (it);
}

struct pkgdb_it *
pkgdb_repo_require(struct pkgdb *db, const char *require, const char *repo)
{
	struct pkgdb_it *it;
	struct pkg_repo_it *rit;

	it = pkgdb_it_new_repo(db);
	if (it == NULL)
		return (NULL);

	tll_foreach(db->repos, cur) {
		if (repo == NULL || strcasecmp(cur->item->name, repo) == 0) {
			if (cur->item->ops->required != NULL) {
				rit = cur->item->ops->required(cur->item, require);
				if (rit != NULL)
					pkgdb_it_repo_attach(it, rit);
			}
		}
	}

	return (it);
}

struct pkgdb_it *
pkgdb_repo_provide(struct pkgdb *db, const char *require, const char *repo)
{
	struct pkgdb_it *it;
	struct pkg_repo_it *rit;

	it = pkgdb_it_new_repo(db);
	if (it == NULL)
		return (NULL);

	tll_foreach(db->repos, cur) {
		if (repo == NULL || strcasecmp(cur->item->name, repo) == 0) {
			if (cur->item->ops->required != NULL) {
				rit = cur->item->ops->provided(cur->item, require);
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
	struct pkgdb_it *it;
	struct pkg_repo_it *rit;

	it = pkgdb_it_new_repo(db);
	if (it == NULL)
		return (NULL);

	tll_foreach(db->repos, cur) {
		if (repo == NULL || strcasecmp(cur->item->name, repo) == 0) {
			if (cur->item->ops->search != NULL) {
				rit = cur->item->ops->search(cur->item, pattern, match,
					field, sort);
				if (rit != NULL)
					pkgdb_it_repo_attach(it, rit);
			}
		}
	}

	return (it);
}
