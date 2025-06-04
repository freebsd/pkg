/*-
 * Copyright (c) 2011-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
 * Copyright (c) 2013 Gerald Pfeifer <gerald@pfeifer.com>
 * Copyright (c) 2013-2017 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2023 Serenity Cyber Security, LLC
 *                    Author: Gleb Popov <arrowd@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#define dbg(x, ...) pkg_dbg(PKG_DBG_DATABASE, x, __VA_ARGS__)

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

#include <bsd_compat.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/utils.h"
#include "private/pkg_deps.h"

/*
 * Keep entries sorted by name!
 */
static struct column_mapping {
	const char * const name;
	pkg_attr type;
	enum {
		PKG_SQLITE_STRING,
		PKG_SQLITE_INT64,
		PKG_SQLITE_BOOL
	} pkg_type;
} columns[] = {
	{ "arch",	PKG_ATTR_ABI, PKG_SQLITE_STRING },
	{ "automatic",	PKG_ATTR_AUTOMATIC, PKG_SQLITE_BOOL },
	{ "cksum",	PKG_ATTR_CKSUM, PKG_SQLITE_STRING },
	{ "comment",	PKG_ATTR_COMMENT, PKG_SQLITE_STRING },
	{ "dbname",	PKG_ATTR_REPONAME, PKG_SQLITE_STRING },
	{ "dep_formula",	PKG_ATTR_DEP_FORMULA, PKG_SQLITE_STRING },
	{ "desc",	PKG_ATTR_DESC, PKG_SQLITE_STRING },
	{ "flatsize",	PKG_ATTR_FLATSIZE, PKG_SQLITE_INT64 },
	{ "id",		PKG_ATTR_ROWID, PKG_SQLITE_INT64 },
	{ "licenselogic", PKG_ATTR_LICENSE_LOGIC, PKG_SQLITE_INT64 },
	{ "locked",	PKG_ATTR_LOCKED, PKG_SQLITE_BOOL },
	{ "maintainer",	PKG_ATTR_MAINTAINER, PKG_SQLITE_STRING },
	{ "manifestdigest",	PKG_ATTR_DIGEST, PKG_SQLITE_STRING },
	{ "message",	PKG_ATTR_MESSAGE, PKG_SQLITE_STRING },
	{ "name",	PKG_ATTR_NAME, PKG_SQLITE_STRING },
	{ "oldflatsize", PKG_ATTR_OLD_FLATSIZE, PKG_SQLITE_INT64 },
	{ "oldversion",	PKG_ATTR_OLD_VERSION, PKG_SQLITE_STRING },
	{ "origin",	PKG_ATTR_ORIGIN, PKG_SQLITE_STRING },
	{ "pkgsize",	PKG_ATTR_PKGSIZE, PKG_SQLITE_INT64 },
	{ "prefix",	PKG_ATTR_PREFIX, PKG_SQLITE_STRING },
	{ "repopath",	PKG_ATTR_REPOPATH, PKG_SQLITE_STRING },
	{ "repourl",	PKG_ATTR_REPOURL, PKG_SQLITE_STRING },
	{ "rowid",	PKG_ATTR_ROWID, PKG_SQLITE_INT64 },
	{ "time",	PKG_ATTR_TIME, PKG_SQLITE_INT64 },
	{ "uniqueid",	PKG_ATTR_UNIQUEID, PKG_SQLITE_STRING },
	{ "version",	PKG_ATTR_VERSION, PKG_SQLITE_STRING },
	{ "vital",	PKG_ATTR_VITAL, PKG_SQLITE_BOOL },
	{ "weight",	-1, PKG_SQLITE_INT64 },
	{ "www",	PKG_ATTR_WWW, PKG_SQLITE_STRING },
	{ NULL,		-1, PKG_SQLITE_STRING }
};

static void
remote_free(struct pkg_repo_it *it)
{
	it->ops->free(it);
}

static int
pkg_addcategory(struct pkg *pkg, const char *data)
{
	return (pkg_addstring(&pkg->categories, data, "category"));
}

static int
pkg_addlicense(struct pkg *pkg, const char *data)
{
	return (pkg_addstring(&pkg->licenses, data, "license"));
}

static int
pkg_addannotation(struct pkg *pkg, const char *k, const char *v)
{
	return (pkg_kv_add(&pkg->annotations, k, v, "annotation"));
}

static int
load_val(sqlite3 *db, struct pkg *pkg, const char *sql, unsigned flags,
    int (*pkg_adddata)(struct pkg *pkg, const char *data), int list)
{
	sqlite3_stmt	*stmt;
	int		 ret;

	assert(db != NULL && pkg != NULL);

	if (pkg->flags & flags)
		return (EPKG_OK);

	stmt = prepare_sql(db, sql);
	if (stmt == NULL)
		return (EPKG_FATAL);

	sqlite3_bind_int64(stmt, 1, pkg->id);
	pkgdb_debug(4, stmt);
	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_adddata(pkg, sqlite3_column_text(stmt, 0));
	}

	if (ret != SQLITE_DONE) {
		if (list != -1)
			pkg_list_free(pkg, list);
		ERROR_STMT_SQLITE(db, stmt);
		sqlite3_finalize(stmt);
		return (EPKG_FATAL);
	}
	sqlite3_finalize(stmt);

	pkg->flags |= flags;
	return (EPKG_OK);
}

