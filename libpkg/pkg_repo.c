/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012 Matthew Seaman <matthew@FreeBSD.org>
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

#include <sys/stat.h>

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

typedef enum _sql_prstmt_index_t {
	PKG = 0,
	DEPS,
	CAT1,
	CAT2,
	LIC1,
	LIC2,
	OPTS,
	SHLIB1,
	SHLIB2,
	EXISTS,
	VERSION,
	DELETE,
	PRSTMT_LAST
} sql_prstmt_index_t;

typedef struct _sql_prstmt_t {
	sqlite3_stmt *stmt;
	const char *sql;
	const char *argtypes;
} sql_prstmt_t;

#define STMT(x) (sql_prepared_statements[(x)].stmt)
#define SQL(x)  (sql_prepared_statements[(x)].sql)

static sql_prstmt_t sql_prepared_statements[PRSTMT_LAST] = {
	/* PKG */
	{
		NULL,
		"INSERT INTO packages ("
		"origin, name, version, comment, desc, arch, maintainer, www, "
		"prefix, pkgsize, flatsize, licenselogic, cksum, path"
		")"
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)",
		"TTTTTTTTTIIITT"
	},
	/* DEPS */
	{
		NULL,
		"INSERT INTO deps (origin, name, version, package_id) "
		"VALUES (?1, ?2, ?3, ?4)",
		"TTTI"
	},
	/* CAT1 */
	{
		NULL,
		"INSERT OR IGNORE INTO categories(name) VALUES(?1)",
		"T"
	},
	/* CAT2 */
	{
		NULL,
		"INSERT OR ROLLBACK INTO pkg_categories(package_id, category_id) "
		"VALUES (?1, (SELECT id FROM categories WHERE name = ?2))",
		"IT"

	},
	/* LIC1 */
	{
		NULL,
		"INSERT OR IGNORE INTO licenses(name) VALUES(?1)",
		"T"
	},
	/* LIC2 */
	{
		NULL,
		"INSERT OR ROLLBACK INTO pkg_licenses(package_id, license_id) "
		"VALUES (?1, (SELECT id FROM licenses WHERE name = ?2))",
		"IT"
	},
	/* OPTS */
	{
		NULL,
		"INSERT OR ROLLBACK INTO options (option, value, package_id) "
		"VALUES (?1, ?2, ?3)",
		"TTI"
	},
	/* SHLIB1 */
	{
		NULL,
		"INSERT OR IGNORE INTO shlibs(name) VALUES(?1)",
		"T"
	},
	/* SHLIB2 */
	{
		NULL, 
		"INSERT OR ROLLBACK INTO pkg_shlibs(package_id, shlib_id) "
		"VALUES (?1, (SELECT id FROM shlibs WHERE name = ?2))",
		"IT"
	},
	/* EXISTS */
	{
		NULL,
		"SELECT count(*) FROM packages WHERE cksum=?1",
		"T"
	},
	/* VERSION */
	{
		NULL,
		"SELECT version FROM packages WHERE origin=?1",
		"T"
	},
	/* DELETE */
	{
		NULL,
		"DELETE FROM packages WHERE origin=?1",
		"T"
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
				pkg_emit_error("%s-%s failed checksum from repository",
				    name, version);
				retcode = EPKG_FATAL;
			} else {
				pkg_emit_error("cached package %s-%s: checksum mismatch, fetching from remote",
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
	char fpath[MAXPATHLEN];
	sqlite3 *db = sqlite3_context_db_handle(ctx);
	char *path = dirname(sqlite3_db_filename(db, "main"));
	if (argc != 1) {
		sqlite3_result_error(ctx, "Need one argument", -1);
		return;
	}

	snprintf(fpath, MAXPATHLEN, "%s/%s", path, sqlite3_value_text(argv[0]));

	if (access(fpath, F_OK) == 0)
			sqlite3_result_int(ctx, 1);
	else
			sqlite3_result_int(ctx, 0);
}

static int
get_repo_user_version(sqlite3 *sqlite, int *repouver)
{
	sqlite3_stmt *stmt;
	int retcode;

	if (sqlite3_prepare_v2(sqlite, "PRAGMA user_version", -1, &stmt, NULL)
	    != SQLITE_OK)
	{
		ERROR_SQLITE(sqlite);
		return (EPKG_FATAL);
	}

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		*repouver = sqlite3_column_int(stmt, 0);
		retcode = EPKG_OK;
	} else {
		*repouver = -1;
		retcode = EPKG_FATAL;
	}
	sqlite3_finalize(stmt);
	return (retcode);
}

