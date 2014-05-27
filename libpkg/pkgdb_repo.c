/*-
 * Copyright (c) 2013 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
 * Copyright (c) 2013 Gerald Pfeifer <gerald@pfeifer.com>
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

#include <sys/param.h>

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
#include "private/repodb.h"

/* The package repo schema major revision */
#define REPO_SCHEMA_MAJOR 2

/* The package repo schema minor revision.
   Minor schema changes don't prevent older pkgng
   versions accessing the repo. */
#define REPO_SCHEMA_MINOR 9

/* REPO_SCHEMA_VERSION=2007 */
#define REPO_SCHEMA_VERSION (REPO_SCHEMA_MAJOR * 1000 + REPO_SCHEMA_MINOR)

typedef enum _sql_prstmt_index {
	PKG = 0,
	DEPS,
	CAT1,
	CAT2,
	LIC1,
	LIC2,
	OPT1,
	OPT2,
	SHLIB1,
	SHLIB_REQD,
	SHLIB_PROV,
	ANNOTATE1,
	ANNOTATE2,
	EXISTS,
	VERSION,
	DELETE,
	FTS_APPEND,
	PRSTMT_LAST,
} sql_prstmt_index;

static sql_prstmt sql_prepared_statements[PRSTMT_LAST] = {
	[PKG] = {
		NULL,
		"INSERT INTO packages ("
		"origin, name, version, comment, desc, arch, maintainer, www, "
		"prefix, pkgsize, flatsize, licenselogic, cksum, path, manifestdigest"
		")"
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15)",
		"TTTTTTTTTIIITTT",
	},
	[DEPS] = {
		NULL,
		"INSERT INTO deps (origin, name, version, package_id) "
		"VALUES (?1, ?2, ?3, ?4)",
		"TTTI",
	},
	[CAT1] = {
		NULL,
		"INSERT OR IGNORE INTO categories(name) VALUES(?1)",
		"T",
	},
	[CAT2] = {
		NULL,
		"INSERT OR ROLLBACK INTO pkg_categories(package_id, category_id) "
		"VALUES (?1, (SELECT id FROM categories WHERE name = ?2))",
		"IT",
	},
	[LIC1] = {
		NULL,
		"INSERT OR IGNORE INTO licenses(name) VALUES(?1)",
		"T",
	},
	[LIC2] = {
		NULL,
		"INSERT OR ROLLBACK INTO pkg_licenses(package_id, license_id) "
		"VALUES (?1, (SELECT id FROM licenses WHERE name = ?2))",
		"IT",
	},
	[OPT1] = {
		NULL,
		"INSERT OR IGNORE INTO option(option) "
		"VALUES (?1)",
		"T",
	},
	[OPT2] = {
		NULL,
		"INSERT OR ROLLBACK INTO pkg_option (option_id, value, package_id) "
		"VALUES (( SELECT option_id FROM option WHERE option = ?1), ?2, ?3)",
		"TTI",
	},
	[SHLIB1] = {
		NULL,
		"INSERT OR IGNORE INTO shlibs(name) VALUES(?1)",
		"T",
	},
	[SHLIB_REQD] = {
		NULL,
		"INSERT OR ROLLBACK INTO pkg_shlibs_required(package_id, shlib_id) "
		"VALUES (?1, (SELECT id FROM shlibs WHERE name = ?2))",
		"IT",
	},
	[SHLIB_PROV] = {
		NULL,
		"INSERT OR ROLLBACK INTO pkg_shlibs_provided(package_id, shlib_id) "
		"VALUES (?1, (SELECT id FROM shlibs WHERE name = ?2))",
		"IT",
	},
	[EXISTS] = {
		NULL,
		"SELECT count(*) FROM packages WHERE cksum=?1",
		"T",
	},
	[ANNOTATE1] = {
		NULL,
		"INSERT OR IGNORE INTO annotation(annotation) "
		"VALUES (?1)",
		"T",
	},
	[ANNOTATE2] = {
		NULL,
		"INSERT OR ROLLBACK INTO pkg_annotation(package_id, tag_id, value_id) "
		"VALUES (?1,"
		" (SELECT annotation_id FROM annotation WHERE annotation=?2),"
		" (SELECT annotation_id FROM annotation WHERE annotation=?3))",
		"ITT",
	},
	[VERSION] = {
		NULL,
		"SELECT version FROM packages WHERE origin=?1",
		"T",
	},
	[DELETE] = {
		NULL,
		"DELETE FROM packages WHERE origin=?1;"
		"DELETE FROM pkg_search WHERE origin=?1;",
		"TT",
	},
	[FTS_APPEND] = {
		NULL,
		"INSERT OR ROLLBACK INTO pkg_search(id, name, origin) "
		"VALUES (?1, ?2 || '-' || ?3, ?4);",
		"ITTT"
	}
	/* PRSTMT_LAST */
};


