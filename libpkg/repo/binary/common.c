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

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <sqlite3.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/utils.h"
#include "binary_private.h"

static sql_prstmt sql_prepared_statements[PRSTMT_LAST] = {
	[PKG] = {
		NULL,
		"INSERT OR REPLACE INTO packages ("
		"origin, name, version, comment, desc, arch, maintainer, www, "
		"prefix, pkgsize, flatsize, licenselogic, cksum, path, manifestdigest, olddigest, "
		"vital)"
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17)",
	},
	[DEPS] = {
		NULL,
		"INSERT OR REPLACE INTO deps (origin, name, version, package_id) "
		"VALUES (?1, ?2, ?3, ?4)",
	},
	[CAT1] = {
		NULL,
		"INSERT OR IGNORE INTO categories(name) VALUES(?1)",
	},
	[CAT2] = {
		NULL,
		"INSERT OR ROLLBACK INTO pkg_categories(package_id, category_id) "
		"VALUES (?1, (SELECT id FROM categories WHERE name = ?2))",
	},
	[LIC1] = {
		NULL,
		"INSERT OR IGNORE INTO licenses(name) VALUES(?1)",
	},
	[LIC2] = {
		NULL,
		"INSERT OR ROLLBACK INTO pkg_licenses(package_id, license_id) "
		"VALUES (?1, (SELECT id FROM licenses WHERE name = ?2))",
	},
	[OPT1] = {
		NULL,
		"INSERT OR IGNORE INTO option(option) "
		"VALUES (?1)",
	},
	[OPT2] = {
		NULL,
		"INSERT OR ROLLBACK INTO pkg_option (option_id, value, package_id) "
		"VALUES (( SELECT option_id FROM option WHERE option = ?1), ?2, ?3)",
	},
	[SHLIB1] = {
		NULL,
		"INSERT OR IGNORE INTO shlibs(name) VALUES(?1)",
	},
	[SHLIB_REQD] = {
		NULL,
		"INSERT OR IGNORE INTO pkg_shlibs_required(package_id, shlib_id) "
		"VALUES (?1, (SELECT id FROM shlibs WHERE name = ?2))",
	},
	[SHLIB_PROV] = {
		NULL,
		"INSERT OR IGNORE INTO pkg_shlibs_provided(package_id, shlib_id) "
		"VALUES (?1, (SELECT id FROM shlibs WHERE name = ?2))",
	},
	[EXISTS] = {
		NULL,
		"SELECT count(*) FROM packages WHERE cksum=?1",
	},
	[ANNOTATE1] = {
		NULL,
		"INSERT OR IGNORE INTO annotation(annotation) "
		"VALUES (?1)",
	},
	[ANNOTATE2] = {
		NULL,
		"INSERT OR ROLLBACK INTO pkg_annotation(package_id, tag_id, value_id) "
		"VALUES (?1,"
		" (SELECT annotation_id FROM annotation WHERE annotation=?2),"
		" (SELECT annotation_id FROM annotation WHERE annotation=?3))",
	},
	[REPO_VERSION] = {
		NULL,
		"SELECT version FROM packages WHERE origin=?1",
	},
	[DELETE] = {
		NULL,
		"DELETE FROM packages WHERE origin=?1;"
		"DELETE FROM pkg_search WHERE origin=?1;",
	},
	[PROVIDE] = {
		NULL,
		"INSERT OR IGNORE INTO provides(provide) VALUES(?1)",
	},
	[PROVIDES] = {
		NULL,
		"INSERT OR IGNORE INTO pkg_provides(package_id, provide_id) "
		"VALUES (?1, (SELECT id FROM provides WHERE provide = ?2))",
	},
	[REQUIRE] = {
		NULL,
		"INSERT OR IGNORE INTO requires(require) VALUES(?1)",
	},
	[REQUIRES] = {
		NULL,
		"INSERT OR IGNORE INTO pkg_requires(package_id, require_id) "
		"VALUES (?1, (SELECT id FROM requires WHERE require = ?2))",
	},
	/* PRSTMT_LAST */
};

const char *
pkg_repo_binary_sql_prstatement(sql_prstmt_index s)
{
	if (s < PRSTMT_LAST)
		return (sql_prepared_statements[s].sql);
	else
		return ("unknown");
}

sqlite3_stmt*
pkg_repo_binary_stmt_prstatement(sql_prstmt_index s)
{
	if (s < PRSTMT_LAST)
		return (sql_prepared_statements[s].stmt);
	else
		return (NULL);
}

int
pkg_repo_binary_init_prstatements(sqlite3 *sqlite)
{
	sql_prstmt_index i, last;

	last = PRSTMT_LAST;

	for (i = 0; i < last; i++) {
		STMT(i) = prepare_sql(sqlite, SQL(i));
		if (STMT(i) == NULL)
			return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

int
pkg_repo_binary_run_prstatement(sql_prstmt_index s, const sql_arg_t *args, size_t nargs)
{
	int retcode;	/* Returns SQLITE error code */
	sqlite3_stmt *stmt;
	size_t i;

	stmt = STMT(s);
	sqlite3_reset(stmt);

	for (i = 0; i < nargs; i++) {
		int bind_index = (int)i + 1;
		switch(args[i].type) {
		case ARG_TEXT:
			sqlite3_bind_text(stmt, bind_index, args[i].v.text, -1,
			    SQLITE_STATIC);
			break;
		case ARG_INT64:
			sqlite3_bind_int64(stmt, bind_index, args[i].v.i64);
			break;
		default:
			return (SQLITE_MISUSE);
		}
	}

	retcode = sqlite3_step(stmt);

	return (retcode);
}

/*
 * Returns a path relative to the dbdir.
 */
const char *
pkg_repo_binary_get_filename(struct pkg_repo *repo)
{
	if (repo->dbpath == NULL)
		xasprintf(&repo->dbpath, "repos/%s/db", repo->name);
	return (repo->dbpath);
}

void
pkg_repo_binary_finalize_prstatements(void)
{
	sql_prstmt_index i, last;

	last = PRSTMT_LAST;

	for (i = 0; i < last; i++)
	{
		if (STMT(i) != NULL) {
			sqlite3_finalize(STMT(i));
			STMT(i) = NULL;
		}
	}
}