static int
initialize_repo(const char *repodb, sqlite3 **sqlite)
{
	bool incremental = false;
	bool db_not_open;
	int repouver;
	int retcode = EPKG_OK;

	const char initsql[] = ""
		"CREATE TABLE packages ("
			"id INTEGER PRIMARY KEY,"
			"origin TEXT UNIQUE,"
			"name TEXT NOT NULL,"
			"version TEXT NOT NULL,"
			"comment TEXT NOT NULL,"
			"desc TEXT NOT NULL,"
			"osversion TEXT,"
			"arch TEXT NOT NULL,"
			"maintainer TEXT NOT NULL,"
			"www TEXT,"
			"prefix TEXT NOT NULL,"
			"pkgsize INTEGER NOT NULL,"
			"flatsize INTEGER NOT NULL,"
			"licenselogic INTEGER NOT NULL,"
			"cksum TEXT NOT NULL,"
			"path TEXT NOT NULL," /* relative path to the package in the repository */
			"pkg_format_version INTEGER"
		");"
		"CREATE TABLE deps ("
			"origin TEXT,"
			"name TEXT,"
			"version TEXT,"
			"package_id INTEGER REFERENCES packages(id)"
		        "  ON DELETE CASCADE ON UPDATE CASCADE,"
			"UNIQUE(package_id, origin)"
		");"
		"CREATE TABLE categories ("
			"id INTEGER PRIMARY KEY, "
			"name TEXT NOT NULL UNIQUE "
		");"
		"CREATE TABLE pkg_categories ("
			"package_id INTEGER REFERENCES packages(id)"
		        "  ON DELETE CASCADE ON UPDATE CASCADE,"
			"category_id INTEGER REFERENCES categories(id)"
			"  ON DELETE RESTRICT ON UPDATE RESTRICT,"
			"UNIQUE(package_id, category_id)"
		");"
		"CREATE TABLE licenses ("
			"id INTEGER PRIMARY KEY,"
			"name TEXT NOT NULL UNIQUE"
		");"
		"CREATE TABLE pkg_licenses ("
			"package_id INTEGER REFERENCES packages(id)"
		        "  ON DELETE CASCADE ON UPDATE CASCADE,"
			"license_id INTEGER REFERENCES licenses(id)"
			"  ON DELETE RESTRICT ON UPDATE RESTRICT,"
			"UNIQUE(package_id, license_id)"
		");"
		"CREATE TABLE options ("
			"package_id INTEGER REFERENCES packages(id)"
		        "  ON DELETE CASCADE ON UPDATE CASCADE,"
			"option TEXT,"
			"value TEXT,"
			"UNIQUE (package_id, option)"
		");"
		"CREATE TABLE shlibs ("
			"id INTEGER PRIMARY KEY,"
			"name TEXT NOT NULL UNIQUE "
		");"
		"CREATE TABLE pkg_shlibs ("
			"package_id INTEGER REFERENCES packages(id)"
		        "  ON DELETE CASCADE ON UPDATE CASCADE,"
			"shlib_id INTEGER REFERENCES shlibs(id)"
			"  ON DELETE RESTRICT ON UPDATE RESTRICT,"
			"UNIQUE(package_id, shlib_id)"
		");"
		"PRAGMA user_version=%d;"
		;

#define REPO_USER_VERSION 3

	if (access(repodb, F_OK) == 0)
		incremental = true;

	sqlite3_initialize();
	db_not_open = true;
	while (db_not_open) {
		if (sqlite3_open(repodb, sqlite) != SQLITE_OK) {
			sqlite3_shutdown();
			return (EPKG_FATAL);
		}

		db_not_open = false;

		/* If the schema is too old, then we cannot do an incremental
		   update.  Delete the existing repo, and promote this to a
		   full update */
		if (incremental) {
			if (get_repo_user_version(*sqlite, &repouver) != EPKG_OK)
				return (EPKG_FATAL);
			if (repouver != REPO_USER_VERSION) {
				pkg_emit_error("updating repo user version "
					"from %d to %d", repouver,
					REPO_USER_VERSION);
				sqlite3_close(*sqlite);
				unlink(repodb);
				incremental = false;
				db_not_open = true;
			}
		}
	}

	sqlite3_create_function(*sqlite, "file_exists", 1, SQLITE_ANY, NULL,
				file_exists, NULL, NULL);

	if ((retcode = sql_exec(*sqlite, "PRAGMA synchronous=off")) != EPKG_OK)
		return (retcode);

	if ((retcode = sql_exec(*sqlite, "PRAGMA journal_mode=memory")) != EPKG_OK)
		return (retcode);

	if ((retcode = sql_exec(*sqlite, "PRAGMA foreign_keys=on")) != EPKG_OK)
		return (retcode);

	if (!incremental && (retcode = sql_exec(*sqlite, initsql, REPO_USER_VERSION)) != EPKG_OK)
		return (retcode);

	if ((retcode = sql_exec(*sqlite, "BEGIN TRANSACTION")) != EPKG_OK)
		return (retcode);

	/* remove anything that is no longer in the repository. */
	if (incremental) {
		sql_exec(*sqlite, "DELETE FROM packages WHERE NOT FILE_EXISTS(path)");
		sql_exec(*sqlite, "DELETE FROM categories WHERE id NOT IN (SELECT category_id FROM pkg_categories);");
		sql_exec(*sqlite, "DELETE FROM licenses WHERE id NOT IN (SELECT license_id FROM pkg_licenses);");
		sql_exec(*sqlite, "DELETE FROM shlibs WHERE id NOT IN (SELECT shlib_id FROM pkg_shlibs);");
	}

	return (EPKG_OK);
}