static void
file_exists(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
	char	 fpath[MAXPATHLEN];
	sqlite3	*db = sqlite3_context_db_handle(ctx);
	char	*path = dirname(sqlite3_db_filename(db, "main"));
	char	 cksum[SHA256_DIGEST_LENGTH * 2 +1];

	if (argc != 2) {
		sqlite3_result_error(ctx, "file_exists needs two argument", -1);
		return;
	}

	snprintf(fpath, sizeof(fpath), "%s/%s", path, sqlite3_value_text(argv[0]));

	if (access(fpath, R_OK) == 0) {
		sha256_file(fpath, cksum);
		if (strcmp(cksum, sqlite3_value_text(argv[1])) == 0)
			sqlite3_result_int(ctx, 1);
		else
			sqlite3_result_int(ctx, 0);
	} else {
		sqlite3_result_int(ctx, 0);
	}
}

static int
get_repo_user_version(sqlite3 *sqlite, const char *database, int *reposcver)
{
	sqlite3_stmt *stmt;
	int retcode;
	char sql[BUFSIZ];
	const char *fmt = "PRAGMA %Q.user_version";

	assert(database != NULL);

	sqlite3_snprintf(sizeof(sql), sql, fmt, database);

	if (sqlite3_prepare_v2(sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite, sql);
		return (EPKG_FATAL);
	}

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		*reposcver = sqlite3_column_int(stmt, 0);
		retcode = EPKG_OK;
	} else {
		*reposcver = -1;
		retcode = EPKG_FATAL;
	}
	sqlite3_finalize(stmt);
	return (retcode);
}