static int
load_tag_val(sqlite3 *db, struct pkg *pkg, const char *sql, unsigned flags,
	     int (*pkg_addtagval)(struct pkg *pkg, const char *tag, const char *val),
	     int list)
{
	sqlite3_stmt	*stmt;
	int		 ret;

	assert(db != NULL && pkg != NULL);

	if (pkg->flags & flags)
		return (EPKG_OK);

	stmt = prepare_sql(db, sql);
	if (stmt == NULL)
		return (EPKG_FATAL);

	sqlite3_bind_int64(stmt, 1, pkg->id);
	pkgdb_debug(4, stmt);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addtagval(pkg, sqlite3_column_text(stmt, 0),
			      sqlite3_column_text(stmt, 1));
	}

	if (ret != SQLITE_DONE) {
		if (list != -1)
			pkg_list_free(pkg, list);
		ERROR_STMT_SQLITE(db, stmt);
		sqlite3_finalize(stmt);
		return (EPKG_FATAL);
	}
	sqlite3_finalize(stmt);

	pkg->flags |= flags;
	return (EPKG_OK);
}

static int
compare_column_func(const void *pkey, const void *pcolumn)
{
	const char *key = (const char*)pkey;
	const struct column_mapping *column =
			(const struct column_mapping*)pcolumn;

	return strcmp(key, column->name);
}

static int
pkgdb_load_deps(sqlite3 *sqlite, struct pkg *pkg)
{
	sqlite3_stmt	*stmt = NULL, *opt_stmt = NULL;
	int		 ret = EPKG_OK;
	struct pkg_dep *chain = NULL;
	struct pkg_dep_formula *f;
	struct pkg_dep_formula_item *fit;
	struct pkg_dep_option_item *optit;
	bool options_match;
	char *formula_sql, *clause;
	const char	 sql[] = ""
		"SELECT DISTINCT d.name, d.origin, p.version, 0"
		"  FROM deps AS d"
		"    LEFT JOIN packages AS p ON"
		"    (p.origin = d.origin AND p.name = d.name)"
		"  WHERE d.package_id = ?1"
		"  ORDER BY d.origin DESC";
	const char formula_preamble[] = ""
		"SELECT id,name,origin,version,locked FROM packages WHERE ";
	const char options_sql[] = ""
		"SELECT option, value"
		"  FROM option"
		"    JOIN pkg_option USING(option_id)"
		"  WHERE package_id = ?1"
		"  ORDER BY option";

	assert(pkg != NULL);

	if (pkg->flags & PKG_LOAD_DEPS)
		return (EPKG_OK);


	stmt = prepare_sql(sqlite, sql);
	if (stmt == NULL)
		return (EPKG_FATAL);

	sqlite3_bind_int64(stmt, 1, pkg->id);
	pkgdb_debug(4, stmt);

	/* XXX: why we used locked here ? */
	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_adddep(pkg, sqlite3_column_text(stmt, 0),
			   sqlite3_column_text(stmt, 1),
			   sqlite3_column_text(stmt, 2),
			   sqlite3_column_int64(stmt, 3));
	}

	if (ret != SQLITE_DONE) {
		pkg_list_free(pkg, PKG_DEPS);
		ERROR_STMT_SQLITE(sqlite, stmt);
		sqlite3_finalize(stmt);
		return (EPKG_FATAL);
	}
	sqlite3_finalize(stmt);

	if (pkg->dep_formula) {
		dbg(4, "Pkgdb: reading package formula '%s'", pkg->dep_formula);

		f = pkg_deps_parse_formula (pkg->dep_formula);

		if (f != NULL) {
			DL_FOREACH(f->items, fit) {
				clause = pkg_deps_formula_tosql(fit);

				if (clause) {
					xasprintf(&formula_sql, "%s%s", formula_preamble, clause);
					stmt = prepare_sql(sqlite, formula_sql);
					if (stmt == NULL) {
						free(clause);
						free(formula_sql);
						pkg_deps_formula_free(f);
						return (EPKG_FATAL);
					}
					pkgdb_debug(4, stmt);

					/* Fetch matching packages */
					chain = NULL;

					while (sqlite3_step(stmt) == SQLITE_ROW) {
						/*
						 * Load options for a package and check
						 * if they are compatible
						 */
						options_match = true;

						if (fit->options) {
							opt_stmt = prepare_sql(sqlite, options_sql);
							if (opt_stmt == NULL) {
								sqlite3_finalize(stmt);
								free(clause);
								free(formula_sql);
								pkg_deps_formula_free(f);
								return (EPKG_FATAL);
							}
							pkgdb_debug(4, opt_stmt);

							sqlite3_bind_int64(opt_stmt, 1,
									sqlite3_column_int64(stmt, 0));

							while (sqlite3_step(opt_stmt) == SQLITE_ROW) {
								DL_FOREACH(fit->options, optit) {
									if(STREQ(optit->opt, sqlite3_column_text(opt_stmt, 0))) {
										if ((!STREQ(sqlite3_column_text(opt_stmt, 1), "on") && !optit->on)
											|| (!STREQ(sqlite3_column_text(opt_stmt, 1), "off") && optit->on)) {
											dbg(4, "incompatible option for"
													"%s: %s",
													sqlite3_column_text(opt_stmt, 1),
													optit->opt);
											options_match = false;
											break;
										}
									}
								}
							}

							sqlite3_finalize(opt_stmt);
						}

						if (options_match) {
							chain = pkg_adddep_chain(chain, pkg,
									sqlite3_column_text(stmt, 1),
									sqlite3_column_text(stmt, 2),
									sqlite3_column_text(stmt, 3),
									sqlite3_column_int64(stmt, 4));
						}
					}

					free(clause);
					free(formula_sql);
					sqlite3_finalize(stmt);
				}

			}

			pkg_deps_formula_free(f);
		}
	}

	pkg->flags |= PKG_LOAD_DEPS;
	return (EPKG_OK);
}

