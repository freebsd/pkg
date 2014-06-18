/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <assert.h>
#include <errno.h>
#include <regex.h>
#include <grp.h>
#include <libutil.h>
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
	{ "arch",	PKG_ARCH, PKG_SQLITE_STRING },
	{ "automatic",	PKG_AUTOMATIC, PKG_SQLITE_BOOL },
	{ "cksum",	PKG_CKSUM, PKG_SQLITE_STRING },
	{ "comment",	PKG_COMMENT, PKG_SQLITE_STRING },
	{ "dbname",	PKG_REPONAME, PKG_SQLITE_STRING },
	{ "desc",	PKG_DESC, PKG_SQLITE_STRING },
	{ "flatsize",	PKG_FLATSIZE, PKG_SQLITE_INT64 },
	{ "id",		PKG_ROWID, PKG_SQLITE_INT64 },
	{ "licenselogic", PKG_LICENSE_LOGIC, PKG_SQLITE_INT64 },
	{ "locked",	PKG_LOCKED, PKG_SQLITE_BOOL },
	{ "maintainer",	PKG_MAINTAINER, PKG_SQLITE_STRING },
	{ "manifestdigest",	PKG_DIGEST, PKG_SQLITE_STRING },
	{ "message",	PKG_MESSAGE, PKG_SQLITE_STRING },
	{ "name",	PKG_NAME, PKG_SQLITE_STRING },
	{ "oldflatsize", PKG_OLD_FLATSIZE, PKG_SQLITE_INT64 },
	{ "oldversion",	PKG_OLD_VERSION, PKG_SQLITE_STRING },
	{ "origin",	PKG_ORIGIN, PKG_SQLITE_STRING },
	{ "pkgsize",	PKG_PKGSIZE, PKG_SQLITE_INT64 },
	{ "prefix",	PKG_PREFIX, PKG_SQLITE_STRING },
	{ "repopath",	PKG_REPOPATH, PKG_SQLITE_STRING },
	{ "rowid",	PKG_ROWID, PKG_SQLITE_INT64 },
	{ "time",	PKG_TIME, PKG_SQLITE_INT64 },
	{ "uniqueid",	PKG_UNIQUEID, PKG_SQLITE_STRING },
	{ "version",	PKG_VERSION, PKG_SQLITE_STRING },
	{ "weight",	-1, PKG_SQLITE_INT64 },
	{ "www",	PKG_WWW, PKG_SQLITE_STRING },
	{ NULL,		-1, PKG_SQLITE_STRING }
};