static int
initialize_prepared_statements(sqlite3 *sqlite)
{
	sql_prstmt_index i, last;
	int ret;

	last = PRSTMT_LAST;

	for (i = 0; i < last; i++) {
		ret = sqlite3_prepare_v2(sqlite, SQL(i), -1, &STMT(i), NULL);
		if (ret != SQLITE_OK) {
			ERROR_SQLITE(sqlite, SQL(i));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

static int
run_prepared_statement(sql_prstmt_index s, ...)
{
	int retcode;	/* Returns SQLITE error code */
	va_list ap;
	sqlite3_stmt *stmt;
	int i;
	const char *argtypes;

	stmt = STMT(s);
	argtypes = sql_prepared_statements[s].argtypes;

	sqlite3_reset(stmt);

	va_start(ap, s);

	for (i = 0; argtypes[i] != '\0'; i++)
	{
		switch (argtypes[i]) {
		case 'T':
			sqlite3_bind_text(stmt, i + 1, va_arg(ap, const char*),
			    -1, SQLITE_STATIC);
			break;
		case 'I':
			sqlite3_bind_int64(stmt, i + 1, va_arg(ap, int64_t));
			break;
		}
	}

	va_end(ap);

	retcode = sqlite3_step(stmt);

	return (retcode);
}

void
pkgdb_repo_finalize_statements(void)
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
	return;
}

int
pkgdb_repo_open(const char *repodb, bool force, sqlite3 **sqlite)
{
	bool incremental = false;
	bool db_not_open;
	int reposcver;
	int retcode = EPKG_OK;

	if (access(repodb, R_OK) == 0)
		incremental = true;

	sqlite3_initialize();
	db_not_open = true;
	while (db_not_open) {
		if (sqlite3_open(repodb, sqlite) != SQLITE_OK) {
			sqlite3_shutdown();
			return (EPKG_FATAL);
		}

		db_not_open = false;

		/* If the schema is too old, or we're forcing a full
			   update, then we cannot do an incremental update.
			   Delete the existing repo, and promote this to a
			   full update */
		if (!incremental)
			continue;
		retcode = get_repo_user_version(*sqlite, "main", &reposcver);
		if (retcode != EPKG_OK)
			return (EPKG_FATAL);
		if (force || reposcver != REPO_SCHEMA_VERSION) {
			if (reposcver != REPO_SCHEMA_VERSION)
				pkg_emit_error("re-creating repo to upgrade schema version "
						"from %d to %d", reposcver,
						REPO_SCHEMA_VERSION);
			sqlite3_close(*sqlite);
			unlink(repodb);
			incremental = false;
			db_not_open = true;
		}
	}

	sqlite3_create_function(*sqlite, "file_exists", 2, SQLITE_ANY, NULL,
	    file_exists, NULL, NULL);

	if (!incremental) {
		retcode = sql_exec(*sqlite, initsql, REPO_SCHEMA_VERSION);
		if (retcode != EPKG_OK)
			return (retcode);
	}

	return (EPKG_OK);
}

int
pkgdb_repo_init(sqlite3 *sqlite)
{
	int retcode = EPKG_OK;

	retcode = sql_exec(sqlite, "PRAGMA synchronous=default");
	if (retcode != EPKG_OK)
		return (retcode);

	retcode = sql_exec(sqlite, "PRAGMA foreign_keys=on");
	if (retcode != EPKG_OK)
		return (retcode);

	retcode = initialize_prepared_statements(sqlite);
	if (retcode != EPKG_OK)
		return (retcode);

	return (EPKG_OK);
}

int
pkgdb_repo_close(sqlite3 *sqlite, bool commit)
{
	int retcode = EPKG_OK;

	if (sqlite == NULL)
		return (retcode);

	if (commit) {
		if (pkgdb_transaction_commit(sqlite, NULL) != SQLITE_OK)
			retcode = EPKG_FATAL;
	}
	else {
		if (pkgdb_transaction_rollback(sqlite, NULL) != SQLITE_OK)
				retcode = EPKG_FATAL;
	}
	pkgdb_repo_finalize_statements();

	return (retcode);
}

static int
maybe_delete_conflicting(const char *origin, const char *version,
			 const char *pkg_path, bool forced)
{
	int ret = EPKG_FATAL;
	const char *oversion;

	if (run_prepared_statement(VERSION, origin) != SQLITE_ROW)
		return (EPKG_FATAL); /* sqlite error */
	oversion = sqlite3_column_text(STMT(VERSION), 0);
	if (!forced) {
		switch(pkg_version_cmp(oversion, version)) {
		case -1:
			pkg_emit_error("duplicate package origin: replacing older "
					"version %s in repo with package %s for "
					"origin %s", oversion, pkg_path, origin);

			if (run_prepared_statement(DELETE, origin, origin) != SQLITE_DONE)
				return (EPKG_FATAL); /* sqlite error */

			ret = EPKG_OK;	/* conflict cleared */
			break;
		case 0:
		case 1:
			pkg_emit_error("duplicate package origin: package %s is not "
					"newer than version %s already in repo for "
					"origin %s", pkg_path, oversion, origin);
			ret = EPKG_END;	/* keep what is already in the repo */
			break;
		}
	}
	else {
		if (run_prepared_statement(DELETE, origin, origin) != SQLITE_DONE)
			return (EPKG_FATAL); /* sqlite error */

		ret = EPKG_OK;
	}
	return (ret);
}

int
pkgdb_repo_cksum_exists(sqlite3 *sqlite, const char *cksum)
{
	if (run_prepared_statement(EXISTS, cksum) != SQLITE_ROW) {
		ERROR_SQLITE(sqlite, SQL(EXISTS));
		return (EPKG_FATAL);
	}
	if (sqlite3_column_int(STMT(EXISTS), 0) > 0) {
		return (EPKG_OK);
	}
	return (EPKG_END);
}

int
pkgdb_repo_add_package(struct pkg *pkg, const char *pkg_path,
		sqlite3 *sqlite, const char *manifest_digest, bool forced)
{
	const char *name, *version, *origin, *comment, *desc;
	const char *arch, *maintainer, *www, *prefix, *sum, *rpath;
	int64_t			 flatsize, pkgsize;
	int64_t			 licenselogic;
	int			 ret;
	struct pkg_dep		*dep      = NULL;
	struct pkg_option	*option   = NULL;
	struct pkg_shlib	*shlib    = NULL;
	const pkg_object	*obj, *licenses, *categories, *annotations;
	pkg_iter		 it;
	int64_t			 package_id;

	pkg_get(pkg, PKG_ORIGIN, &origin, PKG_NAME, &name,
			    PKG_VERSION, &version, PKG_COMMENT, &comment,
			    PKG_DESC, &desc, PKG_ARCH, &arch,
			    PKG_MAINTAINER, &maintainer, PKG_WWW, &www,
			    PKG_PREFIX, &prefix, PKG_FLATSIZE, &flatsize,
			    PKG_LICENSE_LOGIC, &licenselogic, PKG_CKSUM, &sum,
			    PKG_PKGSIZE, &pkgsize, PKG_REPOPATH, &rpath,
			    PKG_LICENSES, &licenses, PKG_CATEGORIES, &categories,
			    PKG_ANNOTATIONS, &annotations);

try_again:
	if ((ret = run_prepared_statement(PKG, origin, name, version,
			comment, desc, arch, maintainer, www, prefix,
			pkgsize, flatsize, (int64_t)licenselogic, sum,
			rpath, manifest_digest)) != SQLITE_DONE) {
		if (ret == SQLITE_CONSTRAINT) {
			switch(maybe_delete_conflicting(origin,
					version, pkg_path, forced)) {
			case EPKG_FATAL: /* sqlite error */
				ERROR_SQLITE(sqlite, SQL(PKG));
				return (EPKG_FATAL);
				break;
			case EPKG_END: /* repo already has newer */
				return (EPKG_END);
				break;
			default: /* conflict cleared, try again */
				goto try_again;
				break;
			}
		} else {
			ERROR_SQLITE(sqlite, SQL(PKG));
			return (EPKG_FATAL);
		}
	}
	package_id = sqlite3_last_insert_rowid(sqlite);

	if (run_prepared_statement (FTS_APPEND, package_id,
			name, version, origin) != SQLITE_DONE) {
		ERROR_SQLITE(sqlite, SQL(FTS_APPEND));
		return (EPKG_FATAL);
	}

	dep = NULL;
	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		if (run_prepared_statement(DEPS,
				pkg_dep_origin(dep),
				pkg_dep_name(dep),
				pkg_dep_version(dep),
				package_id) != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, SQL(DEPS));
			return (EPKG_FATAL);
		}
	}

	it = NULL;
	while ((obj = pkg_object_iterate(categories, &it))) {
		ret = run_prepared_statement(CAT1, pkg_object_string(obj));
		if (ret == SQLITE_DONE)
			ret = run_prepared_statement(CAT2, package_id,
			    pkg_object_string(obj));
		if (ret != SQLITE_DONE)
		{
			ERROR_SQLITE(sqlite, SQL(CAT2));
			return (EPKG_FATAL);
		}
	}

	it = NULL;
	while ((obj = pkg_object_iterate(licenses, &it))) {
		ret = run_prepared_statement(LIC1, pkg_object_string(obj));
		if (ret == SQLITE_DONE)
			ret = run_prepared_statement(LIC2, package_id,
			    pkg_object_string(obj));
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, SQL(LIC2));
			return (EPKG_FATAL);
		}
	}
	option = NULL;
	while (pkg_options(pkg, &option) == EPKG_OK) {
		ret = run_prepared_statement(OPT1, pkg_option_opt(option));
		if (ret == SQLITE_DONE)
		    ret = run_prepared_statement(OPT2, pkg_option_opt(option),
				pkg_option_value(option), package_id);
		if(ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, SQL(OPT2));
			return (EPKG_FATAL);
		}
	}

	shlib = NULL;
	while (pkg_shlibs_required(pkg, &shlib) == EPKG_OK) {
		const char *shlib_name = pkg_shlib_name(shlib);

		ret = run_prepared_statement(SHLIB1, shlib_name);
		if (ret == SQLITE_DONE)
			ret = run_prepared_statement(SHLIB_REQD, package_id,
					shlib_name);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, SQL(SHLIB_REQD));
			return (EPKG_FATAL);
		}
	}

	shlib = NULL;
	while (pkg_shlibs_provided(pkg, &shlib) == EPKG_OK) {
		const char *shlib_name = pkg_shlib_name(shlib);

		ret = run_prepared_statement(SHLIB1, shlib_name);
		if (ret == SQLITE_DONE)
			ret = run_prepared_statement(SHLIB_PROV, package_id,
					shlib_name);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, SQL(SHLIB_PROV));
			return (EPKG_FATAL);
		}
	}

	it = NULL;
	while ((obj = pkg_object_iterate(annotations, &it))) {
		const char *note_tag = pkg_object_key(obj);
		const char *note_val = pkg_object_string(obj);

		ret = run_prepared_statement(ANNOTATE1, note_tag);
		if (ret == SQLITE_DONE)
			ret = run_prepared_statement(ANNOTATE1, note_val);
		if (ret == SQLITE_DONE)
			ret = run_prepared_statement(ANNOTATE2, package_id,
				  note_tag, note_val);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, SQL(ANNOTATE2));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