static int
pkgdb_load_rdeps(sqlite3 *sqlite, struct pkg *pkg)
{
	sqlite3_stmt	*stmt = NULL;
	int		 ret;
	const char	 sql[] = ""
		"SELECT p.name, p.origin, p.version, 0"
		"  FROM packages AS p"
		"    INNER JOIN deps AS d ON (p.id = d.package_id)"
		"  WHERE d.name = ?1";

	assert(pkg != NULL);

	if (pkg->flags & PKG_LOAD_RDEPS)
		return (EPKG_OK);

	stmt = prepare_sql(sqlite, sql);
	if (stmt == NULL)
		return (EPKG_FATAL);

	sqlite3_bind_text(stmt, 1, pkg->uid, -1, SQLITE_STATIC);
	pkgdb_debug(4, stmt);

	/* XXX: why we used locked here ? */
	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addrdep(pkg, sqlite3_column_text(stmt, 0),
			    sqlite3_column_text(stmt, 1),
			    sqlite3_column_text(stmt, 2),
			    sqlite3_column_int64(stmt, 3));
	}

	if (ret != SQLITE_DONE) {
		pkg_list_free(pkg, PKG_RDEPS);
		ERROR_STMT_SQLITE(sqlite, stmt);
		sqlite3_finalize(stmt);
		return (EPKG_FATAL);
	}
	sqlite3_finalize(stmt);

	pkg->flags |= PKG_LOAD_RDEPS;
	return (EPKG_OK);
}

static int
pkgdb_load_files(sqlite3 *sqlite, struct pkg *pkg)
{
	sqlite3_stmt	*stmt = NULL;
	int		 ret;
	const char	 sql[] = ""
		"SELECT path, sha256"
		"  FROM files"
		"  WHERE package_id = ?1"
		"  ORDER BY PATH ASC";
	const char	 sql2[] = ""
		"SELECT path, content"
		"  FROM config_files"
		"  WHERE package_id = ?1"
		"  ORDER BY PATH ASC";

	assert( pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_FILES)
		return (EPKG_OK);

	stmt = prepare_sql(sqlite, sql);
	if (stmt == NULL)
		return (EPKG_FATAL);

	sqlite3_bind_int64(stmt, 1, pkg->id);
	pkgdb_debug(4, stmt);

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		pkg_addfile(pkg, sqlite3_column_text(stmt, 0),
		    sqlite3_column_text(stmt, 1), false);
	}
	sqlite3_finalize(stmt);

	stmt = prepare_sql(sqlite, sql2);
	if (stmt == NULL)
		return (EPKG_FATAL);

	sqlite3_bind_int64(stmt, 1, pkg->id);
	pkgdb_debug(4, stmt);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addconfig_file(pkg, sqlite3_column_text(stmt, 0),
		    sqlite3_column_text(stmt, 1));
	}

	if (ret != SQLITE_DONE) {
		pkg_list_free(pkg, PKG_FILES);
		ERROR_STMT_SQLITE(sqlite, stmt);
		sqlite3_finalize(stmt);
		return (EPKG_FATAL);
	}
	sqlite3_finalize(stmt);

	pkg->flags |= PKG_LOAD_FILES;
	return (EPKG_OK);
}

