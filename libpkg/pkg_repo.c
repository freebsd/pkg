/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#include <archive_entry.h>
#include <assert.h>
#include <fts.h>
#include <libgen.h>
#include <sqlite3.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/repodb.h"
#include "private/thd_repo.h"

/* The package repo schema major revision */
#define REPO_SCHEMA_MAJOR 2

/* The package repo schema minor revision.
   Minor schema changes don't prevent older pkgng
   versions accessing the repo. */
#define REPO_SCHEMA_MINOR 3

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
	ABSTRACT1,
	ABSTRACT2,
	EXISTS,
	VERSION,
	DELETE,
	FILES,
	PRSTMT_LAST,
} sql_prstmt_index;

static sql_prstmt sql_prepared_statements[PRSTMT_LAST] = {
	[PKG] = {
		NULL,
		"INSERT INTO packages ("
		"origin, name, version, comment, desc, arch, maintainer, www, "
		"prefix, pkgsize, flatsize, licenselogic, cksum, path"
		")"
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)",
		"TTTTTTTTTIIITT",
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
	[ABSTRACT1] = {
		NULL,
		"INSERT OR IGNORE INTO abstract(abstract) "
		"VALUES (?1)",
		"T",
	},
	[ABSTRACT2] = {
		NULL,
		"INSERT OR ROLLBACK INTO pkg_abstract(package_id, key_id, value_id) "
		"VALUES (?1,"
		" (SELECT abstract_id FROM abstract WHERE abstract=?2),"
		" (SELECT abstract_id FROM abstract WHERE abstract=?3))",
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
	},
	[FILES] = {
		NULL,
		"INSERT OR ROLLBACK INTO files.files(path, package_id) "
		"VALUES (?1, ?2)",
		"IT",
	}
	/* PRSTMT_LAST */
};