int
pkgdb_repo_remove_package(const char *origin)
{
	if (run_prepared_statement(DELETE, origin, origin) != SQLITE_DONE)
		return (EPKG_FATAL); /* sqlite error */

	return (EPKG_OK);
}

/* We want to replace some arbitrary number of instances of the placeholder
   %Q in the SQL with the name of the database. */
static int
substitute_into_sql(char *sqlbuf, size_t buflen, const char *fmt,
		    const char *replacement)
{
	char	*f;
	char	*f0;
	char	*tofree;
	char	*quoted;
	size_t	 len;
	int	 ret = EPKG_OK;

	tofree = f = strdup(fmt);
	if (tofree == NULL)
		return (EPKG_FATAL); /* out of memory */

	quoted = sqlite3_mprintf("%Q", replacement);
	if (quoted == NULL) {
		free(tofree);
		return (EPKG_FATAL); /* out of memory */
	}

	sqlbuf[0] = '\0';

	while ((f0 = strsep(&f, "%")) != NULL) {
		len = strlcat(sqlbuf, f0, buflen);
		if (len >= buflen) {
			/* Overflowed the buffer */
			ret = EPKG_FATAL;
			break;
		}

		if (f == NULL)
			break;	/* done */

		if (f[0] == 'Q') {
			len = strlcat(sqlbuf, quoted, buflen);
			f++;	/* Jump the Q */
		} else {
			len = strlcat(sqlbuf, "%", buflen);
		}

		if (len >= buflen) {
			/* Overflowed the buffer */
			ret = EPKG_FATAL;
			break;
		}
	}

	free(tofree);
	sqlite3_free(quoted);

	return (ret);
}