static int
pkgdb_load_dirs(sqlite3 *sqlite, struct pkg *pkg)
{
	const char	 sql[] = ""
		"SELECT path, try"
		"  FROM pkg_directories, directories"
		"  WHERE package_id = ?1"
		"    AND directory_id = directories.id"
		"  ORDER by path DESC";
	sqlite3_stmt	*stmt;
	int		 ret;

	assert(pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_DIRS)
		return (EPKG_OK);

	stmt = prepare_sql(sqlite, sql);
	if (stmt == NULL)
		return (EPKG_FATAL);

	sqlite3_bind_int64(stmt, 1, pkg->id);
	pkgdb_debug(4, stmt);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_adddir(pkg, sqlite3_column_text(stmt, 0), false);
	}

	if (ret != SQLITE_DONE) {
		pkg_list_free(pkg, PKG_DIRS);
		ERROR_STMT_SQLITE(sqlite, stmt);
		sqlite3_finalize(stmt);
		return (EPKG_FATAL);
	}
	sqlite3_finalize(stmt);

	pkg->flags |= PKG_LOAD_DIRS;

	return (EPKG_OK);
}

static int
pkgdb_load_license(sqlite3 *sqlite, struct pkg *pkg)
{
	const char	 sql[] = ""
		"SELECT ifnull(group_concat(name, ', '), '') AS name"
		"  FROM pkg_licenses, licenses AS l"
		"  WHERE package_id = ?1"
		"    AND license_id = l.id"
		"  ORDER by name DESC";

	assert(pkg != NULL);

	return (load_val(sqlite, pkg, sql, PKG_LOAD_LICENSES,
	    pkg_addlicense, PKG_ATTR_LICENSES));
}

static int
pkgdb_load_category(sqlite3 *sqlite, struct pkg *pkg)
{
	const char	 sql[] = ""
		"SELECT name"
		"  FROM pkg_categories, categories AS c"
		"  WHERE package_id = ?1"
		"    AND category_id = c.id"
		"  ORDER by name DESC";

	assert(pkg != NULL);

	return (load_val(sqlite, pkg, sql, PKG_LOAD_CATEGORIES,
	    pkg_addcategory, PKG_ATTR_CATEGORIES));
}

static int
pkgdb_load_user(sqlite3 *sqlite, struct pkg *pkg)
{
	int		ret;
	const char	sql[] = ""
		"SELECT users.name"
		"  FROM pkg_users, users"
		"  WHERE package_id = ?1"
		"    AND user_id = users.id"
		"  ORDER by name DESC";

	assert(pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	ret = load_val(sqlite, pkg, sql, PKG_LOAD_USERS,
	    pkg_adduser, PKG_ATTR_USERS);

	return (ret);
}

static int
pkgdb_load_group(sqlite3 *sqlite, struct pkg *pkg)
{
	int			 ret;
	const char		 sql[] = ""
		"SELECT groups.name"
		"  FROM pkg_groups, groups"
		"  WHERE package_id = ?1"
		"    AND group_id = groups.id"
		"  ORDER by name DESC";

	assert(pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	ret = load_val(sqlite, pkg, sql, PKG_LOAD_GROUPS,
	    pkg_addgroup, PKG_ATTR_GROUPS);

	return (ret);
}

static int
addshlib_required_raw(struct pkg *pkg, const char *name)
{
	vec_push(&pkg->shlibs_required, xstrdup(name));
	return (EPKG_OK);
}

static int
pkgdb_load_shlib_required(sqlite3 *sqlite, struct pkg *pkg)
{
	const char	sql[] = ""
		"SELECT name"
		"  FROM pkg_shlibs_required, shlibs AS s"
		"  WHERE package_id = ?1"
		"    AND shlib_id = s.id"
		"  ORDER by name ASC";

	assert(pkg != NULL);

	return (load_val(sqlite, pkg, sql, PKG_LOAD_SHLIBS_REQUIRED,
	    addshlib_required_raw, PKG_ATTR_SHLIBS_REQUIRED));
}

static int
addshlib_provided_raw(struct pkg *pkg, const char *name)
{
	vec_push(&pkg->shlibs_provided, xstrdup(name));
	return (EPKG_OK);
}

static int
pkgdb_load_shlib_provided(sqlite3 *sqlite, struct pkg *pkg)
{
	const char	sql[] = ""
		"SELECT name"
		"  FROM pkg_shlibs_provided, shlibs AS s"
		"  WHERE package_id = ?1"
		"    AND shlib_id = s.id"
		"  ORDER by name ASC";

	assert(pkg != NULL);

	return (load_val(sqlite, pkg, sql, PKG_LOAD_SHLIBS_PROVIDED,
	    addshlib_provided_raw, PKG_SHLIBS_PROVIDED));
}

static int
pkgdb_load_annotations(sqlite3 *sqlite, struct pkg *pkg)
{
	const char	sql[] = ""
		"SELECT k.annotation AS tag, v.annotation AS value"
		"  FROM pkg_annotation p"
		"    JOIN annotation k ON (p.tag_id = k.annotation_id)"
		"    JOIN annotation v ON (p.value_id = v.annotation_id)"
		"  WHERE p.package_id = ?1"
		"  ORDER BY tag, value";

	return (load_tag_val(sqlite, pkg, sql, PKG_LOAD_ANNOTATIONS,
		   pkg_addannotation, PKG_ATTR_ANNOTATIONS));
}

static int
pkgdb_load_lua_scripts(sqlite3 *sqlite, struct pkg *pkg)
{
	sqlite3_stmt	*stmt = NULL;
	int		ret;
	const char	sql[] = ""
		"SELECT lua_script, type"
		"  FROM lua_script"
		"    JOIN pkg_lua_script USING(lua_script_id)"
		"  WHERE package_id = ?1";

	assert(pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_LUA_SCRIPTS)
		return (EPKG_OK);

	stmt = prepare_sql(sqlite, sql);
	if (stmt == NULL)
		return (EPKG_FATAL);

	sqlite3_bind_int64(stmt, 1, pkg->id);

	pkgdb_debug(4, stmt);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_add_lua_script(pkg, sqlite3_column_text(stmt, 0),
		    sqlite3_column_int64(stmt, 1));
	}

	if (ret != SQLITE_DONE) {
		ERROR_STMT_SQLITE(sqlite, stmt);
		sqlite3_finalize(stmt);
		return (EPKG_FATAL);
	}
	sqlite3_finalize(stmt);

	pkg->flags |= PKG_LOAD_LUA_SCRIPTS;
	return (EPKG_OK);
}

static int
pkgdb_load_scripts(sqlite3 *sqlite, struct pkg *pkg)
{
	sqlite3_stmt	*stmt = NULL;
	int		 ret;
	const char	 sql[] = ""
		"SELECT script, type"
		"  FROM pkg_script"
		"    JOIN script USING(script_id)"
		"  WHERE package_id = ?1";

	assert(pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_SCRIPTS)
		return (EPKG_OK);

	stmt = prepare_sql(sqlite, sql);
	if (stmt == NULL)
		return (EPKG_FATAL);

	sqlite3_bind_int64(stmt, 1, pkg->id);
	pkgdb_debug(4, stmt);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addscript(pkg, sqlite3_column_text(stmt, 0),
		    sqlite3_column_int64(stmt, 1));
	}

	if (ret != SQLITE_DONE) {
		ERROR_STMT_SQLITE(sqlite, stmt);
		sqlite3_finalize(stmt);
		return (EPKG_FATAL);
	}
	sqlite3_finalize(stmt);

	pkg->flags |= PKG_LOAD_SCRIPTS;
	return (EPKG_OK);
}