static int
load_val(sqlite3 *db, struct pkg *pkg, const char *sql, unsigned flags,
    int (*pkg_adddata)(struct pkg *pkg, const char *data), int list)
{
	sqlite3_stmt	*stmt;
	int		 ret;
	int64_t		 rowid;

	assert(db != NULL && pkg != NULL);

	if (pkg->flags & flags)
		return (EPKG_OK);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db, sql);
		return (EPKG_FATAL);
	}

	pkg_get(pkg, PKG_ROWID, &rowid);
	sqlite3_bind_int64(stmt, 1, rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_adddata(pkg, sqlite3_column_text(stmt, 0));
	}

	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		if (list != -1)
			pkg_list_free(pkg, list);
		ERROR_SQLITE(db, sql);
		return (EPKG_FATAL);
	}

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
	int64_t		 rowid;

	assert(db != NULL && pkg != NULL);

	if (pkg->flags & flags)
		return (EPKG_OK);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(db, sql);
		return (EPKG_FATAL);
	}

	pkg_get(pkg, PKG_ROWID, &rowid);
	sqlite3_bind_int64(stmt, 1, rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addtagval(pkg, sqlite3_column_text(stmt, 0),
			      sqlite3_column_text(stmt, 1));
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		if (list != -1)
			pkg_list_free(pkg, list);
		ERROR_SQLITE(db, sql);
		return (EPKG_FATAL);
	}

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
	sqlite3_stmt	*stmt = NULL;
	int		 ret = EPKG_OK;
	int64_t		 rowid;
	char		 sql[BUFSIZ];
	const char	*mainsql = ""
		"SELECT d.name, d.origin, d.version, 0 "
		"FROM main.deps AS d "
		"LEFT JOIN main.packages AS p ON p.origin = d.origin "
		"AND p.name = d.name "
		"WHERE d.package_id = ?1 ORDER BY d.origin DESC;";

	assert(pkg != NULL);

	if (pkg->flags & PKG_LOAD_DEPS)
		return (EPKG_OK);


	pkg_debug(4, "Pkgdb: running '%s'", mainsql);
	ret = sqlite3_prepare_v2(sqlite, mainsql, -1, &stmt, NULL);

	if (ret != SQLITE_OK) {
		ERROR_SQLITE(sqlite, sql);
		return (EPKG_FATAL);
	}

	pkg_get(pkg, PKG_ROWID, &rowid);
	sqlite3_bind_int64(stmt, 1, rowid);

	/* XXX: why we used locked here ? */
	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_adddep(pkg, sqlite3_column_text(stmt, 0),
			   sqlite3_column_text(stmt, 1),
			   sqlite3_column_text(stmt, 2),
			   sqlite3_column_int(stmt, 3));
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		pkg_list_free(pkg, PKG_DEPS);
		ERROR_SQLITE(sqlite, sql);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_DEPS;
	return (EPKG_OK);
}

static int
pkgdb_load_rdeps(sqlite3 *sqlite, struct pkg *pkg)
{
	sqlite3_stmt	*stmt = NULL;
	int		 ret;
	const char	*uniqueid;
	const char	*mainsql = ""
		"SELECT p.name, p.origin, p.version, 0 "
		"FROM main.packages AS p "
		"INNER JOIN main.deps AS d ON p.id = d.package_id "
		"WHERE d.name = SPLIT_UID('name', ?1) AND "
		"d.origin = SPLIT_UID('origin', ?1);";

	assert(pkg != NULL);

	if (pkg->flags & PKG_LOAD_RDEPS)
		return (EPKG_OK);


	pkg_debug(4, "Pkgdb: running '%s'", mainsql);
	ret = sqlite3_prepare_v2(sqlite, mainsql, -1, &stmt, NULL);

	if (ret != SQLITE_OK) {
		ERROR_SQLITE(sqlite, mainsql);
		return (EPKG_FATAL);
	}

	pkg_get(pkg, PKG_UNIQUEID, &uniqueid);
	sqlite3_bind_text(stmt, 1, uniqueid, -1, SQLITE_STATIC);

	/* XXX: why we used locked here ? */
	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addrdep(pkg, sqlite3_column_text(stmt, 0),
			    sqlite3_column_text(stmt, 1),
			    sqlite3_column_text(stmt, 2),
			    sqlite3_column_int(stmt, 3));
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		pkg_list_free(pkg, PKG_RDEPS);
		ERROR_SQLITE(sqlite, mainsql);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_RDEPS;
	return (EPKG_OK);
}

static int
pkgdb_load_files(sqlite3 *sqlite, struct pkg *pkg)
{
	sqlite3_stmt	*stmt = NULL;
	int		 ret;
	int64_t		 rowid;
	const char	 sql[] = ""
		"SELECT path, sha256 "
		"FROM files "
		"WHERE package_id = ?1 "
		"ORDER BY PATH ASC";

	assert( pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_FILES)
		return (EPKG_OK);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite, sql);
		return (EPKG_FATAL);
	}

	pkg_get(pkg, PKG_ROWID, &rowid);
	sqlite3_bind_int64(stmt, 1, rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addfile(pkg, sqlite3_column_text(stmt, 0),
		    sqlite3_column_text(stmt, 1), false);
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		pkg_list_free(pkg, PKG_FILES);
		ERROR_SQLITE(sqlite, sql);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_FILES;
	return (EPKG_OK);
}