static int
set_repo_user_version(sqlite3 *sqlite, const char *database, int reposcver)
{
	int		 retcode = EPKG_OK;
	char		 sql[BUFSIZ];
	char		*errmsg;
	const char	*fmt = "PRAGMA %Q.user_version = %d;" ;

	assert(database != NULL);

	sqlite3_snprintf(sizeof(sql), sql, fmt, database, reposcver);

	if (sqlite3_exec(sqlite, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
		pkg_emit_error("sqlite: %s", errmsg);
		sqlite3_free(errmsg);
		retcode = EPKG_FATAL;
	}
	return (retcode);
}

static int
apply_repo_change(struct pkgdb *db, const char *database,
		  const struct repo_changes *repo_changes, const char *updown,
		  int version, int *next_version)
{
	const struct repo_changes	*change;
	bool			 found = false, in_trans = false;
	int			 ret = EPKG_OK;
	char			 sql[8192];
	char			*errmsg;

	for (change = repo_changes; change->version != -1; change++) {
		if (change->version == version) {
			found = true;
			break;
		}
	}
	if (!found) {
		pkg_emit_error("Failed to %s \"%s\" repo schema "
			" version %d (target version %d) "
			"-- change not found", updown, database, version,
			REPO_SCHEMA_VERSION);
		return (EPKG_FATAL);
	}

	/* substitute the repo database name */
	ret = substitute_into_sql(sql, sizeof(sql), change->sql, database);

	/* begin transaction */
	if (ret == EPKG_OK) {
		in_trans = true;
		ret = pkgdb_transaction_begin(db->sqlite, "SCHEMA");
	}

	/* apply change */
	if (ret == EPKG_OK) {
		pkg_debug(4, "Pkgdb: running '%s'", sql);
		ret = sqlite3_exec(db->sqlite, sql, NULL, NULL, &errmsg);
		if (ret != SQLITE_OK) {
			pkg_emit_error("sqlite: %s", errmsg);
			sqlite3_free(errmsg);
			ret = EPKG_FATAL;
		}
	}

	/* update repo user_version */
	if (ret == EPKG_OK) {
		*next_version = change->next_version;
		ret = set_repo_user_version(db->sqlite, database, *next_version);
	}

	/* commit or rollback */
	if (in_trans) {
		if (ret != EPKG_OK)
			pkgdb_transaction_rollback(db->sqlite, "SCHEMA");

		if (pkgdb_transaction_commit(db->sqlite, "SCHEMA") != EPKG_OK)
			ret = EPKG_FATAL;
	}

	if (ret == EPKG_OK) {
		pkg_emit_notice("Repo \"%s\" %s schema %d to %d: %s",
				database, updown, version,
				change->next_version, change->message);
	}

	return (ret);
}

static int
upgrade_repo_schema(struct pkgdb *db, const char *database, int current_version)
{
	int version;
	int next_version;
	int ret = EPKG_OK;

	for (version = current_version;
	     version < REPO_SCHEMA_VERSION;
	     version = next_version)  {
		ret = apply_repo_change(db, database, repo_upgrades,
					"upgrade", version, &next_version);
		if (ret != EPKG_OK)
			break;
		pkg_debug(1, "Upgrading repo database schema from %d to %d",
				version, next_version);
	}
	return (ret);
}

static int
downgrade_repo_schema(struct pkgdb *db, const char *database, int current_version)
{
	int version;
	int next_version;
	int ret = EPKG_OK;

	for (version = current_version;
	     version > REPO_SCHEMA_VERSION;
	     version = next_version)  {

		ret = apply_repo_change(db, database, repo_downgrades,
					"downgrade", version, &next_version);
		if (ret != EPKG_OK)
			break;
		pkg_debug(1, "Downgrading repo database schema from %d to %d",
				version, next_version);
	}
	return (ret);
}

int
pkgdb_repo_check_version(struct pkgdb *db, const char *database)
{
	int reposcver;
	int repomajor;
	int ret;

	assert(db != NULL);
	assert(database != NULL);

	if ((ret = get_repo_user_version(db->sqlite, database, &reposcver))
	    != EPKG_OK)
		return (ret);	/* sqlite error */

	/*
	 * If the local pkgng uses a repo schema behind that used to
	 * create the repo, we may still be able use it for reading
	 * (ie pkg install), but pkg repo can't do an incremental
	 * update unless the actual schema matches the compiled in
	 * schema version.
	 *
	 * Use a major - minor version schema: as the user_version
	 * PRAGMA takes an integer version, encode this as MAJOR *
	 * 1000 + MINOR.
	 *
	 * So long as the major versions are the same, the local pkgng
	 * should be compatible with any repo created by a more recent
	 * pkgng, although it may need some modification of the repo
	 * schema
	 */

	/* --- Temporary ---- Grandfather in the old repo schema
	   version so this patch doesn't immediately invalidate all
	   the repos out there */

	if (reposcver == 2)
		reposcver = 2000;
	if (reposcver == 3)
		reposcver = 2001;

	repomajor = reposcver / 1000;

	if (repomajor < REPO_SCHEMA_MAJOR) {
		pkg_emit_error("Repo %s (schema version %d) is too old - "
		    "need at least schema %d", database, reposcver,
		    REPO_SCHEMA_MAJOR * 1000);
		return (EPKG_REPOSCHEMA);
	}

	if (repomajor > REPO_SCHEMA_MAJOR) {
		pkg_emit_error("Repo %s (schema version %d) is too new - "
		    "we can accept at most schema %d", database, reposcver,
		    ((REPO_SCHEMA_MAJOR + 1) * 1000) - 1);
		return (EPKG_REPOSCHEMA);
	}

	/* This is a repo schema version we can work with */

	ret = EPKG_OK;

	if (reposcver < REPO_SCHEMA_VERSION) {
		if (sqlite3_db_readonly(db->sqlite, database)) {
			pkg_emit_error("Repo %s needs schema upgrade from "
			"%d to %d but it is opened readonly", database,
			       reposcver, REPO_SCHEMA_VERSION
			);
			ret = EPKG_FATAL;
		} else
			ret = upgrade_repo_schema(db, database, reposcver);
	} else if (reposcver > REPO_SCHEMA_VERSION) {
		if (sqlite3_db_readonly(db->sqlite, database)) {
			pkg_emit_error("Repo %s needs schema downgrade from "
			"%d to %d but it is opened readonly", database,
			       reposcver, REPO_SCHEMA_VERSION
			);
			ret = EPKG_FATAL;
		} else
			ret = downgrade_repo_schema(db, database, reposcver);
	}

	return (ret);
}

struct pkgdb_it *
pkgdb_repo_origins(sqlite3 *sqlite)
{
	sqlite3_stmt *stmt = NULL;
	int ret;
	static struct pkgdb repodb;
	const char query_sql[] = ""
		"SELECT id, origin, name, name || '~' || origin as uniqueid, version, comment, "
		"prefix, desc, arch, maintainer, www, "
		"licenselogic, flatsize, pkgsize, "
		"cksum, path AS repopath, manifestdigest "
		"FROM packages "
		"ORDER BY origin;";

	ret = sqlite3_prepare_v2(sqlite, query_sql, -1,
			&stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(sqlite, query_sql);
		return (NULL);
	}
	repodb.sqlite = sqlite;
	repodb.type = PKGDB_REMOTE;

	return pkgdb_it_new(&repodb, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE);
}

int
pkgdb_repo_register_conflicts(const char *origin, char **conflicts,
		int conflicts_num, sqlite3 *sqlite)
{
	const char clean_conflicts_sql[] = ""
			"DELETE FROM pkg_conflicts "
			"WHERE package_id = ?1;";
	const char select_id_sql[] = ""
			"SELECT id FROM packages "
			"WHERE origin = ?1;";
	const char insert_conflict_sql[] = ""
			"INSERT INTO pkg_conflicts "
			"(package_id, conflict_id) "
			"VALUES (?1, ?2);";
	sqlite3_stmt *stmt = NULL;
	int ret, i;
	int64_t origin_id, conflict_id;

	pkg_debug(4, "pkgdb_repo_register_conflicts: running '%s'", select_id_sql);
	if (sqlite3_prepare_v2(sqlite, select_id_sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite, select_id_sql);
		return (EPKG_FATAL);
	}

	sqlite3_bind_text(stmt, 1, origin, -1, SQLITE_TRANSIENT);
	ret = sqlite3_step(stmt);

	if (ret == SQLITE_ROW) {
		origin_id = sqlite3_column_int64(stmt, 0);
	}
	else {
		ERROR_SQLITE(sqlite, select_id_sql);
		return (EPKG_FATAL);
	}
	sqlite3_finalize(stmt);

	pkg_debug(4, "pkgdb_repo_register_conflicts: running '%s'", clean_conflicts_sql);
	if (sqlite3_prepare_v2(sqlite, clean_conflicts_sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite, clean_conflicts_sql);
		return (EPKG_FATAL);
	}

	sqlite3_bind_int64(stmt, 1, origin_id);
	/* Ignore cleanup result */
	(void)sqlite3_step(stmt);

	sqlite3_finalize(stmt);

	for (i = 0; i < conflicts_num; i ++) {
		/* Select a conflict */
		pkg_debug(4, "pkgdb_repo_register_conflicts: running '%s'", select_id_sql);
		if (sqlite3_prepare_v2(sqlite, select_id_sql, -1, &stmt, NULL) != SQLITE_OK) {
			ERROR_SQLITE(sqlite, select_id_sql);
			return (EPKG_FATAL);
		}

		sqlite3_bind_text(stmt, 1, conflicts[i], -1, SQLITE_TRANSIENT);
		ret = sqlite3_step(stmt);

		if (ret == SQLITE_ROW) {
			conflict_id = sqlite3_column_int64(stmt, 0);
		}
		else {
			ERROR_SQLITE(sqlite, select_id_sql);
			return (EPKG_FATAL);
		}

		sqlite3_finalize(stmt);

		/* Insert a pair */
		pkg_debug(4, "pkgdb_repo_register_conflicts: running '%s'", insert_conflict_sql);
		if (sqlite3_prepare_v2(sqlite, insert_conflict_sql, -1, &stmt, NULL) != SQLITE_OK) {
			ERROR_SQLITE(sqlite, insert_conflict_sql);
			return (EPKG_FATAL);
		}

		sqlite3_bind_int64(stmt, 1, origin_id);
		sqlite3_bind_int64(stmt, 2, conflict_id);
		ret = sqlite3_step(stmt);

		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, insert_conflict_sql);
			return (EPKG_FATAL);
		}

		sqlite3_finalize(stmt);
	}

	return (EPKG_OK);
}