static int
initialize_prepared_statements(sqlite3 *sqlite)
{
	sql_prstmt_index_t i;
	int retcode = EPKG_OK;

	for (i = 0; i < PRSTMT_LAST; i++)
	{
		if (sqlite3_prepare_v2(sqlite, SQL(i), -1, &STMT(i), NULL)
		    != SQLITE_OK)
		{
			ERROR_SQLITE(sqlite);
			retcode = EPKG_FATAL;
			break;
		}
	}
	return (retcode);
}

static int
run_prepared_statement(sql_prstmt_index_t s, ...)
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
	sql_prstmt_index_t i;

	for (i = 0; i < PRSTMT_LAST; i++)
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
	int ret;
	const char *oversion;

	if (run_prepared_statement(VERSION, origin) != SQLITE_ROW)
		return (EPKG_FATAL); /* sqlite error */
	oversion = sqlite3_column_text(STMT(VERSION), 0);
	switch(pkg_version_cmp(oversion, version))
	{
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

int
pkg_create_repo(char *path, void (progress)(struct pkg *pkg, void *data), void *data)
{
	FTS *fts = NULL;
	FTSENT *ent = NULL;

	struct pkg *pkg = NULL;
	struct pkg_dep *dep = NULL;
	struct pkg_category *category = NULL;
	struct pkg_license *license = NULL;
	struct pkg_option *option = NULL;
	struct pkg_shlib *shlib = NULL;
	struct sbuf *manifest = NULL;
	char *ext = NULL;

	sqlite3 *sqlite = NULL;

	int64_t package_id;
	char *errmsg = NULL;
	int retcode = EPKG_OK;
	char *pkg_path;
	char cksum[SHA256_DIGEST_LENGTH * 2 +1];
	int ret;

	char *repopath[2];
	char repodb[MAXPATHLEN + 1];
	char repopack[MAXPATHLEN + 1];

	struct archive *a = NULL;
	struct archive_entry *ae = NULL;

	if (!is_dir(path)) {
		pkg_emit_error("%s is not a directory", path);
		return (EPKG_FATAL);
	}

	repopath[0] = path;
	repopath[1] = NULL;

	if ((fts = fts_open(repopath, FTS_PHYSICAL, NULL)) == NULL) {
		pkg_emit_errno("fts_open", path);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	snprintf(repodb, sizeof(repodb), "%s/repo.sqlite", path);
	snprintf(repopack, sizeof(repopack), "%s/repo.txz", path);

	if (access(repopack, F_OK) == 0) {
		a = archive_read_new();
		archive_read_support_compression_all(a);
		archive_read_support_format_tar(a);
		if (archive_read_open_filename(a, repopack, 4096) != ARCHIVE_OK) {
			/* if we can't unpack it it won't be useful for us */
			unlink(repopack);
		} else {
			while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
				if (!strcmp(archive_entry_pathname(ae), "repo.sqlite")) {
					archive_entry_set_pathname(ae, repodb);
					archive_read_extract(a, ae, EXTRACT_ARCHIVE_FLAGS);
					break;
				}
			}
		}
		if (a != NULL)
			archive_read_finish(a);
	}

	if ((retcode = initialize_repo(repodb, &sqlite)) != EPKG_OK)
		goto cleanup;

	if ((retcode = initialize_prepared_statements(sqlite)) != EPKG_OK)
		goto cleanup;

	manifest = sbuf_new_auto();
	while ((ent = fts_read(fts)) != NULL) {
		const char *name, *version, *origin, *comment, *desc;
		const char *arch, *maintainer, *www, *prefix;
		int64_t flatsize;
		lic_t licenselogic;

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

		if (strcmp(ent->fts_name, "repo.txz") == 0)
			continue;

		pkg_path = ent->fts_path;
		pkg_path += strlen(path);
		while (pkg_path[0] == '/')
			pkg_path++;

		cksum[0] = '\0';
		sha256_file(ent->fts_accpath, cksum);

		/* do not add if package if already in repodb
		   (possibly at a different pkg_path) */

		if (run_prepared_statement(EXISTS, cksum) != SQLITE_ROW) {
			ERROR_SQLITE(sqlite);
			goto cleanup;
		}
		if (sqlite3_column_int(STMT(EXISTS), 0) > 0) {
			continue;
		}

		if (pkg_open(&pkg, ent->fts_accpath, manifest) != EPKG_OK) {
			retcode = EPKG_WARN;
			continue;
		}

		if (progress != NULL)
			progress(pkg, data);

		pkg_get(pkg, PKG_ORIGIN, &origin, PKG_NAME, &name, PKG_VERSION, &version,
		    PKG_COMMENT, &comment, PKG_DESC, &desc, PKG_ARCH, &arch,
		    PKG_MAINTAINER, &maintainer, PKG_WWW, &www, PKG_PREFIX, &prefix,
		    PKG_FLATSIZE, &flatsize, PKG_LICENSE_LOGIC, &licenselogic);

	try_again:
		if ((ret = run_prepared_statement(PKG, origin, name, version,
		    comment, desc, arch, maintainer, www, prefix,
		    ent->fts_statp->st_size, flatsize, licenselogic, cksum,
		    pkg_path)) != SQLITE_DONE) {
			if (ret == SQLITE_CONSTRAINT) {
				switch(maybe_delete_conflicting(origin,
				    version, pkg_path)) 
				{
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
		while (pkg_deps(pkg, &dep) == EPKG_OK) {
			if (run_prepared_statement(DEPS, 
			    pkg_dep_get(dep, PKG_DEP_ORIGIN),
			    pkg_dep_get(dep, PKG_DEP_NAME),
			    pkg_dep_get(dep, PKG_DEP_VERSION),
			    package_id) != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		}

		category = NULL;
		while (pkg_categories(pkg, &category) == EPKG_OK) {
			if (run_prepared_statement(CAT1,
			    pkg_category_name(category)) != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}

			if (run_prepared_statement(CAT2, package_id,
			    pkg_category_name(category)) != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		}

		license = NULL;
		while (pkg_licenses(pkg, &license) == EPKG_OK) {
			if (run_prepared_statement(LIC1,
			    pkg_license_name(license)) != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}

			if (run_prepared_statement(LIC2, package_id,
			    pkg_license_name(license)) != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		}
		option = NULL;
		while (pkg_options(pkg, &option) == EPKG_OK) {
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
		while (pkg_shlibs(pkg, &shlib) == EPKG_OK) {
			if (run_prepared_statement(SHLIB1,
			    pkg_shlib_name(shlib)) != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}

			if (run_prepared_statement(SHLIB2, package_id,
			    pkg_shlib_name(shlib)) != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		}
	}

	if (sqlite3_exec(sqlite, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK) {
		pkg_emit_error("sqlite: %s", errmsg);
		retcode = EPKG_FATAL;
	}

	cleanup:
	if (fts != NULL)
		fts_close(fts);

	if (pkg != NULL)
		pkg_free(pkg);

	finalize_prepared_statements();

	if (sqlite != NULL)
		sqlite3_close(sqlite);

	if (errmsg != NULL)
		sqlite3_free(errmsg);

	sbuf_free(manifest);

	sqlite3_shutdown();

	return (retcode);
}

int
pkg_finish_repo(char *path, pem_password_cb *password_cb, char *rsa_key_path)
{
	char repo_path[MAXPATHLEN + 1];
	char repo_archive[MAXPATHLEN + 1];
	struct packing *pack;
	unsigned char *sigret = NULL;
	unsigned int siglen = 0;
	
	if (!is_dir(path)) {
	    pkg_emit_error("%s is not a directory", path);
	    return (EPKG_FATAL);
	}

	snprintf(repo_path, sizeof(repo_path), "%s/repo.sqlite", path);
	snprintf(repo_archive, sizeof(repo_archive), "%s/repo", path);

	packing_init(&pack, repo_archive, TXZ);
	if (rsa_key_path != NULL) {
		rsa_sign(repo_path, password_cb, rsa_key_path, &sigret,
				&siglen);

		packing_append_buffer(pack, sigret, "signature", siglen + 1);

		free(sigret);
	}
	packing_append_file_attr(pack, repo_path, "repo.sqlite", "root", "wheel", 0644);
	unlink(repo_path);
	packing_finish(pack);

	return (EPKG_OK);
}