static int
pkgdb_load_dirs(sqlite3 *sqlite, struct pkg *pkg)
{
	const char	 sql[] = ""
		"SELECT path, try "
		"FROM pkg_directories, directories "
		"WHERE package_id = ?1 "
		"AND directory_id = directories.id "
		"ORDER by path DESC";
	sqlite3_stmt	*stmt;
	int		 ret;
	int64_t		 rowid;

	assert(pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_DIRS)
		return (EPKG_OK);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite, sql);
		return (EPKG_FATAL);
	}

	pkg_get(pkg, PKG_ROWID, &rowid);
	sqlite3_bind_int64(stmt, 1, rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_adddir(pkg, sqlite3_column_text(stmt, 0),
		    sqlite3_column_int(stmt, 1), false);
	}

	sqlite3_finalize(stmt);
	if (ret != SQLITE_DONE) {
		pkg_list_free(pkg, PKG_DIRS);
		ERROR_SQLITE(sqlite, sql);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_DIRS;

	return (EPKG_OK);
}

static int
pkgdb_load_license(sqlite3 *sqlite, struct pkg *pkg)
{
	char		 sql[BUFSIZ];
	const char	*basesql = ""
		"SELECT name "
		"FROM %Q.pkg_licenses, %Q.licenses AS l "
		"WHERE package_id = ?1 "
			"AND license_id = l.id "
		"ORDER by name DESC";

	assert(pkg != NULL);

	sqlite3_snprintf(sizeof(sql), sql, basesql, "main", "main");

	return (load_val(sqlite, pkg, sql, PKG_LOAD_LICENSES,
	    pkg_addlicense, PKG_LICENSES));
}

static int
pkgdb_load_category(sqlite3 *sqlite, struct pkg *pkg)
{
	char		 sql[BUFSIZ];
	const char	*basesql = ""
		"SELECT name "
		"FROM %Q.pkg_categories, %Q.categories AS c "
		"WHERE package_id = ?1 "
			"AND category_id = c.id "
		"ORDER by name DESC";

	assert(pkg != NULL);

	sqlite3_snprintf(sizeof(sql), sql, basesql, "main", "main");

	return (load_val(sqlite, pkg, sql, PKG_LOAD_CATEGORIES,
	    pkg_addcategory, PKG_CATEGORIES));
}