const char *
pkgdb_get_reponame(struct pkgdb *db, const char *repo)
{
	const char	*reponame = NULL;
	struct pkg_repo	*r;

	assert(db->type == PKGDB_REMOTE);

	if (repo != NULL) {
		if ((r = pkg_repo_find_ident(repo)) == NULL) {
			pkg_emit_error("repository '%s' does not exist", repo);
			return (NULL);
		}
		reponame = pkg_repo_name(r);

		if (!pkgdb_is_attached(db->sqlite, reponame)) {
			pkg_emit_error("repository '%s' does not exist", repo);
			return (NULL);
		}
	}

	return (reponame);
}

struct pkgdb_it *
pkgdb_rquery(struct pkgdb *db, const char *pattern, match_t match,
    const char *repo)
{
	sqlite3_stmt	*stmt = NULL;
	struct sbuf	*sql = NULL;
	const char	*reponame = NULL;
	const char	*comp = NULL;
	int		 ret;
	char		 basesql[BUFSIZ] = ""
		"SELECT id, origin, name, name || '~' || origin as uniqueid, version, comment, "
		"prefix, desc, arch, maintainer, www, "
		"licenselogic, flatsize, pkgsize, "
		"cksum, manifestdigest, path AS repopath, '%1$s' AS dbname "
		"FROM '%1$s'.packages p";

	assert(db != NULL);
	assert(match == MATCH_ALL || (pattern != NULL && pattern[0] != '\0'));

	/*
	 * If we have no remote repos loaded, we just return nothing instead of failing
	 * an assert deep inside pkgdb_get_reponame
	 */
	if (db->type != PKGDB_REMOTE)
		return (NULL);

	reponame = pkgdb_get_reponame(db, repo);

	sql = sbuf_new_auto();
	comp = pkgdb_get_pattern_query(pattern, match);
	if (comp && comp[0])
		strlcat(basesql, comp, sizeof(basesql));

	/*
	 * Working on multiple remote repositories
	 */
	if (reponame == NULL) {
		/* duplicate the query via UNION for all the attached
		 * databases */

		ret = pkgdb_sql_all_attached(db->sqlite, sql,
		    basesql, " UNION ALL ");
		if (ret != EPKG_OK) {
			sbuf_delete(sql);
			return (NULL);
		}
	} else
		sbuf_printf(sql, basesql, reponame, reponame);

	sbuf_cat(sql, " ORDER BY name;");
	sbuf_finish(sql);

	pkg_debug(4, "Pkgdb: running '%s' query for %s", sbuf_get(sql), pattern);
	ret = sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), sbuf_size(sql), &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sbuf_get(sql));
		sbuf_delete(sql);
		return (NULL);
	}

	sbuf_delete(sql);

	if (match != MATCH_ALL && match != MATCH_CONDITION)
		sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE));
}

