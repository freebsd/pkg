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
#include <libutil.h>
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
#define REPO_SCHEMA_MINOR 5

#define REPO_SCHEMA_VERSION (REPO_SCHEMA_MAJOR * 1000 + REPO_SCHEMA_MINOR)

typedef enum _sql_prstmt_index {
	PKG = 0,
	DEPS,
	CAT1,
	CAT2,
	LIC1,
	LIC2,
	OPTS,
	SHLIB1,
	SHLIB_REQD,
	SHLIB_PROV,
	ANNOTATE1,
	ANNOTATE2,
	EXISTS,
	VERSION,
	DELETE,
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
	[OPTS] = {
		NULL,
		"INSERT OR ROLLBACK INTO options (option, value, package_id) "
		"VALUES (?1, ?2, ?3)",
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
		"INSERT OR ROLLBACK INTO pkg_annotate(package_id, tag_id, value_id) "
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
		"DELETE FROM packages WHERE origin=?1",
		"T",
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

	snprintf(fpath, MAXPATHLEN, "%s/%s", path, sqlite3_value_text(argv[0]));

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
		ERROR_SQLITE(sqlite);
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

	for (i = 0; i < last; i++)
	{
		ret = sqlite3_prepare_v2(sqlite, SQL(i), -1, &STMT(i), NULL);
		if (ret != SQLITE_OK) {
			ERROR_SQLITE(sqlite);
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

static void
finalize_prepared_statements(void)
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

	retcode = sql_exec(sqlite, "PRAGMA synchronous=off");
	if (retcode != EPKG_OK)
		return (retcode);

	retcode = sql_exec(sqlite, "PRAGMA journal_mode=memory");
	if (retcode != EPKG_OK)
		return (retcode);

	retcode = sql_exec(sqlite, "PRAGMA foreign_keys=on");
	if (retcode != EPKG_OK)
		return (retcode);

	retcode = initialize_prepared_statements(sqlite);
	if (retcode != EPKG_OK)
		return (retcode);

	retcode = pkgdb_transaction_begin(sqlite, NULL);
	if (retcode != EPKG_OK)
		return (retcode);

	return (EPKG_OK);
}

int
pkgdb_repo_close(sqlite3 *sqlite, bool commit)
{
	int retcode = EPKG_OK;

	if (commit) {
		if (pkgdb_transaction_commit(sqlite, NULL) != SQLITE_OK)
			retcode = EPKG_FATAL;
	}
	else {
		if (pkgdb_transaction_rollback(sqlite, NULL) != SQLITE_OK)
				retcode = EPKG_FATAL;
	}
	finalize_prepared_statements();

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

			if (run_prepared_statement(DELETE, origin) != SQLITE_DONE)
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
		if (run_prepared_statement(DELETE, origin) != SQLITE_DONE)
			return (EPKG_FATAL); /* sqlite error */

		ret = EPKG_OK;
	}
	return (ret);
}

int
pkgdb_repo_cksum_exists(sqlite3 *sqlite, const char *cksum)
{
	if (run_prepared_statement(EXISTS, cksum) != SQLITE_ROW) {
		ERROR_SQLITE(sqlite);
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
	lic_t			 licenselogic;
	int			 ret;
	struct pkg_dep		*dep      = NULL;
	struct pkg_category	*category = NULL;
	struct pkg_license	*license  = NULL;
	struct pkg_option	*option   = NULL;
	struct pkg_shlib	*shlib    = NULL;
	struct pkg_note		*note     = NULL;
	int64_t			 package_id;

	pkg_get(pkg, PKG_ORIGIN, &origin, PKG_NAME, &name,
			    PKG_VERSION, &version, PKG_COMMENT, &comment,
			    PKG_DESC, &desc, PKG_ARCH, &arch,
			    PKG_MAINTAINER, &maintainer, PKG_WWW, &www,
			    PKG_PREFIX, &prefix, PKG_FLATSIZE, &flatsize,
			    PKG_LICENSE_LOGIC, &licenselogic, PKG_CKSUM, &sum,
			    PKG_NEW_PKGSIZE, &pkgsize, PKG_REPOPATH, &rpath);

try_again:
	if ((ret = run_prepared_statement(PKG, origin, name, version,
			comment, desc, arch, maintainer, www, prefix,
			pkgsize, flatsize, (int64_t)licenselogic, sum,
			rpath, manifest_digest)) != SQLITE_DONE) {
		if (ret == SQLITE_CONSTRAINT) {
			switch(maybe_delete_conflicting(origin,
					version, pkg_path, forced)) {
			case EPKG_FATAL: /* sqlite error */
				ERROR_SQLITE(sqlite);
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
			ERROR_SQLITE(sqlite);
			return (EPKG_FATAL);
		}
	}
	package_id = sqlite3_last_insert_rowid(sqlite);

	dep = NULL;
	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		if (run_prepared_statement(DEPS,
				pkg_dep_origin(dep),
				pkg_dep_name(dep),
				pkg_dep_version(dep),
				package_id) != SQLITE_DONE) {
			ERROR_SQLITE(sqlite);
			return (EPKG_FATAL);
		}
	}

	category = NULL;
	while (pkg_categories(pkg, &category) == EPKG_OK) {
		const char *cat_name = pkg_category_name(category);

		ret = run_prepared_statement(CAT1, cat_name);
		if (ret == SQLITE_DONE)
			ret = run_prepared_statement(CAT2, package_id,
					cat_name);
		if (ret != SQLITE_DONE)
		{
			ERROR_SQLITE(sqlite);
			return (EPKG_FATAL);
		}
	}

	license = NULL;
	while (pkg_licenses(pkg, &license) == EPKG_OK) {
		const char *lic_name = pkg_license_name(license);

		ret = run_prepared_statement(LIC1, lic_name);
		if (ret == SQLITE_DONE)
			ret = run_prepared_statement(LIC2, package_id,
					lic_name);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite);
			return (EPKG_FATAL);
		}
	}
	option = NULL;
	while (pkg_options(pkg, &option) == EPKG_OK) {
		if (run_prepared_statement(OPTS,
				pkg_option_opt(option),
				pkg_option_value(option),
				package_id) != SQLITE_DONE) {
			ERROR_SQLITE(sqlite);
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
			ERROR_SQLITE(sqlite);
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
			ERROR_SQLITE(sqlite);
			return (EPKG_FATAL);
		}
	}

	note = NULL;
	while (pkg_annotations(pkg, &note) == EPKG_OK) {
		const char *note_tag = pkg_annotation_tag(note);
		const char *note_val = pkg_annotation_value(note);

		ret = run_prepared_statement(ANNOTATE1, note_tag);
		if (ret == SQLITE_DONE) 
			ret = run_prepared_statement(ANNOTATE1, note_val);
		if (ret == SQLITE_DONE)
			ret = run_prepared_statement(ANNOTATE2, package_id,
				  note_tag, note_val);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite);
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

int
pkgdb_repo_remove_package(const char *origin)
{
	if (run_prepared_statement(DELETE, origin) != SQLITE_DONE)
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
	bool			 found = false;
	int			 ret = EPKG_OK;
	char			 sql[BUFSIZ];
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
	if (ret == EPKG_OK)
		ret = pkgdb_transaction_begin(db->sqlite, NULL);

	/* apply change */
	if (ret == EPKG_OK) {
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
	if (ret == EPKG_OK)
		ret = pkgdb_transaction_commit(db->sqlite, NULL);
	else
		pkgdb_transaction_rollback(db->sqlite, NULL);

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
		"SELECT origin, manifestdigest "
		"FROM packages "
		"ORDER BY origin;";

	ret = sqlite3_prepare_v2(sqlite, query_sql, -1,
			&stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(sqlite);
		return (NULL);
	}
	repodb.sqlite = sqlite;
	repodb.type = PKGDB_REMOTE;

	return pkgdb_it_new(&repodb, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE);
}