static int
pkgdb_load_options(sqlite3 *sqlite, struct pkg *pkg)
{
	unsigned int	 i;

	struct optionsql {
		const char	 *sql;
		int		(*pkg_addtagval)(struct pkg *pkg,
						  const char *tag,
						  const char *val);
	}			  optionsql[] = {
		{
			"SELECT option, value"
			"  FROM option"
			"    JOIN pkg_option USING(option_id)"
			"  WHERE package_id = ?1"
			"  ORDER BY option",
			pkg_addoption,
		},
		{
			"SELECT option, default_value"
			"  FROM option"
			"    JOIN pkg_option_default USING(option_id)"
			"  WHERE package_id = ?1"
			"  ORDER BY option",
			pkg_addoption_default,
		},
		{
			"SELECT option, description"
			"  FROM option"
			"    JOIN pkg_option_desc USING(option_id)"
			"    JOIN option_desc USING(option_desc_id)"
			"  WHERE package_id = ?1"
			"  ORDER BY option",
			pkg_addoption_description,
		}
	};
	const char		 *opt_sql;
	int			(*pkg_addtagval)(struct pkg *pkg,
						 const char *tag,
						 const char *val);
	int			  ret;

	assert(pkg != NULL);

	if (pkg->flags & PKG_LOAD_OPTIONS)
		return (EPKG_OK);


	for (i = 0; i < NELEM(optionsql); i++) {
		opt_sql       = optionsql[i].sql;
		pkg_addtagval = optionsql[i].pkg_addtagval;

		dbg(4, "adding option");
		ret = load_tag_val(sqlite, pkg, opt_sql, PKG_LOAD_OPTIONS,
				   pkg_addtagval, PKG_OPTIONS);
		if (ret != EPKG_OK)
			break;
	}
	return (ret);
}

static int
pkgdb_load_conflicts(sqlite3 *sqlite, struct pkg *pkg)
{
	const char	sql[] = ""
		"SELECT packages.name"
		"  FROM pkg_conflicts"
		"    LEFT JOIN packages ON"
		"    (packages.id = pkg_conflicts.conflict_id)"
		"  WHERE package_id = ?1";

	assert(pkg != NULL);

	return (load_val(sqlite, pkg, sql, PKG_LOAD_CONFLICTS,
			pkg_addconflict, PKG_ATTR_CONFLICTS));
}

