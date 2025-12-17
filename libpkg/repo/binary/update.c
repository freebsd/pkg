/*
 * Copyright (c) 2014, Vsevolod Stakhov
 * Copyright (c) 2012-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2024 Serenity Cyber Security, LLC
 *                    Author: Gleb Popov <arrowd@FreeBSD.org>
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

#include <sys/stat.h>
#include <sys/param.h>
#include <sys/file.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>

#include <archive.h>
#include <archive_entry.h>

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkgdb.h"
#include "private/pkg.h"
#include "private/json.h"
#include "binary.h"
#include "binary_private.h"

static int
pkg_repo_binary_init_update(struct pkg_repo *repo)
{
	sqlite3 *sqlite;
	const char update_check_sql[] = ""
					"INSERT INTO repo_update VALUES(1);";
	const char update_start_sql[] = ""
					"CREATE TABLE IF NOT EXISTS repo_update (n INT);";

	/* [Re]create repo */
	if (repo->ops->create(repo) != EPKG_OK) {
		pkg_emit_notice("Unable to create repository %s", repo->name);
		return (EPKG_FATAL);
	}
	if (repo->ops->open(repo, R_OK|W_OK) != EPKG_OK) {
		pkg_emit_notice("Unable to open created repository %s", repo->name);
		return (EPKG_FATAL);
	}

	repo->ops->init(repo);

	sqlite = PRIV_GET(repo);

	if(sqlite3_exec(sqlite, update_check_sql, NULL, NULL, NULL) == SQLITE_OK) {
		pkg_emit_notice("Previous update has not been finished, restart it");
		return (EPKG_END);
	}
	else {
		sql_exec(sqlite, update_start_sql);
	}

	return (EPKG_OK);
}

static int
pkg_repo_binary_delete_conflicting(const char *origin, const char *version,
    bool forced)
{
	int ret = EPKG_FATAL;
	const unsigned char *oversion;

	sql_arg_t arg[] = { SQL_ARG(origin) };
	sql_arg_t args[] = { SQL_ARG(origin), SQL_ARG(origin) };
	if (pkg_repo_binary_run_prstatement(REPO_VERSION, arg, NELEM(arg)) != SQLITE_ROW) {
		ret = EPKG_FATAL;
		goto cleanup;
	}
	oversion = sqlite3_column_text(pkg_repo_binary_stmt_prstatement(REPO_VERSION), 0);
	if (!forced) {
		switch(pkg_version_cmp(oversion, version)) {
		case -1:
			pkg_emit_error("duplicate package origin: replacing older "
					"version %s in repo with package %s",
					oversion, origin);

			if (pkg_repo_binary_run_prstatement(DELETE, args, NELEM(args)) !=
							SQLITE_DONE)
				ret = EPKG_FATAL;
			else
				ret = EPKG_OK;	/* conflict cleared */

			break;
		case 0:
		case 1:
			pkg_emit_error("duplicate package origin: package %s is not "
					"newer than version %s already in repo",
					origin, oversion);
			ret = EPKG_END;	/* keep what is already in the repo */
			break;
		}
	}
	else {
		ret = EPKG_OK;
		if (pkg_repo_binary_run_prstatement(DELETE, args, NELEM(args)) != SQLITE_DONE)
			ret = EPKG_FATAL;
	}

cleanup:
	sqlite3_reset(pkg_repo_binary_stmt_prstatement(REPO_VERSION));

	return (ret);
}