static int
pkgdb_load_user(sqlite3 *sqlite, struct pkg *pkg)
{
	/*struct pkg_user *u = NULL;
	struct passwd *pwd = NULL;*/
	int		ret;
	const char	sql[] = ""
		"SELECT users.name "
		"FROM pkg_users, users "
		"WHERE package_id = ?1 "
			"AND user_id = users.id "
		"ORDER by name DESC";

	assert(pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	ret = load_val(sqlite, pkg, sql, PKG_LOAD_USERS,
	    pkg_adduser, PKG_USERS);

	/* TODO get user uidstr from local database */
/*	while (pkg_users(pkg, &u) == EPKG_OK) {
		pwd = getpwnam(pkg_user_name(u));
		if (pwd == NULL)
			continue;
		strlcpy(u->uidstr, pw_make(pwd), sizeof(u->uidstr));
	}*/

	return (ret);
}

static int
pkgdb_load_group(sqlite3 *sqlite, struct pkg *pkg)
{
	struct pkg_group	*g = NULL;
	struct group		*grp = NULL;
	int			 ret;
	const char		 sql[] = ""
		"SELECT groups.name "
		"FROM pkg_groups, groups "
		"WHERE package_id = ?1 "
			"AND group_id = groups.id "
		"ORDER by name DESC";

	assert(pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	ret = load_val(sqlite, pkg, sql, PKG_LOAD_GROUPS,
	    pkg_addgroup, PKG_GROUPS);

	while (pkg_groups(pkg, &g) == EPKG_OK) {
		grp = getgrnam(pkg_group_name(g));
		if (grp == NULL)
			continue;
		strlcpy(g->gidstr, gr_make(grp), sizeof(g->gidstr));
	}

	return (ret);
}

static int
pkgdb_load_shlib_required(sqlite3 *sqlite, struct pkg *pkg)
{
	char		 sql[BUFSIZ];
	const char	*basesql = ""
		"SELECT name "
		"FROM %Q.pkg_shlibs_required, %Q.shlibs AS s "
		"WHERE package_id = ?1 "
			"AND shlib_id = s.id "
		"ORDER by name DESC";

	assert(pkg != NULL);

	sqlite3_snprintf(sizeof(sql), sql, basesql, "main", "main");

	return (load_val(sqlite, pkg, sql, PKG_LOAD_SHLIBS_REQUIRED,
	    pkg_addshlib_required, PKG_SHLIBS_REQUIRED));
}


static int
pkgdb_load_shlib_provided(sqlite3 *sqlite, struct pkg *pkg)
{
	char		 sql[BUFSIZ];
	const char	*basesql = ""
		"SELECT name "
		"FROM %Q.pkg_shlibs_provided, %Q.shlibs AS s "
		"WHERE package_id = ?1 "
			"AND shlib_id = s.id "
		"ORDER by name DESC";

	assert(pkg != NULL);

	sqlite3_snprintf(sizeof(sql), sql, basesql, "main", "main");

	return (load_val(sqlite, pkg, sql, PKG_LOAD_SHLIBS_PROVIDED,
	    pkg_addshlib_provided, PKG_SHLIBS_PROVIDED));
}

static int
pkgdb_load_annotations(sqlite3 *sqlite, struct pkg *pkg)
{
	char		 sql[BUFSIZ];
	const char	*basesql = ""
		"SELECT k.annotation AS tag, v.annotation AS value"
		"  FROM %Q.pkg_annotation p"
		"    JOIN %Q.annotation k ON (p.tag_id = k.annotation_id)"
		"    JOIN %Q.annotation v ON (p.value_id = v.annotation_id)"
		"  WHERE p.package_id = ?1"
		"  ORDER BY tag, value";

	sqlite3_snprintf(sizeof(sql), sql, basesql, "main",
                    "main", "main");

	return (load_tag_val(sqlite, pkg, sql, PKG_LOAD_ANNOTATIONS,
		   pkg_addannotation, PKG_ANNOTATIONS));
}

static int
pkgdb_load_scripts(sqlite3 *sqlite, struct pkg *pkg)
{
	sqlite3_stmt	*stmt = NULL;
	int		 ret;
	int64_t		 rowid;
	const char	 sql[] = ""
		"SELECT script, type "
		"FROM pkg_script JOIN script USING(script_id) "
		"WHERE package_id = ?1";

	assert(pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	if (pkg->flags & PKG_LOAD_SCRIPTS)
		return (EPKG_OK);

	pkg_debug(4, "Pkgdb: running '%s'", sql);
	if (sqlite3_prepare_v2(sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite, sql);
		return (EPKG_FATAL);
	}

	pkg_get(pkg, PKG_ROWID, &rowid);
	sqlite3_bind_int64(stmt, 1, rowid);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		pkg_addscript(pkg, sqlite3_column_text(stmt, 0),
		    sqlite3_column_int(stmt, 1));
	}
	sqlite3_finalize(stmt);

	if (ret != SQLITE_DONE) {
		ERROR_SQLITE(sqlite, sql);
		return (EPKG_FATAL);
	}

	pkg->flags |= PKG_LOAD_SCRIPTS;
	return (EPKG_OK);
}


static int
pkgdb_load_options(sqlite3 *sqlite, struct pkg *pkg)
{
	const char	*reponame;
	char		 sql[BUFSIZ];
	unsigned int	 i;

	struct optionsql {
		const char	 *sql;
		int		(*pkg_addtagval)(struct pkg *pkg,
						  const char *tag,
						  const char *val);
		int		  nargs;
	}			  optionsql[] = {
		{
			"SELECT option, value "
			"FROM %Q.option JOIN %Q.pkg_option USING(option_id) "
			"WHERE package_id = ?1 ORDER BY option",
			pkg_addoption,
			2,
		},
		{
			"SELECT option, default_value "
			"FROM %Q.option JOIN %Q.pkg_option_default USING(option_id) "
			"WHERE package_id = ?1 ORDER BY option",
			pkg_addoption_default,
			2,
		},
		{
			"SELECT option, description "
			"FROM %Q.option JOIN %Q.pkg_option_desc USING(option_id) "
			"JOIN %Q.option_desc USING(option_desc_id) ORDER BY option",
			pkg_addoption_description,
			3,
		}
	};
	const char		 *opt_sql;
	int			(*pkg_addtagval)(struct pkg *pkg,
						 const char *tag,
						 const char *val);
	int			  nargs, ret;

	assert(pkg != NULL);

	if (pkg->flags & PKG_LOAD_OPTIONS)
		return (EPKG_OK);

	reponame = "main";

	for (i = 0; i < NELEM(optionsql); i++) {
		opt_sql       = optionsql[i].sql;
		pkg_addtagval = optionsql[i].pkg_addtagval;
		nargs         = optionsql[i].nargs;

		switch(nargs) {
		case 1:
			sqlite3_snprintf(sizeof(sql), sql, opt_sql, reponame);
			break;
		case 2:
			sqlite3_snprintf(sizeof(sql), sql, opt_sql, reponame,
					 reponame);
			break;
		case 3:
			sqlite3_snprintf(sizeof(sql), sql, opt_sql, reponame,
					 reponame, reponame);
			break;
		default:
			/* Nothing needs 4 or more, yet... */
			return (EPKG_FATAL);
			break;
		}

		pkg_debug(4, "Pkgdb> adding option");
		ret = load_tag_val(sqlite, pkg, sql, PKG_LOAD_OPTIONS,
				   pkg_addtagval, PKG_OPTIONS);
		if (ret != EPKG_OK)
			break;
	}
	return (ret);
}

static int
pkgdb_load_mtree(sqlite3 *sqlite, struct pkg *pkg)
{
	const char	sql[] = ""
		"SELECT m.content "
		"FROM mtree AS m, packages AS p "
		"WHERE m.id = p.mtree_id "
			"AND p.id = ?1;";

	assert(pkg != NULL);
	assert(pkg->type == PKG_INSTALLED);

	return (load_val(sqlite, pkg, sql, PKG_LOAD_MTREE, pkg_set_mtree, -1));
}

static int
pkgdb_load_conflicts(sqlite3 *sqlite, struct pkg *pkg)
{
	char		 sql[BUFSIZ];
	const char	*basesql = ""
			"SELECT packages.origin "
			"FROM %Q.pkg_conflicts "
			"LEFT JOIN %Q.packages ON "
			"packages.id = pkg_conflicts.conflict_id "
			"WHERE package_id = ?1";

	assert(pkg != NULL);

	sqlite3_snprintf(sizeof(sql), sql, basesql, "main", "main");

	return (load_val(sqlite, pkg, sql, PKG_LOAD_CONFLICTS,
			pkg_addconflict, PKG_CONFLICTS));
}

static int
pkgdb_load_provides(sqlite3 *sqlite, struct pkg *pkg)
{
	char		 sql[BUFSIZ];
	const char	*basesql = ""
		"SELECT provide "
		"FROM %Q.provides "
		"WHERE package_id = ?1";

	assert(pkg != NULL);

	sqlite3_snprintf(sizeof(sql), sql, basesql, "main", "main");

	return (load_val(sqlite, pkg, sql, PKG_LOAD_PROVIDES,
			pkg_addconflict, PKG_PROVIDES));
}

static void
populate_pkg(sqlite3_stmt *stmt, struct pkg *pkg) {
	int		 icol = 0;
	const char	*colname;

	assert(stmt != NULL);

	for (icol = 0; icol < sqlite3_column_count(stmt); icol++) {
		colname = sqlite3_column_name(stmt, icol);
		struct column_mapping *column;
		switch (sqlite3_column_type(stmt, icol)) {
		case SQLITE_TEXT:
			column = bsearch(colname, columns, NELEM(columns) - 1,
					sizeof(columns[0]), compare_column_func);
			if (column == NULL) {
				pkg_emit_error("unknown column %s", colname);
			}
			else {
				if (column->pkg_type == PKG_SQLITE_STRING)
					pkg_set(pkg, column->type,
						sqlite3_column_text(stmt, icol));
				else
					pkg_emit_error("want string for column %s and got number",
							colname);
			}
			break;
		case SQLITE_INTEGER:
			column = bsearch(colname, columns, NELEM(columns) - 1,
					sizeof(columns[0]), compare_column_func);
			if (column == NULL) {
				pkg_emit_error("Unknown column %s", colname);
			}
			else {
				if (column->pkg_type == PKG_SQLITE_INT64)
					pkg_set(pkg, column->type,
						sqlite3_column_int64(stmt, icol));
				else if (column->pkg_type == PKG_SQLITE_BOOL)
					pkg_set(pkg, column->type,
							(bool)sqlite3_column_int(stmt, icol));
				else
					pkg_emit_error("want number for column %s and got string",
							colname);
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
	{ PKG_LOAD_MTREE,		pkgdb_load_mtree },
	{ PKG_LOAD_CATEGORIES,		pkgdb_load_category },
	{ PKG_LOAD_LICENSES,		pkgdb_load_license },
	{ PKG_LOAD_USERS,		pkgdb_load_user },
	{ PKG_LOAD_GROUPS,		pkgdb_load_group },
	{ PKG_LOAD_SHLIBS_REQUIRED,	pkgdb_load_shlib_required },
	{ PKG_LOAD_SHLIBS_PROVIDED,	pkgdb_load_shlib_provided },
	{ PKG_LOAD_ANNOTATIONS,		pkgdb_load_annotations },
	{ PKG_LOAD_CONFLICTS,		pkgdb_load_conflicts },
	{ PKG_LOAD_PROVIDES,		pkgdb_load_provides },
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
	const char *digest;

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
		if (*pkg_p == NULL) {
			ret = pkg_new(pkg_p, it->pkg_type);
			if (ret != EPKG_OK)
				return (ret);
		} else
			pkg_reset(*pkg_p, it->pkg_type);
		pkg = *pkg_p;

		populate_pkg(it->stmt, pkg);

		pkg_get(pkg, PKG_DIGEST, &digest);
		if (digest != NULL && !pkg_checksum_is_valid(digest, strlen(digest)))
			pkg_set(pkg, PKG_DIGEST, NULL);

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
	int ret;

	assert(it != NULL);

	switch (it->type) {
	case PKGDB_IT_LOCAL:
		return (pkgdb_sqlite_it_next(&it->un.local, pkg_p, flags));
		break;
	case PKGDB_IT_REPO:
		if (it->un.remote != NULL) {
			rit = it->un.remote->it;
			ret = rit->ops->next(rit, pkg_p, flags);
			if (ret != EPKG_OK) {
				/*
				 * Detach this iterator from list and switch to another
				 */
				struct _pkg_repo_it_set *tmp;

				rit->ops->free(rit);
				tmp = it->un.remote;
				it->un.remote = tmp->next;
				free(tmp);

				return (pkgdb_it_next(it, pkg_p, flags));
			}

			if (*pkg_p != NULL)
				(*pkg_p)->repo = rit->repo;

			return (EPKG_OK);
		}
		/*
		 * All done
		 */
		return (EPKG_END);
		break;
	}

	return (EPKG_FATAL);
}

void
pkgdb_it_reset(struct pkgdb_it *it)
{
	struct _pkg_repo_it_set *cur;

	assert(it != NULL);

	switch (it->type) {
		case PKGDB_IT_LOCAL:
			pkgdb_sqlite_it_reset(&it->un.local);
			break;
		case PKGDB_IT_REPO:
			LL_FOREACH(it->un.remote, cur) {
				cur->it->ops->reset(cur->it);
			}
			break;
	}
}

void
pkgdb_it_free(struct pkgdb_it *it)
{
	struct _pkg_repo_it_set *cur, *tmp;

	if (it == NULL)
		return;

	switch (it->type) {
		case PKGDB_IT_LOCAL:
			pkgdb_sqlite_it_free(&it->un.local);
			break;
		case PKGDB_IT_REPO:
			LL_FOREACH_SAFE(it->un.remote, cur, tmp) {
				cur->it->ops->free(cur->it);
				free(cur);
			}
			break;
	}

	free(it);
}

struct pkgdb_it *
pkgdb_it_new_sqlite(struct pkgdb *db, sqlite3_stmt *s, int type, short flags)
{
	struct pkgdb_it	*it;

	assert(db != NULL && s != NULL);
	assert(!(flags & (PKGDB_IT_FLAG_CYCLED & PKGDB_IT_FLAG_ONCE)));
	assert(!(flags & (PKGDB_IT_FLAG_AUTO & (PKGDB_IT_FLAG_CYCLED | PKGDB_IT_FLAG_ONCE))));

	if ((it = malloc(sizeof(struct pkgdb_it))) == NULL) {
		pkg_emit_errno("malloc", "pkgdb_it");
		sqlite3_finalize(s);
		return (NULL);
	}

	it->type = PKGDB_IT_LOCAL;

	it->db = db;
	it->un.local.sqlite = db->sqlite;
	it->un.local.stmt = s;
	it->un.local.pkg_type = type;

	it->un.local.flags = flags;
	it->un.local.finished = 0;

	return (it);
}

struct pkgdb_it *
pkgdb_it_new_repo(struct pkgdb *db)
{
	struct pkgdb_it	*it;

	if ((it = malloc(sizeof(struct pkgdb_it))) == NULL) {
		pkg_emit_errno("malloc", "pkgdb_it");
		return (NULL);
	}

	it->type = PKGDB_IT_REPO;

	it->db = db;

	it->un.remote = NULL;

	return (it);
}

void
pkgdb_it_repo_attach(struct pkgdb_it *it, struct pkg_repo_it *rit)
{
	struct _pkg_repo_it_set *item;

	if ((item = malloc(sizeof(struct _pkg_repo_it_set))) == NULL) {
		pkg_emit_errno("malloc", "_pkg_repo_it_set");
	}
	else {
		item->it = rit;
		LL_PREPEND(it->un.remote, item);
	}
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
		}
	}

	return (EPKG_OK);
}

int
pkgdb_ensure_loaded(struct pkgdb *db, struct pkg *pkg, unsigned flags)
{
	int ret;
	struct _pkg_repo_list_item *cur;

	if (pkg->type == PKG_INSTALLED) {
		return (pkgdb_ensure_loaded_sqlite(db->sqlite, pkg, flags));
	}
	else {
		/* Call repo functions */
		LL_FOREACH(db->repos, cur) {
			if (cur->repo == pkg->repo) {
				if (cur->repo->ops->ensure_loaded) {
					ret = cur->repo->ops->ensure_loaded(cur->repo, pkg, flags);
					if (ret != EPKG_OK)
						return (EPKG_FATAL);
				}
			}
		}
	}

	/* Not reached */
	return (EPKG_FATAL);
}