static int
pkgdb_load_provides(sqlite3 *sqlite, struct pkg *pkg)
{
	const char	sql[] = ""
		"SELECT provide"
		"  FROM pkg_provides, provides AS s"
		"  WHERE package_id = ?1"
		"    AND provide_id = s.id"
		"  ORDER by provide DESC";

	assert(pkg != NULL);

	return (load_val(sqlite, pkg, sql, PKG_LOAD_PROVIDES,
	    pkg_addprovide, PKG_ATTR_PROVIDES));
}

static int
pkgdb_load_requires(sqlite3 *sqlite, struct pkg *pkg)
{
	const char	sql[] = ""
		"SELECT require"
		"  FROM pkg_requires, requires AS s"
		"  WHERE package_id = ?1"
		"    AND require_id = s.id"
		"  ORDER by require DESC";

	assert(pkg != NULL);

	return (load_val(sqlite, pkg, sql, PKG_LOAD_REQUIRES,
	    pkg_addrequire, PKG_REQUIRES));
}

static void
populate_pkg(sqlite3_stmt *stmt, struct pkg *pkg) {
	int		 icol = 0;
	const char	*colname, *msg;
	char		 legacyarch[BUFSIZ];

	assert(stmt != NULL);

	for (icol = 0; icol < sqlite3_column_count(stmt); icol++) {
		colname = sqlite3_column_name(stmt, icol);
		struct column_mapping *column;
		switch (sqlite3_column_type(stmt, icol)) {
		case SQLITE_TEXT:
			column = bsearch(colname, columns,
					 NELEM(columns) - 1,
					 sizeof(columns[0]),
					 compare_column_func);
			if (column == NULL) {
				pkg_emit_error("unknown column %s", colname);
				continue;
			}

			switch (column->type) {
			case PKG_ATTR_ABI:
				free(pkg->abi);
				pkg->abi = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			case PKG_ATTR_CKSUM:
				free(pkg->sum);
				pkg->sum = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			case PKG_ATTR_COMMENT:
				free(pkg->comment);
				pkg->comment = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			case PKG_ATTR_REPONAME:
				free(pkg->reponame);
				pkg->reponame = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			case PKG_ATTR_DESC:
				free(pkg->desc);
				pkg->desc = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			case PKG_ATTR_MAINTAINER:
				free(pkg->maintainer);
				pkg->maintainer = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			case PKG_ATTR_DIGEST:
				free(pkg->digest);
				pkg->digest = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			case PKG_ATTR_MESSAGE:
				msg = sqlite3_column_text(stmt, icol);
				if (msg) {
					/* A stupid logic to detect legacy pkg message */
					if (msg[0] == '[') {
						pkg_message_from_str(pkg, msg, 0);
					}
					else {
						struct pkg_message *message;
						message = xcalloc(1, sizeof(*message));
						message->str = xstrdup(msg);
						vec_push(&pkg->message, message);
					}
				}
				break;
			case PKG_ATTR_NAME:
				free(pkg->name);
				pkg->name = xstrdup(sqlite3_column_text(stmt, icol));
				free(pkg->uid);
				pkg->uid = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			case PKG_ATTR_OLD_VERSION:
				free(pkg->old_version);
				pkg->old_version = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			case PKG_ATTR_ORIGIN:
				free(pkg->origin);
				pkg->origin = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			case PKG_ATTR_PREFIX:
				free(pkg->prefix);
				pkg->prefix = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			case PKG_ATTR_REPOPATH:
				free(pkg->repopath);
				pkg->repopath = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			case PKG_ATTR_REPOURL:
				free(pkg->repourl);
				pkg->repourl = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			case PKG_ATTR_UNIQUEID:
				free(pkg->uid);
				pkg->uid = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			case PKG_ATTR_VERSION:
				free(pkg->version);
				pkg->version = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			case PKG_ATTR_WWW:
				free(pkg->www);
				pkg->www = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			case PKG_ATTR_DEP_FORMULA:
				free(pkg->dep_formula);
				pkg->dep_formula = xstrdup(sqlite3_column_text(stmt, icol));
				break;
			default:
				pkg_emit_error("Unexpected text value for %s", colname);
				break;
			}
			break;
		case SQLITE_INTEGER:
			column = bsearch(colname, columns,
					 NELEM(columns) - 1,
					 sizeof(columns[0]),
					 compare_column_func);
			if (column == NULL) {
				pkg_emit_error("Unknown column %s", colname);
				continue;
			}

			switch (column->type) {
			case PKG_ATTR_AUTOMATIC:
				pkg->automatic = (bool)sqlite3_column_int64(stmt, icol);
				break;
			case PKG_ATTR_LOCKED:
				pkg->locked = (bool)sqlite3_column_int64(stmt, icol);
				break;
			case PKG_ATTR_FLATSIZE:
				pkg->flatsize = sqlite3_column_int64(stmt, icol);
				break;
			case PKG_ATTR_ROWID:
				pkg->id = sqlite3_column_int64(stmt, icol);
				break;
			case PKG_ATTR_LICENSE_LOGIC:
				pkg->licenselogic = (lic_t)sqlite3_column_int64(stmt, icol);
				break;
			case PKG_ATTR_OLD_FLATSIZE:
				pkg->old_flatsize = sqlite3_column_int64(stmt, icol);
				break;
			case PKG_ATTR_PKGSIZE:
				pkg->pkgsize = sqlite3_column_int64(stmt, icol);
				break;
			case PKG_ATTR_VITAL:
				pkg->vital = (bool)sqlite3_column_int64(stmt, icol);
				break;
			case PKG_ATTR_TIME:
				pkg->timestamp = sqlite3_column_int64(stmt, icol);
				break;
			default:
				pkg_emit_error("Unexpected integer value for %s", colname);
				break;
			}
			break;
		case SQLITE_BLOB:
		case SQLITE_FLOAT:
			pkg_emit_error("wrong type for column: %s",
			    colname);
			/* just ignore currently */
			break;
		case SQLITE_NULL:
			break;
		}
	}

	pkg_arch_to_legacy(pkg->abi, legacyarch, BUFSIZ);
	pkg->altabi = xstrdup(legacyarch);
}