static int
pkg_repo_binary_add_pkg(struct pkg *pkg, sqlite3 *sqlite, bool forced)
{
	int			 ret;
	struct pkg_dep		*dep      = NULL;
	struct pkg_option	*option   = NULL;
	struct pkg_kv		*kv;
	const char		*arch;
	int64_t			 package_id;

	arch = pkg->abi != NULL ? pkg->abi : pkg->altabi;

	sql_arg_t args[] = {
		SQL_ARG(pkg->origin),
		SQL_ARG(pkg->name),
		SQL_ARG(pkg->version),
		SQL_ARG(pkg->comment),
		SQL_ARG(pkg->desc),
		SQL_ARG(arch),
		SQL_ARG(pkg->maintainer),
		SQL_ARG(pkg->www),
		SQL_ARG(pkg->prefix),
		SQL_ARG(pkg->pkgsize),
		SQL_ARG(pkg->flatsize),
		SQL_ARG(pkg->licenselogic),
		SQL_ARG(pkg->sum),
		SQL_ARG(pkg->repopath),
		SQL_ARG(pkg->digest),
		SQL_ARG(pkg->old_digest),
		SQL_ARG(pkg->vital),
	};
try_again:
	if ((ret = pkg_repo_binary_run_prstatement(PKG, args, NELEM(args))) != SQLITE_DONE) {
		if (ret == SQLITE_CONSTRAINT) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(PKG));
			switch(pkg_repo_binary_delete_conflicting(pkg->origin,
			    pkg->version, forced)) {
			case EPKG_FATAL: /* sqlite error */
				ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(PKG));
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
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(PKG));
			return (EPKG_FATAL);
		}
	}
	package_id = sqlite3_last_insert_rowid(sqlite);

	dep = NULL;
	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		sql_arg_t dep_arg[] = {
			SQL_ARG(dep->origin),
			SQL_ARG(dep->name),
			SQL_ARG(dep->version),
			SQL_ARG(package_id),
		};
		if (pkg_repo_binary_run_prstatement(DEPS, dep_arg, NELEM(dep_arg)) != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(DEPS));
			return (EPKG_FATAL);
		}
	}

	vec_foreach(pkg->categories, i) {
		sql_arg_t cat_arg1[] = { SQL_ARG(pkg->categories.d[i]) };
		sql_arg_t cat_arg2[] = { SQL_ARG(package_id), SQL_ARG(pkg->categories.d[i])};
		ret = pkg_repo_binary_run_prstatement(CAT1, cat_arg1, NELEM(cat_arg1));
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(CAT2, cat_arg2, NELEM(cat_arg2));
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(CAT2));
			return (EPKG_FATAL);
		}
	}

	vec_foreach(pkg->licenses, i) {
		sql_arg_t lic_arg1[] = { SQL_ARG(pkg->licenses.d[i]) };
		sql_arg_t lic_arg2[] = { SQL_ARG(package_id), SQL_ARG(pkg->licenses.d[i])};
		ret = pkg_repo_binary_run_prstatement(LIC1, lic_arg1, NELEM(lic_arg1));
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(LIC2, lic_arg2, NELEM(lic_arg2));
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(LIC2));
			return (EPKG_FATAL);
		}
	}

	option = NULL;
	while (pkg_options(pkg, &option) == EPKG_OK) {
		sql_arg_t lic_arg1[] = { SQL_ARG(option->key) };
		sql_arg_t lic_arg2[] = {
			SQL_ARG(option->key),
			SQL_ARG(option->value),
			SQL_ARG(package_id),
		};
		ret = pkg_repo_binary_run_prstatement(OPT1, lic_arg1, NELEM(lic_arg1));
		if (ret == SQLITE_DONE)
		    ret = pkg_repo_binary_run_prstatement(OPT2, lic_arg2, NELEM(lic_arg2));
		if(ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(OPT2));
			return (EPKG_FATAL);
		}
	}

	vec_foreach(pkg->shlibs_required, i) {
		sql_arg_t arg1[] = { SQL_ARG(pkg->shlibs_required.d[i]) };
		sql_arg_t arg2[] = {
			SQL_ARG(package_id),
			SQL_ARG(pkg->shlibs_required.d[i]),
		};
		ret = pkg_repo_binary_run_prstatement(SHLIB1, arg1, NELEM(arg1));
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(SHLIB_REQD, arg2, NELEM(arg2));
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(SHLIB_REQD));
			return (EPKG_FATAL);
		}
	}

	vec_foreach(pkg->shlibs_provided, i) {
		sql_arg_t arg1[] = { SQL_ARG(pkg->shlibs_provided.d[i]) };
		sql_arg_t arg2[] = {
			SQL_ARG(package_id),
			SQL_ARG(pkg->shlibs_provided.d[i]),
		};
		ret = pkg_repo_binary_run_prstatement(SHLIB1, arg1, NELEM(arg1));
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(SHLIB_PROV, arg2, NELEM(arg2));
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(SHLIB_PROV));
			return (EPKG_FATAL);
		}
	}

	vec_foreach(pkg->provides, i) {
		sql_arg_t arg1[] = { SQL_ARG(pkg->provides.d[i]) };
		sql_arg_t arg2[] = {
			SQL_ARG(package_id),
			SQL_ARG(pkg->provides.d[i]),
		};
		ret = pkg_repo_binary_run_prstatement(PROVIDE, arg1, NELEM(arg1));
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(PROVIDES, arg2, NELEM(arg2));
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(PROVIDES));
			return (EPKG_FATAL);
		}
	}

	vec_foreach(pkg->requires, i) {
		sql_arg_t arg1[] = { SQL_ARG(pkg->requires.d[i]) };
		sql_arg_t arg2[] = {
			SQL_ARG(package_id),
			SQL_ARG(pkg->requires.d[i]),
		};
		ret = pkg_repo_binary_run_prstatement(REQUIRE, arg1, NELEM(arg1));
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(REQUIRES, arg2, NELEM(arg2));
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(REQUIRES));
			return (EPKG_FATAL);
		}
	}

	vec_foreach(pkg->annotations, i) {
		kv = pkg->annotations.d[i];
		sql_arg_t arg1[] = { SQL_ARG(kv->key) };
		sql_arg_t arg2[] = { SQL_ARG(kv->value) };
		sql_arg_t arg3[] = {
			SQL_ARG(package_id),
			SQL_ARG(kv->key),
			SQL_ARG(kv->value),
		};
		ret = pkg_repo_binary_run_prstatement(ANNOTATE1, arg1, NELEM(arg1));
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(ANNOTATE1, arg2, NELEM(arg2));
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(ANNOTATE2, arg3, NELEM(arg3));
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(ANNOTATE2));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