struct pkgdb_it *
pkgdb_rquery_provide(struct pkgdb *db, const char *provide, const char *repo)
{
	sqlite3_stmt	*stmt;
	struct sbuf	*sql = NULL;
	const char	*reponame = NULL;
	int		 ret;
	const char	 basesql[] = ""
			"SELECT p.id, p.origin, p.name, p.version, p.comment, "
			"p.name || '~' || p.origin as uniqueid, "
			"p.prefix, p.desc, p.arch, p.maintainer, p.www, "
			"p.licenselogic, p.flatsize, p.pkgsize, "
			"p.cksum, p.manifestdigest, p.path AS repopath, '%1$s' AS dbname "
			"FROM '%1$s'.packages AS p, '%1$s'.pkg_provides AS pp, "
			"'%1$s'.provides AS pr "
			"WHERE p.id = pp.package_id "
			"AND pp.provide_id = pr.id "
			"AND pr.name = ?1;";

	assert(db != NULL);
	reponame = pkgdb_get_reponame(db, repo);

	sql = sbuf_new_auto();
	/*
	 * Working on multiple remote repositories
	 */
	if (reponame == NULL) {
		/* duplicate the query via UNION for all the attached
		 * databases */

		ret = pkgdb_sql_all_attached(db->sqlite, sql,
				basesql, " UNION ALL ");
		if (ret != EPKG_OK) {
			sbuf_delete(sql);
			return (NULL);
		}
	} else
		sbuf_printf(sql, basesql, reponame);

	sbuf_finish(sql);

	pkg_debug(4, "Pkgdb: running '%s'", sbuf_get(sql));
	ret = sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sbuf_get(sql));
		sbuf_delete(sql);
		return (NULL);
	}

	sbuf_delete(sql);

	sqlite3_bind_text(stmt, 1, provide, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE));
}