static struct load_on_flag {
	int	flag;
	int	(*load)(sqlite3 *sqlite, struct pkg *p);
} load_on_flag[] = {
	{ PKG_LOAD_DEPS,		pkgdb_load_deps },
	{ PKG_LOAD_RDEPS,		pkgdb_load_rdeps },
	{ PKG_LOAD_FILES,		pkgdb_load_files },
	{ PKG_LOAD_DIRS,		pkgdb_load_dirs },
	{ PKG_LOAD_SCRIPTS,		pkgdb_load_scripts },
	{ PKG_LOAD_OPTIONS,		pkgdb_load_options },
	{ PKG_LOAD_CATEGORIES,		pkgdb_load_category },
	{ PKG_LOAD_LICENSES,		pkgdb_load_license },
	{ PKG_LOAD_USERS,		pkgdb_load_user },
	{ PKG_LOAD_GROUPS,		pkgdb_load_group },
	{ PKG_LOAD_SHLIBS_REQUIRED,	pkgdb_load_shlib_required },
	{ PKG_LOAD_SHLIBS_PROVIDED,	pkgdb_load_shlib_provided },
	{ PKG_LOAD_ANNOTATIONS,		pkgdb_load_annotations },
	{ PKG_LOAD_CONFLICTS,		pkgdb_load_conflicts },
	{ PKG_LOAD_PROVIDES,		pkgdb_load_provides },
	{ PKG_LOAD_REQUIRES,		pkgdb_load_requires },
	{ PKG_LOAD_LUA_SCRIPTS,		pkgdb_load_lua_scripts },
	{ -1,			        NULL }
};

static void
pkgdb_sqlite_it_reset(struct pkgdb_sqlite_it *it)
{
	if (it == NULL)
		return;

	it->finished = 0;
	sqlite3_reset(it->stmt);
}

static void
pkgdb_sqlite_it_free(struct pkgdb_sqlite_it *it)
{
	if (it == NULL)
		return;

	sqlite3_finalize(it->stmt);
}

static int
pkgdb_sqlite_it_next(struct pkgdb_sqlite_it *it,
	struct pkg **pkg_p, unsigned flags)
{
	struct pkg	*pkg;
	int		 i;
	int		 ret;

	assert(it != NULL);

	/*
	 * XXX:
	 * Currently, we have a lot of issues related to pkg digests.
	 * So we want to ensure that we always have a valid package digest
	 * even if we work with pkg 1.2 repo. Therefore, we explicitly check
	 * manifest digests and set it to NULL if it is invalid.
	 *
	 */

	if (it->finished && (it->flags & PKGDB_IT_FLAG_ONCE))
		return (EPKG_END);

	switch (sqlite3_step(it->stmt)) {
	case SQLITE_ROW:
		pkg_free(*pkg_p);
		ret = pkg_new(pkg_p, it->pkg_type);
		if (ret != EPKG_OK)
			return (ret);
		pkg = *pkg_p;

		populate_pkg(it->stmt, pkg);

		if (pkg->digest != NULL && !pkg_checksum_is_valid(pkg->digest, strlen(pkg->digest))) {
			free(pkg->digest);
			pkg->digest = NULL;
		}

		for (i = 0; load_on_flag[i].load != NULL; i++) {
			if (flags & load_on_flag[i].flag) {
				if (it->sqlite != NULL) {
					ret = load_on_flag[i].load(it->sqlite, pkg);
					if (ret != EPKG_OK)
						return (ret);
				}
				else {
					pkg_emit_error("invalid iterator passed to pkgdb_it_next");
					return (EPKG_FATAL);
				}
			}
		}

		return (EPKG_OK);
	case SQLITE_DONE:
		it->finished ++;
		if (it->flags & PKGDB_IT_FLAG_CYCLED) {
			sqlite3_reset(it->stmt);
			return (EPKG_OK);
		}
		else {
			if (it->flags & PKGDB_IT_FLAG_AUTO)
				pkgdb_sqlite_it_free(it);
			return (EPKG_END);
		}
		break;
	default:
		ERROR_SQLITE(it->sqlite, "iterator");
		return (EPKG_FATAL);
	}
}