static int
pkg_repo_binary_register_conflicts(const char *origin, char **conflicts,
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
	stmt = prepare_sql(sqlite, select_id_sql);
	if (stmt == NULL)
		return (EPKG_FATAL);

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
	stmt = prepare_sql(sqlite, clean_conflicts_sql);
	if (stmt == NULL)
		return (EPKG_FATAL);

	sqlite3_bind_int64(stmt, 1, origin_id);
	/* Ignore cleanup result */
	(void)sqlite3_step(stmt);

	sqlite3_finalize(stmt);

	for (i = 0; i < conflicts_num; i ++) {
		/* Select a conflict */
		pkg_debug(4, "pkgdb_repo_register_conflicts: running '%s'", select_id_sql);
		stmt = prepare_sql(sqlite, select_id_sql);
		if (stmt == NULL)
			return (EPKG_FATAL);

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
		stmt = prepare_sql(sqlite, insert_conflict_sql);
		if (stmt == NULL)
			return (EPKG_FATAL);

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

static int
pkg_repo_binary_add_from_string(sqlite3 *sqlite, const char *str, size_t len, struct pkg_repo *repo)
{
	int rc = EPKG_OK;
	struct pkg *pkg;
	const char *abi;

	rc = pkg_new(&pkg, PKG_REMOTE);
	if (rc != EPKG_OK)
		return (EPKG_FATAL);

	rc = pkg_parse_manifest(pkg, str, len);
	if (rc != EPKG_OK)
		goto cleanup;

	if (pkg->digest == NULL || !pkg_checksum_is_valid(pkg->digest, strlen(pkg->digest)))
		pkg_checksum_calculate(pkg, NULL, false, true, false);
	abi = pkg->abi != NULL ? pkg->abi : pkg->altabi;
	if (abi == NULL || !is_valid_abi(abi, true)) {
		rc = EPKG_FATAL;
		pkg_emit_error("repository %s contains packages with wrong ABI: %s",
			repo->name, abi);
		goto cleanup;
	}
	if (!is_valid_os_version(pkg)) {
		rc = EPKG_FATAL;
		pkg_emit_error("repository %s contains packages for wrong OS "
		    "version: %s", repo->name, abi);
		goto cleanup;
	}

	free(pkg->reponame);
	pkg->reponame = xstrdup(repo->name);

	rc = pkg_repo_binary_add_pkg(pkg, sqlite, true);

cleanup:
	pkg_free(pkg);

	return (rc);
}

static int
pkg_repo_binary_add_from_manifest(const char *buf, sqlite3 *sqlite, size_t len,
		struct pkg_repo *repo)
{
	int rc = EPKG_OK;
	struct pkg *pkg;
	const char *abi;

	rc = pkg_new(&pkg, PKG_REMOTE);
	if (rc != EPKG_OK)
		return (EPKG_FATAL);

	rc = pkg_parse_manifest(pkg, buf, len);
	if (rc != EPKG_OK) {
		goto cleanup;
	}

	if (pkg->digest == NULL || !pkg_checksum_is_valid(pkg->digest, strlen(pkg->digest)))
		pkg_checksum_calculate(pkg, NULL, false, true, false);
	abi = pkg->abi != NULL ? pkg->abi : pkg->altabi;
	if (abi == NULL || !is_valid_abi(abi, true)) {
		rc = EPKG_FATAL;
		pkg_emit_error("repository %s contains packages with wrong ABI: %s",
			repo->name, abi);
		goto cleanup;
	}
	if (!is_valid_os_version(pkg)) {
		rc = EPKG_FATAL;
		pkg_emit_error("repository %s contains packages for wrong OS "
		    "version: %s", repo->name, abi);
		goto cleanup;
	}

	free(pkg->reponame);
	pkg->reponame = xstrdup(repo->name);

	rc = pkg_repo_binary_add_pkg(pkg, sqlite, true);

cleanup:
	pkg_free(pkg);

	return (rc);
}

static void __unused
pkg_repo_binary_parse_conflicts(FILE *f, sqlite3 *sqlite)
{
	size_t linecap = 0;
	ssize_t linelen;
	char *linebuf = NULL, *p, **deps;
	const char *origin, *pdep;
	int ndep, i;
	const char conflicts_clean_sql[] = ""
			"DELETE FROM pkg_conflicts;";

	pkg_debug(4, "pkg_parse_conflicts_file: running '%s'", conflicts_clean_sql);
	(void)sql_exec(sqlite, conflicts_clean_sql);

	while ((linelen = getline(&linebuf, &linecap, f)) > 0) {
		p = linebuf;
		origin = strsep(&p, ":");
		/* Check dependencies number */
		pdep = p;
		ndep = 1;
		while (*pdep != '\0') {
			if (*pdep == ',')
				ndep ++;
			pdep ++;
		}
		deps = xmalloc(sizeof(char *) * ndep);
		for (i = 0; i < ndep; i ++) {
			deps[i] = strsep(&p, ",\n");
		}
		pkg_repo_binary_register_conflicts(origin, deps, ndep, sqlite);
		free(deps);
	}

	free(linebuf);
}

static void
rollback_repo(void *data)
{
	const char *name = (const char *)data;
	char path[MAXPATHLEN];

	snprintf(path, sizeof(path), "%s-pkgtemp", name);
	unlink(name);
	rename(path, name);
	snprintf(path, sizeof(path), "%s-journal", name);
	unlink(path);
}

static int
dump_json(struct pkg_repo *repo, const char *line, jsmntok_t *tok, const char *dst_name)
{
	if (tok->type != JSMN_ARRAY) {
		pkg_emit_error("Invalid %s, expecting an array", dst_name);
		return (1);
	}
	if (tok->size == 0) {
		return (1);
	}
	if (repo->dfd == -1 && pkg_repo_open(repo) == EPKG_FATAL)
		return (0);
	int fd = openat(repo->dfd, dst_name, O_CREAT|O_TRUNC|O_RDWR, 0644);
	if (fd == -1) {
		pkg_emit_errno("openat", "repo dump_json");
	}
	FILE *f = fdopen(fd, "w");
	fprintf(f, "%.*s", jsmn_toklen(tok), line + tok->start);
	fclose(f);

	return (0);
}

static int
pkg_repo_binary_update_proceed(const char *name, struct pkg_repo *repo,
	time_t *mtime, bool force)
{
	int rc = EPKG_FATAL, cancel = 0;
	sqlite3 *sqlite = NULL;
	int cnt = 0;
	time_t local_t;
	bool in_trans = false;
	char *path = NULL;
	FILE *f = NULL;
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen, totallen = 0;
	struct pkg_repo_content prc;

	pkg_debug(1, "Pkgrepo, begin update of '%s'", name);

	/* In forced mode, ignore mtime */
	if (force)
		*mtime = 0;

	/* Fetch meta */
	local_t = *mtime;
	if (pkg_repo_fetch_meta(repo, &local_t) == EPKG_FATAL)
		pkg_emit_notice("repository %s has no meta file, using "
		    "default settings", repo->name);

	/* Fetch packagesite */
	local_t = *mtime;
	prc.manifest_fd = -1;
	prc.mtime = *mtime;
	prc.manifest_len = 0;
	prc.data_fd = -1;

	rc = pkg_repo_fetch_data_fd(repo, &prc);
	if (rc == EPKG_UPTODATE)
		goto cleanup;

	if (rc == EPKG_OK) {
		f = fdopen(prc.data_fd, "r");
		rewind(f);
		if ((linelen = getline(&line, &linecap, f)) <0) {
			pkg_emit_errno("Error parsing data", "getline");
			rc = EPKG_FATAL;
			goto cleanup;
		}
		fclose(f);
		f = NULL;
	} else {
		rc = pkg_repo_fetch_remote_extract_fd(repo, &prc);
		if (rc != EPKG_OK)
			goto cleanup;
		f = fdopen(prc.manifest_fd, "r");
		rewind(f);
	}

	*mtime = prc.mtime;
	/*fconflicts = repo_fetch_remote_extract_tmp(repo,
			repo_conflicts_archive, "txz", &local_t,
			&rc, repo_conflicts_file);*/

	/* Load local repository data */
	xasprintf(&path, "%s-pkgtemp", name);
	rename(name, path);
	pkg_register_cleanup_callback(rollback_repo, (void *)name);
	rc = pkg_repo_binary_init_update(repo);
	if (rc != EPKG_OK) {
		rc = EPKG_FATAL;
		goto cleanup;
	}

	/* Here sqlite is initialized */
	sqlite = PRIV_GET(repo);

	pkg_debug(1, "Pkgrepo, reading new metadata");

	pkg_emit_incremental_update_begin(repo->name);
	pkg_emit_progress_start("Processing entries");

	/* 200MB should be enough */
	sql_exec(sqlite, "PRAGMA mmap_size = 209715200;");
	sql_exec(sqlite, "PRAGMA page_size = %d;", getpagesize());
	sql_exec(sqlite, "PRAGMA foreign_keys = OFF;");
	sql_exec(sqlite, "PRAGMA journal_mode = TRUNCATE;");
	sql_exec(sqlite, "PRAGMA synchronous = FULL;");

	rc = pkgdb_transaction_begin_sqlite(sqlite, "REPO");
	if (rc != EPKG_OK)
		goto cleanup;

	in_trans = true;
	if (line != NULL) {
		jsmn_parser p;
		jsmntok_t *tok;
		jsmn_init(&p);
		int tokcount = jsmn_parse(&p, line, linelen, NULL, 0);
		if (tokcount < 0) {
			pkg_emit_error("Invalid data");
			goto cleanup;
		}
		tok = xcalloc(tokcount, sizeof(*tok));
		jsmn_init(&p);
		tokcount = jsmn_parse(&p, line, linelen, tok, tokcount);
		if (tokcount < 0) {
			pkg_emit_error("Invalid data");
			goto cleanup;
		}
		tokcount = p.toknext;
		if (tok->type != JSMN_OBJECT) {
			pkg_emit_error("Invalid data (expecting a json object)");
			free(tok);
			goto cleanup;
		}
		int i = 0;
		while ((i = jsmntok_nextchild(tok, tokcount, 0, i)) > 0) {
			jsmntok_t *key = tok + i;
			jsmntok_t *value = tok + i +1;

			if (key->type != JSMN_STRING) {
				continue;
			}
			if (jsmntok_stringeq(key, line, "groups")) {
				dump_json(repo, line, value, "groups");
			} else if (jsmntok_stringeq(key, line, "expired_packages")) {
				dump_json(repo, line, value, "expired_packages");
			} else if (jsmntok_stringeq(key, line, "packages")) {
				if (value->type == JSMN_ARRAY) {
					int j = i + 1;
					while ((j = jsmntok_nextchild(tok, tokcount, i + 1, j)) > 0) {
						jsmntok_t *jobj = tok + j;
						cnt++;
						if ((cnt % 10) == 0)
							cancel = pkg_emit_progress_tick(cnt, value->size);
						rc = pkg_repo_binary_add_from_string(sqlite, line + jobj->start, jsmn_toklen(jobj), repo);
						if (rc != EPKG_OK || cancel != 0)
							break;
					}
					pkg_emit_progress_tick(cnt, value->size);
				}
			}
		}
	}
	if (f != NULL) {
		while ((linelen = getline(&line, &linecap, f)) > 0) {
			cnt++;
			totallen += linelen;
			if ((cnt % 10 ) == 0)
				cancel = pkg_emit_progress_tick(totallen, prc.manifest_len);
			rc = pkg_repo_binary_add_from_manifest(line, sqlite,
			    linelen, repo);
			if (rc != EPKG_OK || cancel != 0)
				break;
		}
		pkg_emit_progress_tick(prc.manifest_len, prc.manifest_len);
	}

	if (rc == EPKG_OK)
		pkg_emit_incremental_update(repo->name, cnt);

	sql_exec(sqlite, ""
	"CREATE INDEX packages_origin ON packages(origin COLLATE NOCASE);"
	"CREATE INDEX packages_name ON packages(name COLLATE NOCASE);"
	"CREATE INDEX packages_uid_nocase ON packages(name COLLATE NOCASE, origin COLLATE NOCASE);"
	"CREATE INDEX packages_version_nocase ON packages(name COLLATE NOCASE, version);"
	"CREATE INDEX packages_uid ON packages(name, origin);"
	"CREATE INDEX packages_version ON packages(name, version);"
	"CREATE UNIQUE INDEX packages_digest ON packages(manifestdigest);"
	 );

cleanup:

	if (in_trans) {
		if (rc != EPKG_OK)
			pkgdb_transaction_rollback_sqlite(sqlite, "REPO");

		if (pkgdb_transaction_commit_sqlite(sqlite, "REPO") != EPKG_OK)
			rc = EPKG_FATAL;
	}
	if (path != NULL) {
		/* restore the previous db in case of failures */
		if (rc != EPKG_OK && rc != EPKG_UPTODATE) {
			unlink(name);
			rename(path, name);
		}
		unlink(path);
		free(path);
	}
	pkg_unregister_cleanup_callback(rollback_repo, (void *)name);
	if (f != NULL)
		fclose(f);
	free(line);

	return (rc);
}

int
pkg_repo_binary_update(struct pkg_repo *repo, bool force)
{
	char *lockpath = NULL;
	const char update_finish_sql[] = ""
		"DROP TABLE repo_update;";
	char filename[PATH_MAX];
	sqlite3 *sqlite;

	struct stat st;
	time_t t = 0;
	int ld, res = EPKG_FATAL;

	bool got_meta = false;

	sqlite3_initialize();

	if (!pkg_repo_enabled(repo))
		return (EPKG_OK);

	pkg_debug(1, "PkgRepo: verifying update for %s", repo->name);

	(void)snprintf(filename, sizeof(filename), "%s/%s",
	    ctx.dbdir, pkg_repo_binary_get_filename(repo));

	/* First of all, try to open and init repo and check whether it is fine */
	if (repo->dfd == -1 && pkg_repo_open(repo) == EPKG_FATAL)
		return (EPKG_FATAL);

	if (repo->ops->open(repo, R_OK|W_OK) != EPKG_OK) {
		pkg_debug(1, "PkgRepo: need forced update of %s", repo->name);
		t = 0;
		force = true;
	}
	else {
		repo->ops->close(repo, false);
		if (fstatat(repo->dfd, "meta", &st, 0) != -1) {
			t = force ? 0 : st.st_mtime;
			got_meta = true;
		}

		if (got_meta && stat(filename, &st) != -1) {
			if (!force)
				t = st.st_mtime;
		}
	}

	ld = openat(repo->dfd, "lock", O_CREAT|O_TRUNC|O_WRONLY, 00644);
	if (ld == -1) {
		pkg_emit_errno("openat", "lock");
	}
	if (flock(ld, LOCK_EX|LOCK_NB) == -1) {
		/* lock blocking anyway to let the other end finish */
		pkg_emit_notice("Waiting for another process to "
		    "update repository %s", repo->name);
		flock(ld, LOCK_EX);
		res = EPKG_OK;
		t = 0;
		goto cleanup;
	}

	res = pkg_repo_binary_update_proceed(filename, repo, &t, force);
	if (res != EPKG_OK && res != EPKG_UPTODATE) {
		pkg_emit_notice("Unable to update repository %s", repo->name);
		goto cleanup;
	}

	/* Finish updated repo */
	if (res == EPKG_OK) {
		sqlite = PRIV_GET(repo);
		sql_exec(sqlite, update_finish_sql);
	}

cleanup:
	if (ld != -1) {
		flock(ld, LOCK_UN);
		close(ld);
	}

	if (lockpath != NULL)
		unlinkat(repo->dfd, "lock", 0);

	/* Set mtime from http request if possible */
	if (t != 0 && res == EPKG_OK) {
		struct timespec ts[2] = {
			{
			.tv_sec = t,
			.tv_nsec = 0
			},
			{
			.tv_sec = t,
			.tv_nsec = 0
			}
		};

		utimensat(AT_FDCWD, filename, ts, 0);
		if (got_meta)
			utimensat(repo->dfd, "meta", ts, 0);
	}

	if (repo->priv != NULL)
		repo->ops->close(repo, false);

	return (res);
}