int
pkg_repo_fetch(struct pkg *pkg)
{
	char dest[MAXPATHLEN + 1];
	char url[MAXPATHLEN + 1];
	int fetched = 0;
	char cksum[SHA256_DIGEST_LENGTH * 2 +1];
	char *path = NULL;
	const char *packagesite = NULL;
	const char *cachedir = NULL;
	bool multirepos_enabled = false;
	int retcode = EPKG_OK;
	const char *repopath, *repourl, *sum, *name, *version;

	assert((pkg->type & PKG_REMOTE) == PKG_REMOTE);

	if (pkg_config_string(PKG_CONFIG_CACHEDIR, &cachedir) != EPKG_OK)
		return (EPKG_FATAL);

	pkg_get(pkg, PKG_REPOPATH, &repopath, PKG_REPOURL, &repourl,
	    PKG_CKSUM, &sum, PKG_NAME, &name, PKG_VERSION, &version);

	snprintf(dest, sizeof(dest), "%s/%s", cachedir, repopath);

	/* If it is already in the local cachedir, dont bother to
	 * download it */
	if (access(dest, F_OK) == 0)
		goto checksum;

	/* Create the dirs in cachedir */
	if ((path = dirname(dest)) == NULL) {
		pkg_emit_errno("dirname", dest);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if ((retcode = mkdirs(path)) != EPKG_OK)
		goto cleanup;

	/*
	 * In multi-repos the remote URL is stored in pkg[PKG_REPOURL]
	 * For a single attached database the repository URL should be
	 * defined by PACKAGESITE.
	 */
	pkg_config_bool(PKG_CONFIG_MULTIREPOS, &multirepos_enabled);

	if (multirepos_enabled) {
		packagesite = repourl;
	} else {
		pkg_config_string(PKG_CONFIG_REPO, &packagesite);
	}

	if (packagesite == NULL || packagesite[0] == '\0') {
		pkg_emit_error("PACKAGESITE is not defined");
		retcode = 1;
		goto cleanup;
	}

	if (packagesite[strlen(packagesite) - 1] == '/')
		snprintf(url, sizeof(url), "%s%s", packagesite, repopath);
	else
		snprintf(url, sizeof(url), "%s/%s", packagesite, repopath);

	retcode = pkg_fetch_file(url, dest, 0);
	fetched = 1;

	if (retcode != EPKG_OK)
		goto cleanup;

	checksum:
	retcode = sha256_file(dest, cksum);
	if (retcode == EPKG_OK)
		if (strcmp(cksum, sum)) {
			if (fetched == 1) {
				pkg_emit_error("%s-%s failed checksum "
				    "from repository", name, version);
				retcode = EPKG_FATAL;
			} else {
				pkg_emit_error("cached package %s-%s: "
				    "checksum mismatch, fetching from remote",
				    name, version);
				unlink(dest);
				return (pkg_repo_fetch(pkg));
			}
		}

	cleanup:
	if (retcode != EPKG_OK)
		unlink(dest);

	return (retcode);
}

static void
file_exists(sqlite3_context *ctx, int argc, __unused sqlite3_value **argv)
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

	if (access(fpath, F_OK) == 0) {
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
set_repo_user_version(sqlite3 *sqlite, const char *database, int reposcver)
{
	int		 retcode = EPKG_OK;
	char		 sql[BUFSIZ];
	char		*errmsg;
	const char	*fmt = "PRAGMA %Q.user_version = %" PRId64 ";" ;

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
initialize_repo(const char *repodb, const char *filesdb, bool force,
    bool files, sqlite3 **sqlite)
{
	bool incremental = false;
	bool db_not_open;
	int reposcver;
	int retcode = EPKG_OK;

	const char initfilessql[] = ""
		"CREATE TABLE files.files ("
			"path TEXT,"
			"package_id INTEGER"
		");";

	if (access(repodb, F_OK) == 0) {
		if (files) {
			if (access(filesdb, F_OK) != 0) {
				unlink(repodb);
			} else {
				incremental = true;
			}
		} else {
			incremental = true;
		}
	}

	sqlite3_initialize();
	db_not_open = true;
	while (db_not_open) {
		if (sqlite3_open(repodb, sqlite) != SQLITE_OK) {
			sqlite3_shutdown();
			return (EPKG_FATAL);
		}

		if (sql_exec(*sqlite, "ATTACH '%s' AS 'files';", filesdb) != EPKG_OK) {
			pkg_emit_error("Unable to reate files database");
			sqlite3_close(*sqlite);
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
				pkg_emit_error("updating repo schema version "
				     "from %d to %d", reposcver,
				     REPO_SCHEMA_VERSION);
			sqlite3_close(*sqlite);
			unlink(repodb);
			if (files)
				unlink(filesdb);
			incremental = false;
			db_not_open = true;
		}
	}

	sqlite3_create_function(*sqlite, "file_exists", 2, SQLITE_ANY, NULL,
	    file_exists, NULL, NULL);

	retcode = sql_exec(*sqlite, "PRAGMA synchronous=off");
	if (retcode != EPKG_OK)
		return (retcode);

	retcode = sql_exec(*sqlite, "PRAGMA journal_mode=memory");
	if (retcode != EPKG_OK)
		return (retcode);

	retcode = sql_exec(*sqlite, "PRAGMA foreign_keys=on");
	if (retcode != EPKG_OK)
		return (retcode);

	if (!incremental) {
		retcode = sql_exec(*sqlite, initsql, REPO_SCHEMA_VERSION);
		if (retcode != EPKG_OK)
			return (retcode);

		if (files) {
			retcode = sql_exec(*sqlite, initfilessql);
			if (retcode != EPKG_OK)
				return (retcode);
		}
	}

	retcode = pkgdb_transaction_begin(*sqlite, NULL);
	if (retcode != EPKG_OK)
		return (retcode);

	/* remove anything that is no longer in the repository. */
	if (incremental) {
		const char *obsolete[] = {
			"packages WHERE NOT FILE_EXISTS(path, cksum)",
			"categories WHERE id NOT IN "
				"(SELECT category_id FROM pkg_categories)",
			"licenses WHERE id NOT IN "
				"(SELECT license_id FROM pkg_licenses)",
			"shlibs WHERE id NOT IN "
				"(SELECT shlib_id FROM pkg_shlibs_required)"
			        "AND id NOT IN "
				"(SELECT shlib_id FROM pkg_shlibs_provided)"
		};
		const char filesobsolete[] = "files.files where package_id NOT IN "
			"(SELECT id from packages)";
		size_t num_objs = sizeof(obsolete) / sizeof(*obsolete);
		for (size_t obj = 0; obj < num_objs; obj++)
			sql_exec(*sqlite, "DELETE FROM %s;", obsolete[obj]);
		if (files)
			sql_exec(*sqlite, "DELETE FROM %s;", filesobsolete);
	}

	return (EPKG_OK);
}

static int
initialize_prepared_statements(sqlite3 *sqlite, bool files)
{
	sql_prstmt_index i, last;
	int ret;

	last = files ? PRSTMT_LAST : PRSTMT_LAST - 1;

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
finalize_prepared_statements(bool files)
{
	sql_prstmt_index i, last;

	last = files ? PRSTMT_LAST : PRSTMT_LAST - 1;

	for (i = 0; i < last; i++)
	{
		if (STMT(i) != NULL) {
			sqlite3_finalize(STMT(i));
			STMT(i) = NULL;
		}
	}
	return;
}

static int
maybe_delete_conflicting(const char *origin, const char *version,
			 const char *pkg_path)
{
	int ret = EPKG_FATAL;
	const char *oversion;

	if (run_prepared_statement(VERSION, origin) != SQLITE_ROW)
		return (EPKG_FATAL); /* sqlite error */
	oversion = sqlite3_column_text(STMT(VERSION), 0);
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
	return (ret);	
}

static void
pack_extract(const char *pack, const char *dbname, const char *dbpath)
{
	struct archive *a = NULL;
	struct archive_entry *ae = NULL;

	if (access(pack, F_OK) != 0)
		return;

	a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_tar(a);
	if (archive_read_open_filename(a, pack, 4096) != ARCHIVE_OK) {
		/* if we can't unpack it it won't be useful for us */
		unlink(pack);
		archive_read_free(a);
		return;
	}

	while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
		if (strcmp(archive_entry_pathname(ae), dbname) == 0) {
			archive_entry_set_pathname(ae, dbpath);
			archive_read_extract(a, ae, EXTRACT_ARCHIVE_FLAGS);
			break;
		}
	}

	archive_read_free(a);


}
int
pkg_create_repo(char *path, bool force, bool files,
    void (progress)(struct pkg *pkg, void *data), void *data)
{
	FTS *fts = NULL;
	struct thd_data thd_data;
	int num_workers;
	size_t len;
	pthread_t *tids = NULL;

	struct pkg_file *f = NULL;
	struct pkg_dep *dep = NULL;
	struct pkg_category *category = NULL;
	struct pkg_license *license = NULL;
	struct pkg_option *option = NULL;
	struct pkg_shlib *shlib = NULL;

	sqlite3 *sqlite = NULL;

	int64_t package_id;
	char *errmsg = NULL;
	int retcode = EPKG_OK;
	int ret;

	char *repopath[2];
	char repodb[MAXPATHLEN + 1];
	char repopack[MAXPATHLEN + 1];
	char filesdb[MAXPATHLEN + 1];
	char filespack[MAXPATHLEN + 1];

	if (!is_dir(path)) {
		pkg_emit_error("%s is not a directory", path);
		return (EPKG_FATAL);
	}

	repopath[0] = path;
	repopath[1] = NULL;

	len = sizeof(num_workers);
	if (sysctlbyname("hw.ncpu", &num_workers, &len, NULL, 0) == -1)
		num_workers = 6;

	if ((fts = fts_open(repopath, FTS_PHYSICAL|FTS_NOCHDIR, NULL)) == NULL) {
		pkg_emit_errno("fts_open", path);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	snprintf(repodb, sizeof(repodb), "%s/repo.sqlite", path);
	snprintf(repopack, sizeof(repopack), "%s/repo.txz", path);
	if (files) {
		snprintf(filesdb, sizeof(filesdb), "%s/files.sqlite", path);
		snprintf(filespack, sizeof(filespack), "%s/files.txz", path);
	}

	pack_extract(repopack, "repo.sqlite", repodb);
	if (files)
		pack_extract(filespack, "files.sqlite", filesdb);

	if ((retcode = initialize_repo(repodb, filesdb, force, files, &sqlite)) != EPKG_OK)
		goto cleanup;

	if ((retcode = initialize_prepared_statements(sqlite, files)) != EPKG_OK)
		goto cleanup;

	thd_data.root_path = path;
	thd_data.max_results = num_workers;
	thd_data.num_results = 0;
	thd_data.stop = false;
	thd_data.fts = fts;
	pthread_mutex_init(&thd_data.fts_m, NULL);
	thd_data.results = NULL;
	thd_data.thd_finished = 0;
	pthread_mutex_init(&thd_data.results_m, NULL);
	pthread_cond_init(&thd_data.has_result, NULL);
	pthread_cond_init(&thd_data.has_room, NULL);

	/* Launch workers */
	tids = calloc(num_workers, sizeof(pthread_t));
	for (int i = 0; i < num_workers; i++) {
		pthread_create(&tids[i], NULL, (void *)&read_pkg_file, &thd_data);
	}

	for (;;) {
		struct pkg_result *r;

		const char *name, *version, *origin, *comment, *desc;
		const char *arch, *maintainer, *www, *prefix;
		int64_t flatsize;
		lic_t licenselogic;

		pthread_mutex_lock(&thd_data.results_m);
		while ((r = thd_data.results) == NULL) {
			if (thd_data.thd_finished == num_workers) {
				break;
			}
			pthread_cond_wait(&thd_data.has_result, &thd_data.results_m);
		}
		if (r != NULL) {
			LL_DELETE(thd_data.results, thd_data.results);
			thd_data.num_results--;
			pthread_cond_signal(&thd_data.has_room);
		}
		pthread_mutex_unlock(&thd_data.results_m);
		if (r == NULL) {
			break;
		}

		if (r->retcode != EPKG_OK) {
			continue;
		}

		/* do not add if package if already in repodb
		   (possibly at a different pkg_path) */

		if (run_prepared_statement(EXISTS, r->cksum) != SQLITE_ROW) {
			ERROR_SQLITE(sqlite);
			goto cleanup;
		}
		if (sqlite3_column_int(STMT(EXISTS), 0) > 0) {
			continue;
		}

		if (progress != NULL)
			progress(r->pkg, data);

		pkg_get(r->pkg, PKG_ORIGIN, &origin, PKG_NAME, &name,
		    PKG_VERSION, &version, PKG_COMMENT, &comment,
		    PKG_DESC, &desc, PKG_ARCH, &arch,
		    PKG_MAINTAINER, &maintainer, PKG_WWW, &www,
		    PKG_PREFIX, &prefix, PKG_FLATSIZE, &flatsize,
		    PKG_LICENSE_LOGIC, &licenselogic);

	try_again:
		if ((ret = run_prepared_statement(PKG, origin, name, version,
		    comment, desc, arch, maintainer, www, prefix,
		    r->size, flatsize, (int64_t)licenselogic, r->cksum,
		    r->path)) != SQLITE_DONE) {
			if (ret == SQLITE_CONSTRAINT) {
				switch(maybe_delete_conflicting(origin,
				    version, r->path)) {
				case EPKG_FATAL: /* sqlite error */
					ERROR_SQLITE(sqlite);
					retcode = EPKG_FATAL;
					goto cleanup;
					break;
				case EPKG_END: /* repo already has newer */
					continue;
					break;
				default: /* conflict cleared, try again */
					goto try_again;
					break;
				}
			} else {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		}

		package_id = sqlite3_last_insert_rowid(sqlite);

		dep = NULL;
		while (pkg_deps(r->pkg, &dep) == EPKG_OK) {
			if (run_prepared_statement(DEPS,
			    pkg_dep_origin(dep),
			    pkg_dep_name(dep),
			    pkg_dep_version(dep),
			    package_id) != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		}

		category = NULL;
		while (pkg_categories(r->pkg, &category) == EPKG_OK) {
			const char *cat_name = pkg_category_name(category);

			ret = run_prepared_statement(CAT1, cat_name);
			if (ret == SQLITE_DONE)
			    ret = run_prepared_statement(CAT2, package_id,
			        cat_name);
			if (ret != SQLITE_DONE)
			{
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		}

		license = NULL;
		while (pkg_licenses(r->pkg, &license) == EPKG_OK) {
			const char *lic_name = pkg_license_name(license);

			ret = run_prepared_statement(LIC1, lic_name);
			if (ret == SQLITE_DONE)
				ret = run_prepared_statement(LIC2, package_id,
				    lic_name);
			if (ret != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		}
		option = NULL;
		while (pkg_options(r->pkg, &option) == EPKG_OK) {
			if (run_prepared_statement(OPTS,
			    pkg_option_opt(option),
			    pkg_option_value(option),
			    package_id) != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		}

		shlib = NULL;
		while (pkg_shlibs_required(r->pkg, &shlib) == EPKG_OK) {
			const char *shlib_name = pkg_shlib_name(shlib);

			ret = run_prepared_statement(SHLIB1, shlib_name);
			if (ret == SQLITE_DONE)
			    ret = run_prepared_statement(SHLIB_REQD, package_id,
			        shlib_name);
			if (ret != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		}

		shlib = NULL;
		while (pkg_shlibs_provided(r->pkg, &shlib) == EPKG_OK) {
			const char *shlib_name = pkg_shlib_name(shlib);

			ret = run_prepared_statement(SHLIB1, shlib_name);
			if (ret == SQLITE_DONE)
			    ret = run_prepared_statement(SHLIB_PROV, package_id,
			        shlib_name);
			if (ret != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		}

		if (files) {
			f = NULL;
			while (pkg_files(r->pkg, &f) == EPKG_OK) {
				const char *f_path = pkg_file_path(f);
				ret = run_prepared_statement(FILES, package_id, f_path);
				if (ret != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
				}
			}
		}

		pkg_free(r->pkg);
		free(r);
	}

	if (pkgdb_transaction_commit(sqlite, NULL) != SQLITE_OK)
		retcode = EPKG_FATAL;

	cleanup:

	if (tids != NULL) {
		// Cancel running threads
		if (retcode != EPKG_OK) {
			pthread_mutex_lock(&thd_data.fts_m);
			thd_data.stop = true;
			pthread_mutex_unlock(&thd_data.fts_m);
		}
		// Join on threads to release thread IDs
		for (int i = 0; i < num_workers; i++) {
			pthread_join(tids[i], NULL);
		}
		free(tids);
	}

	if (fts != NULL)
		fts_close(fts);

	finalize_prepared_statements(files);

	if (sqlite != NULL)
		sqlite3_close(sqlite);

	if (errmsg != NULL)
		sqlite3_free(errmsg);

	sqlite3_shutdown();

	return (retcode);
}

void
read_pkg_file(void *data)
{
	struct thd_data *d = (struct thd_data*) data;
	struct pkg_result *r;

	FTSENT *fts_ent = NULL;
	char fts_accpath[MAXPATHLEN + 1];
	char fts_path[MAXPATHLEN + 1];
	char fts_name[MAXPATHLEN + 1];
	off_t st_size;
	int fts_info;

	char *ext = NULL;
	char *pkg_path;

	for (;;) {
		fts_ent = NULL;

		/*
		 * Get a file to read from.
		 * Copy the data we need from the fts entry localy because as soon as
		 * we unlock the fts_m mutex, we can not access it.
		 */
		pthread_mutex_lock(&d->fts_m);
		if (!d->stop)
			fts_ent = fts_read(d->fts);
		if (fts_ent != NULL) {
			strlcpy(fts_accpath, fts_ent->fts_accpath, sizeof(fts_accpath));
			strlcpy(fts_path, fts_ent->fts_path, sizeof(fts_path));
			strlcpy(fts_name, fts_ent->fts_name, sizeof(fts_name));
			st_size = fts_ent->fts_statp->st_size;
			fts_info = fts_ent->fts_info;
		}
		pthread_mutex_unlock(&d->fts_m);

		// There is no more jobs, exit the main loop.
		if (fts_ent == NULL)
			break;

		/* skip everything that is not a file */
		if (fts_info != FTS_F)
			continue;

		ext = strrchr(fts_name, '.');

		if (ext == NULL)
			continue;

		if (strcmp(ext, ".tgz") != 0 &&
				strcmp(ext, ".tbz") != 0 &&
				strcmp(ext, ".txz") != 0 &&
				strcmp(ext, ".tar") != 0)
			continue;

		if (strcmp(fts_name, "repo.txz") == 0)
			continue;

		pkg_path = fts_path;
		pkg_path += strlen(d->root_path);
		while (pkg_path[0] == '/')
			pkg_path++;

		r = calloc(1, sizeof(struct pkg_result));
		strlcpy(r->path, pkg_path, sizeof(r->path));
		r->size = st_size;

		sha256_file(fts_accpath, r->cksum);

		if (pkg_open(&r->pkg, fts_accpath) != EPKG_OK) {
			r->retcode = EPKG_WARN;
		}

		/* Add result to the FIFO and notify */
		pthread_mutex_lock(&d->results_m);
		while (d->num_results >= d->max_results) {
			pthread_cond_wait(&d->has_room, &d->results_m);
		}
		LL_APPEND(d->results, r);
		d->num_results++;
		pthread_cond_signal(&d->has_result);
		pthread_mutex_unlock(&d->results_m);
	}

	/*
	 * This thread is about to exit.
	 * Notify the main thread that we are done.
	 */
	pthread_mutex_lock(&d->results_m);
	d->thd_finished++;
	pthread_cond_signal(&d->has_result);
	pthread_mutex_unlock(&d->results_m);
}

static void
pack_db(const char *name, const char *archive, char *path,
    char *rsa_key_path, pem_password_cb *password_cb)
{
	struct packing *pack;
	unsigned char *sigret = NULL;
	unsigned int siglen = 0;

	packing_init(&pack, archive, TXZ);
	if (rsa_key_path != NULL) {
		rsa_sign(path, password_cb, rsa_key_path, &sigret, &siglen);

		packing_append_buffer(pack, sigret, "signature", siglen + 1);

		free(sigret);
	}
	packing_append_file_attr(pack, path, name, "root", "wheel", 0644);
	unlink(path);
	packing_finish(pack);
}

int
pkg_finish_repo(char *path, pem_password_cb *password_cb, char *rsa_key_path)
{
	char files_path[MAXPATHLEN + 1];
	char files_archive[MAXPATHLEN + 1];
	char repo_path[MAXPATHLEN + 1];
	char repo_archive[MAXPATHLEN + 1];
	
	if (!is_dir(path)) {
	    pkg_emit_error("%s is not a directory", path);
	    return (EPKG_FATAL);
	}

	snprintf(repo_path, sizeof(repo_path), "%s/repo.sqlite", path);
	snprintf(repo_archive, sizeof(repo_archive), "%s/repo", path);
	snprintf(files_path, sizeof(repo_path), "%s/files.sqlite", path);
	snprintf(files_archive, sizeof(repo_archive), "%s/files", path);

	pack_db("repo.sqlite", repo_archive, repo_path, rsa_key_path, password_cb);
	pack_db("files.sqlite", files_archive, files_path, rsa_key_path, password_cb);

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
	size_t	 len;
	int	 ret = EPKG_OK;

	tofree = f = strdup(fmt);
	if (tofree == NULL)
		return EPKG_FATAL; /* out of memory */

	sqlbuf[0] = '\0';

	while ((f0 = strsep(&f, "%")) != NULL) {
		len = strlcat(sqlbuf, f0, buflen);
		if (len >= buflen) {
			/* Overflowed the buffer */
			ret = EPKG_FATAL;
			break;
		}

		if (f[0] == 'Q') {
			len = strlcat(sqlbuf, replacement, buflen);
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

	return (ret);
}


static int
apply_repo_change(struct pkgdb *db, const char *database,
		  struct repo_changes *repo_changes, const char *updown,
		  int version)
{
	struct repo_changes	*change;
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
		pkg_emit_error("Failed to %s \"%s\" repo schema to "
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
	if (ret == EPKG_OK)
		ret = set_repo_user_version(db->sqlite, database, version);

	/* commit or rollback */
	if (ret == EPKG_OK)
		ret = pkgdb_transaction_commit(db->sqlite, NULL);
	else
		ret = pkgdb_transaction_rollback(db->sqlite, NULL);

	if (ret == EPKG_OK) {
		pkg_emit_notice("Repo %s schema %s to version %d: %s",
				database, version, change->message);
	}

	return (ret);
}

static int
upgrade_repo_schema(struct pkgdb *db, const char *database, int current_version)
{
	int version;
	int ret = EPKG_OK;

	for (version = current_version;
	     version <= REPO_SCHEMA_VERSION;
	     version++)  {
		ret = apply_repo_change(db, database, repo_upgrades,
					"upgrade", version);
		if (ret != EPKG_OK)
			break;
	}
	return (ret);
}

static int
downgrade_repo_schema(struct pkgdb *db, const char *database, int current_version)
{
	int version;
	int ret = EPKG_OK;

	for (version = current_version - 1;
	     version >= REPO_SCHEMA_VERSION;
	     version--)  {
		ret = apply_repo_change(db, database, repo_downgrades,
					"downgrade", version);
		if (ret != EPKG_OK)
			break;
	}
	return (ret);
}

int
pkg_check_repo_version(struct pkgdb *db, const char *database)
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