int
pkgdb_it_next(struct pkgdb_it *it, struct pkg **pkg_p, unsigned flags)
{
	struct pkg_repo_it *rit;
	int ret = EPKG_END;

	assert(it != NULL);

	if (it->local != NULL && !it->local->finished) {
		int r = pkgdb_sqlite_it_next(it->local, pkg_p, flags);
		if ( r != EPKG_END)
			return (r);
	}

	if (vec_len(&it->remote) != 0) {
		if (it->remote_pos >= it->remote.len)
			it->remote_pos = 0;
		struct pkg_repo_it *rit = it->remote.d[it->remote_pos];
		ret = rit->ops->next(rit, pkg_p, flags);
		if (ret != EPKG_OK) {
			if (it->remote_pos == it->remote.len -1 )
				return (EPKG_END);
			it->remote_pos++;
			return (pkgdb_it_next(it, pkg_p, flags));
		}

		if (*pkg_p != NULL)
			(*pkg_p)->repo = rit->repo;

		return (EPKG_OK);
	}

	return ret;
}

// TODO: Why doesn't this function handle remote?
int
pkgdb_it_count(struct pkgdb_it *it)
{
	int		 	i;
	int		 	ret;
	struct pkgdb_sqlite_it *sit;

	assert(it != NULL);

	i = 0;
	sit = it->local;

	if (sit == NULL)
		return (0);

	while ((ret = sqlite3_step(sit->stmt))) {
		switch (ret) {
		case SQLITE_ROW:
			++i;
			break;
		case SQLITE_DONE:
			goto done;
		default:
			ERROR_SQLITE(sit->sqlite, "iterator");
			return (-1);
		}
	}

done:
	pkgdb_it_reset(it);
	return (i);
}

void
pkgdb_it_reset(struct pkgdb_it *it)
{
	assert(it != NULL);

	if (it->local != NULL) {
		pkgdb_sqlite_it_reset(it->local);
	}
	vec_foreach(it->remote, i) {
		it->remote.d[i]->ops->reset(it->remote.d[i]);
	}
}

void
pkgdb_it_free(struct pkgdb_it *it)
{
	if (it == NULL)
		return;

	if (it->local != NULL) {
		pkgdb_sqlite_it_free(it->local);
		free(it->local);
	}
	vec_free_and_free(&it->remote, remote_free);

	free(it);
}

struct pkgdb_it *
pkgdb_it_new_sqlite(struct pkgdb *db, sqlite3_stmt *s, int type, short flags)
{
	struct pkgdb_it	*it;

	assert(db != NULL && s != NULL);
	assert(!(flags & (PKGDB_IT_FLAG_CYCLED & PKGDB_IT_FLAG_ONCE)));
	assert(!(flags & (PKGDB_IT_FLAG_AUTO & (PKGDB_IT_FLAG_CYCLED | PKGDB_IT_FLAG_ONCE))));

	it = xcalloc(1, sizeof(struct pkgdb_it));

	it->db = db;
	it->local = xmalloc(sizeof(struct pkgdb_sqlite_it));
	it->local->sqlite = db->sqlite;
	it->local->stmt = s;
	it->local->pkg_type = type;

	it->local->flags = flags;
	it->local->finished = 0;
	it->remote_pos = 0;

	return (it);
}

struct pkgdb_it *
pkgdb_it_new_repo(struct pkgdb *db)
{
	struct pkgdb_it	*it;

	it = xcalloc(1, sizeof(struct pkgdb_it));

	it->db = db;

	return (it);
}

void
pkgdb_it_repo_attach(struct pkgdb_it *it, struct pkg_repo_it *rit)
{
	vec_push(&it->remote, rit);
}

int
pkgdb_ensure_loaded_sqlite(sqlite3 *sqlite, struct pkg *pkg, unsigned flags)
{
	int i, ret;

	for (i = 0; load_on_flag[i].load != NULL; i++) {
		if (~pkg->flags & flags & load_on_flag[i].flag) {
			ret = load_on_flag[i].load(sqlite, pkg);
			if (ret != EPKG_OK)
				return (ret);
			pkg->flags |= load_on_flag[i].flag;
		}
	}

	return (EPKG_OK);
}

int
pkgdb_ensure_loaded(struct pkgdb *db, struct pkg *pkg, unsigned flags)
{
	if (pkg->type == PKG_INSTALLED)
		return (pkgdb_ensure_loaded_sqlite(db->sqlite, pkg, flags));

	vec_foreach(db->repos, i) {
		if (db->repos.d[i] == pkg->repo) {
			if (db->repos.d[i]->ops->ensure_loaded) {
				return (db->repos.d[i]->ops->ensure_loaded(db->repos.d[i],
				    pkg, flags));
			}
		}
	}

	return (EPKG_FATAL);
}