struct pkgdb_it *
pkgdb_find_shlib_provide(struct pkgdb *db, const char *require, const char *repo)
{
	sqlite3_stmt	*stmt;
	struct sbuf	*sql = NULL;
	const char	*reponame = NULL;
	int		 ret;
	const char	 basesql[] = ""
			"SELECT p.id, p.origin, p.name, p.version, p.comment, "
			"p.name || '~' || p.origin as uniqueid, "
			"p.prefix, p.desc, p.arch, p.maintainer, p.www, "
			"p.licenselogic, p.flatsize, p.pkgsize, "
			"p.cksum, p.manifestdigest, p.path AS repopath, '%1$s' AS dbname "
			"FROM '%1$s'.packages AS p INNER JOIN '%1$s'.pkg_shlibs_provided AS ps ON "
			"p.id = ps.package_id "
			"WHERE ps.shlib_id IN (SELECT id FROM '%1$s'.shlibs WHERE "
			"name BETWEEN ?1 AND ?1 || '.9');";

	assert(db != NULL);
	reponame = pkgdb_get_reponame(db, repo);

	sql = sbuf_new_auto();
	/*
	 * Working on multiple remote repositories
	 */
	if (reponame == NULL) {
		/* duplicate the query via UNION for all the attached
		 * databases */

		ret = pkgdb_sql_all_attached(db->sqlite, sql,
				basesql, " UNION ALL ");
		if (ret != EPKG_OK) {
			sbuf_delete(sql);
			return (NULL);
		}
	} else
		sbuf_printf(sql, basesql, reponame);

	sbuf_finish(sql);

	pkg_debug(4, "Pkgdb: running '%s'", sbuf_get(sql));
	ret = sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sbuf_get(sql));
		sbuf_delete(sql);
		return (NULL);
	}

	sbuf_delete(sql);

	sqlite3_bind_text(stmt, 1, require, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE));
}

struct pkgdb_it *
pkgdb_find_shlib_require(struct pkgdb *db, const char *provide, const char *repo)
{
	sqlite3_stmt	*stmt;
	struct sbuf	*sql = NULL;
	const char	*reponame = NULL;
	int		 ret;
	const char	 basesql[] = ""
			"SELECT p.id, p.origin, p.name, p.version, p.comment, "
			"p.name || '~' || p.origin as uniqueid, "
			"p.prefix, p.desc, p.arch, p.maintainer, p.www, "
			"p.licenselogic, p.flatsize, p.pkgsize, "
			"p.cksum, p.manifestdigest, p.path AS repopath, '%1$s' AS dbname "
			"FROM '%1$s'.packages AS p INNER JOIN '%1$s'.pkg_shlibs_required AS ps ON "
			"p.id = ps.package_id "
			"WHERE ps.shlib_id = (SELECT id FROM '%1$s'.shlibs WHERE name=?1);";

	assert(db != NULL);
	reponame = pkgdb_get_reponame(db, repo);

	sql = sbuf_new_auto();
	/*
	 * Working on multiple remote repositories
	 */
	if (reponame == NULL) {
		/* duplicate the query via UNION for all the attached
		 * databases */

		ret = pkgdb_sql_all_attached(db->sqlite, sql,
				basesql, " UNION ALL ");
		if (ret != EPKG_OK) {
			sbuf_delete(sql);
			return (NULL);
		}
	} else
		sbuf_printf(sql, basesql, reponame);

	sbuf_finish(sql);

	pkg_debug(4, "Pkgdb: running '%s'", sbuf_get(sql));
	ret = sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sbuf_get(sql));
		sbuf_delete(sql);
		return (NULL);
	}

	sbuf_delete(sql);

	sqlite3_bind_text(stmt, 1, provide, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new(db, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE));
}
