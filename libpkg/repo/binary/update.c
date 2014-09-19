/*
 * Copyright (c) 2014, Vsevolod Stakhov
 * Copyright (c) 2012-2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2012 Julien Laffaye <jlaffaye@FreeBSD.org>
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
#include <sys/mman.h>

#define _WITH_GETLINE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include <archive.h>
#include <archive_entry.h>

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkgdb.h"
#include "private/pkg.h"
#include "binary.h"
#include "binary_private.h"

static int
pkg_repo_binary_init_update(struct pkg_repo *repo, const char *name,
	bool forced)
{
	sqlite3 *sqlite;
	const char update_check_sql[] = ""
					"INSERT INTO repo_update VALUES(1);";
	const char update_start_sql[] = ""
					"CREATE TABLE IF NOT EXISTS repo_update (n INT);";

	if (!forced) {
		/* Try to open repo */
		if (repo->ops->open(repo, R_OK|W_OK) != EPKG_OK) {
			/* Try to re-create it */
			unlink(name);
			if (repo->ops->create(repo) != EPKG_OK) {
				pkg_emit_notice("Unable to create repository %s", repo->name);
				return (EPKG_FATAL);
			}
			if (repo->ops->open(repo, R_OK|W_OK) != EPKG_OK) {
				pkg_emit_notice("Unable to open created repository %s", repo->name);
				return (EPKG_FATAL);
			}
		}
	}
	else {
		/* [Re]create repo */
		unlink(name);
		if (repo->ops->create(repo) != EPKG_OK) {
			pkg_emit_notice("Unable to create repository %s", repo->name);
			return (EPKG_FATAL);
		}
		if (repo->ops->open(repo, R_OK|W_OK) != EPKG_OK) {
			pkg_emit_notice("Unable to open created repository %s", repo->name);
			return (EPKG_FATAL);
		}
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
			 const char *pkg_path, bool forced)
{
	int ret = EPKG_FATAL;
	const char *oversion;

	if (pkg_repo_binary_run_prstatement(VERSION, origin) != SQLITE_ROW) {
		ret = EPKG_FATAL;
		goto cleanup;
	}
	oversion = sqlite3_column_text(pkg_repo_binary_stmt_prstatement(VERSION), 0);
	if (!forced) {
		switch(pkg_version_cmp(oversion, version)) {
		case -1:
			pkg_emit_error("duplicate package origin: replacing older "
					"version %s in repo with package %s for "
					"origin %s", oversion, pkg_path, origin);

			if (pkg_repo_binary_run_prstatement(DELETE, origin, origin) !=
							SQLITE_DONE)
				ret = EPKG_FATAL;
			else
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
		if (pkg_repo_binary_run_prstatement(DELETE, origin, origin) != SQLITE_DONE)
			ret = EPKG_FATAL;

		ret = EPKG_OK;
	}

cleanup:
	sqlite3_reset(pkg_repo_binary_stmt_prstatement(VERSION));

	return (ret);
}

static int
pkg_repo_binary_add_pkg(struct pkg *pkg, const char *pkg_path,
		sqlite3 *sqlite, bool forced)
{
	const char *name, *version, *origin, *comment, *desc;
	const char *arch, *maintainer, *www, *prefix, *sum, *rpath;
	const char *olddigest, *manifestdigest;
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
			    PKG_ANNOTATIONS, &annotations, PKG_OLD_DIGEST, &olddigest,
			    PKG_DIGEST, &manifestdigest);

try_again:
	if ((ret = pkg_repo_binary_run_prstatement(PKG, origin, name, version,
			comment, desc, arch, maintainer, www, prefix,
			pkgsize, flatsize, (int64_t)licenselogic, sum,
			rpath, manifestdigest, olddigest)) != SQLITE_DONE) {
		if (ret == SQLITE_CONSTRAINT) {
			switch(pkg_repo_binary_delete_conflicting(origin,
					version, pkg_path, forced)) {
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

	if (pkg_repo_binary_run_prstatement (FTS_APPEND, package_id,
			name, version, origin) != SQLITE_DONE) {
		ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(FTS_APPEND));
		return (EPKG_FATAL);
	}

	dep = NULL;
	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		if (pkg_repo_binary_run_prstatement(DEPS,
				pkg_dep_origin(dep),
				pkg_dep_name(dep),
				pkg_dep_version(dep),
				package_id) != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(DEPS));
			return (EPKG_FATAL);
		}
	}

	it = NULL;
	while ((obj = pkg_object_iterate(categories, &it))) {
		ret = pkg_repo_binary_run_prstatement(CAT1, pkg_object_string(obj));
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(CAT2, package_id,
			    pkg_object_string(obj));
		if (ret != SQLITE_DONE)
		{
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(CAT2));
			return (EPKG_FATAL);
		}
	}

	it = NULL;
	while ((obj = pkg_object_iterate(licenses, &it))) {
		ret = pkg_repo_binary_run_prstatement(LIC1, pkg_object_string(obj));
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(LIC2, package_id,
			    pkg_object_string(obj));
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(LIC2));
			return (EPKG_FATAL);
		}
	}
	option = NULL;
	while (pkg_options(pkg, &option) == EPKG_OK) {
		ret = pkg_repo_binary_run_prstatement(OPT1, pkg_option_opt(option));
		if (ret == SQLITE_DONE)
		    ret = pkg_repo_binary_run_prstatement(OPT2, pkg_option_opt(option),
				pkg_option_value(option), package_id);
		if(ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(OPT2));
			return (EPKG_FATAL);
		}
	}

	shlib = NULL;
	while (pkg_shlibs_required(pkg, &shlib) == EPKG_OK) {
		const char *shlib_name = pkg_shlib_name(shlib);

		ret = pkg_repo_binary_run_prstatement(SHLIB1, shlib_name);
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(SHLIB_REQD, package_id,
					shlib_name);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(SHLIB_REQD));
			return (EPKG_FATAL);
		}
	}

	shlib = NULL;
	while (pkg_shlibs_provided(pkg, &shlib) == EPKG_OK) {
		const char *shlib_name = pkg_shlib_name(shlib);

		ret = pkg_repo_binary_run_prstatement(SHLIB1, shlib_name);
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(SHLIB_PROV, package_id,
					shlib_name);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(SHLIB_PROV));
			return (EPKG_FATAL);
		}
	}

	it = NULL;
	while ((obj = pkg_object_iterate(annotations, &it))) {
		const char *note_tag = pkg_object_key(obj);
		const char *note_val = pkg_object_string(obj);

		ret = pkg_repo_binary_run_prstatement(ANNOTATE1, note_tag);
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(ANNOTATE1, note_val);
		if (ret == SQLITE_DONE)
			ret = pkg_repo_binary_run_prstatement(ANNOTATE2, package_id,
				  note_tag, note_val);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, pkg_repo_binary_sql_prstatement(ANNOTATE2));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

int
pkgdb_repo_remove_package(const char *origin)
{
	if (pkg_repo_binary_run_prstatement(DELETE, origin, origin) != SQLITE_DONE)
		return (EPKG_FATAL); /* sqlite error */

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

static int
pkg_repo_binary_add_from_manifest(char *buf, const char *origin, const char *digest,
		long offset, sqlite3 *sqlite,
		struct pkg_manifest_key **keys, struct pkg **p, bool is_legacy,
		struct pkg_repo *repo)
{
	int rc = EPKG_OK;
	struct pkg *pkg;
	const char *local_origin, *pkg_arch;

	if (*p == NULL) {
		rc = pkg_new(p, PKG_REMOTE);
		if (rc != EPKG_OK)
			return (EPKG_FATAL);
	} else {
		pkg_reset(*p, PKG_REMOTE);
	}

	pkg = *p;

	pkg_manifest_keys_new(keys);
	rc = pkg_parse_manifest(pkg, buf, offset, *keys);
	if (rc != EPKG_OK) {
		goto cleanup;
	}
	rc = pkg_is_valid(pkg);
	if (rc != EPKG_OK) {
		goto cleanup;
	}

	/* Ensure that we have a proper origin and arch*/
	pkg_get(pkg, PKG_ORIGIN, &local_origin, PKG_ARCH, &pkg_arch);
	if (local_origin == NULL || strcmp(local_origin, origin) != 0) {
		pkg_emit_error("manifest contains origin %s while we wanted to add origin %s",
				local_origin ? local_origin : "NULL", origin);
		rc = EPKG_FATAL;
		goto cleanup;
	}

	if (pkg_arch == NULL || !is_valid_abi(pkg_arch, true)) {
		rc = EPKG_FATAL;
		pkg_emit_error("repository %s contains packages with wrong ABI: %s",
			repo->name, pkg_arch);
		goto cleanup;
	}

	pkg_set(pkg, PKG_REPONAME, repo->name);
	if (is_legacy) {
		pkg_set(pkg, PKG_OLD_DIGEST, digest);
		pkg_checksum_calculate(pkg, NULL);
	}
	else {
		pkg_set(pkg, PKG_DIGEST, digest);
	}

	rc = pkg_repo_binary_add_pkg(pkg, NULL, sqlite, true);

cleanup:
	return (rc);
}

struct pkg_increment_task_item {
	char *origin;
	char *digest;
	char *olddigest;
	long offset;
	long length;
	char *checksum;
	UT_hash_handle hh;
};

static void
pkg_repo_binary_update_item_new(struct pkg_increment_task_item **head, const char *origin,
		const char *digest, long offset, long length, const char *checksum)
{
	struct pkg_increment_task_item *item;

	item = calloc(1, sizeof(struct pkg_increment_task_item));
	item->origin = strdup(origin);
	if (digest == NULL)
		digest = "";
	item->digest = strdup(digest);
	item->offset = offset;
	item->length = length;
	if (checksum)
		item->checksum = strdup(checksum);

	HASH_ADD_KEYPTR(hh, *head, item->origin, strlen(item->origin), item);
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
		deps = malloc(sizeof(char *) * ndep);
		for (i = 0; i < ndep; i ++) {
			deps[i] = strsep(&p, ",\n");
		}
		pkg_repo_binary_register_conflicts(origin, deps, ndep, sqlite);
		free(deps);
	}

	free(linebuf);
}

sqlite3_stmt *
pkg_repo_binary_get_origins(sqlite3 *sqlite)
{
	sqlite3_stmt *stmt = NULL;
	int ret;
	const char query_sql[] = ""
		"SELECT id, origin, name, name || '~' || origin as uniqueid, version, comment, "
		"prefix, desc, arch, maintainer, www, "
		"licenselogic, flatsize, pkgsize, "
		"cksum, path AS repopath, manifestdigest "
		"FROM packages "
		"ORDER BY origin;";

	pkg_debug(4, "binary_repo: running '%s'", query_sql);
	ret = sqlite3_prepare_v2(sqlite, query_sql, -1,
			&stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(sqlite, query_sql);
		return (NULL);
	}

	return (stmt);
}

static void
pkg_repo_binary_update_item_free(struct pkg_increment_task_item *item)
{
	if (item != NULL) {
		free(item->origin);
		free(item->digest);
		free(item->checksum);
		free(item);
	}
}

static int
pkg_repo_binary_update_incremental(const char *name, struct pkg_repo *repo,
	time_t *mtime, bool force)
{
	FILE *fmanifest = NULL, *fdigests = NULL /*, *fconflicts = NULL*/;
	struct pkg *pkg = NULL;
	int rc = EPKG_FATAL;
	sqlite3 *sqlite = NULL;
	sqlite3_stmt *stmt;
	const char *origin, *digest, *offset, *length, *checksum;
	char *linebuf = NULL, *p;
	int updated = 0, removed = 0, added = 0, processed = 0, pushed = 0;
	long num_offset, num_length;
	time_t local_t;
	time_t digest_t;
	time_t packagesite_t;
	struct pkg_increment_task_item *ldel = NULL, *ladd = NULL,
			*item, *tmp_item;
	struct pkg_manifest_key *keys = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	char *map = MAP_FAILED;
	size_t len = 0;
	int hash_it = 0;
	bool in_trans = false, legacy_repo = false;
	/* Required for making iterator */
	struct pkgdb_it *it = NULL;
	struct pkgdb fakedb;

	pkg_debug(1, "Pkgrepo, begin incremental update of '%s'", name);

	/* In forced mode, ignore mtime */
	if (force)
		*mtime = 0;

	/* Fetch meta */
	local_t = *mtime;
	if (pkg_repo_fetch_meta(repo, &local_t) == EPKG_FATAL)
		pkg_emit_notice("repository %s has no meta file, using "
		    "default settings", repo->name);

	/* Fetch digests */
	local_t = *mtime;
	fdigests = pkg_repo_fetch_remote_extract_tmp(repo,
			repo->meta->digests, &local_t, &rc);
	if (fdigests == NULL)
		goto cleanup;

	/* Fetch packagesite */
	digest_t = local_t;
	local_t = *mtime;
	fmanifest = pkg_repo_fetch_remote_extract_tmp(repo,
			repo->meta->manifests, &local_t, &rc);
	if (fmanifest == NULL)
		goto cleanup;

	packagesite_t = local_t;
	*mtime = digest_t;
	/*fconflicts = repo_fetch_remote_extract_tmp(repo,
			repo_conflicts_archive, "txz", &local_t,
			&rc, repo_conflicts_file);*/
	fseek(fmanifest, 0, SEEK_END);
	len = ftell(fmanifest);

	/* Detect whether we have legacy repo */
	if ((linelen = getline(&linebuf, &linecap, fdigests)) > 0) {
		p = linebuf;
		origin = strsep(&p, ":");
		digest = strsep(&p, ":");
		if (digest == NULL) {
			pkg_emit_error("invalid digest file format");
			rc = EPKG_FATAL;
			goto cleanup;
		}
		if (!pkg_checksum_is_valid(digest, strlen(digest))) {
			legacy_repo = true;
			pkg_debug(1, "repository '%s' has a legacy digests format", repo->name);
		}
	}
	fseek(fdigests, 0, SEEK_SET);

	/* Load local repository data */
	rc = pkg_repo_binary_init_update(repo, name, force);
	if (rc == EPKG_END) {
		/* Need to perform forced update */
		repo->ops->close(repo, false);
		return (pkg_repo_binary_update_incremental(name, repo, mtime, true));
	}
	if (rc != EPKG_OK) {
		rc = EPKG_FATAL;
		goto cleanup;
	}

	/* Here sqlite is initialized */
	sqlite = PRIV_GET(repo);

	stmt = pkg_repo_binary_get_origins(sqlite);
	if (stmt == NULL) {
		rc = EPKG_FATAL;
		goto cleanup;
	}
	fakedb.sqlite = sqlite;
	it = pkgdb_it_new_sqlite(&fakedb, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE);

	if (it != NULL) {
		while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
			pkg_get(pkg, PKG_ORIGIN, &origin, legacy_repo ? PKG_OLD_DIGEST : PKG_DIGEST,
							&digest, PKG_CKSUM, &checksum);
			pkg_repo_binary_update_item_new(&ldel, origin, digest, 0, 0, checksum);
		}

		pkgdb_it_free(it);
	}
	else {
		sqlite3_finalize(stmt);
	}

	pkg_debug(1, "Pkgrepo, reading new packagesite.yaml for '%s'", name);
	/* load the while digests */
	while ((linelen = getline(&linebuf, &linecap, fdigests)) > 0) {
		p = linebuf;
		origin = strsep(&p, ":");
		digest = strsep(&p, ":");
		offset = strsep(&p, ":");
		/* files offset */
		strsep(&p, ":");
		if (p)
			length = strsep(&p, ":\n");
		/* Checksum */
		if (p)
			checksum = strsep(&p, ":\n");

		if (origin == NULL || digest == NULL ||
				offset == NULL) {
			pkg_emit_error("invalid digest file format");
			rc = EPKG_FATAL;
			goto cleanup;
		}
		errno = 0;
		num_offset = (long)strtoul(offset, NULL, 10);
		if (errno != 0) {
			pkg_emit_errno("strtoul", "digest format error");
			rc = EPKG_FATAL;
			goto cleanup;
		}
		if (length != NULL) {
			errno = 0;
			num_length = (long)strtoul(length, NULL, 10);
			if (errno != 0) {
				pkg_emit_errno("strtoul", "digest format error");
				rc = EPKG_FATAL;
				goto cleanup;
			}
		}
		else {
			num_length = 0;
		}
		processed++;
		HASH_FIND_STR(ldel, origin, item);
		if (item == NULL) {
			added++;
			pkg_repo_binary_update_item_new(&ladd, origin, digest, num_offset,
					num_length, checksum);
		} else {
			HASH_DEL(ldel, item);
			if (checksum == NULL || item->checksum == NULL) {
				pkg_repo_binary_update_item_new(&ladd, origin, digest,
						num_offset, num_length, checksum);
				updated++;
			}
			else if (strcmp(checksum, item->checksum) != 0) {
				/* Allow checksum to be used as unique mark */
				pkg_repo_binary_update_item_new(&ladd, origin, digest,
					num_offset, num_length, checksum);
				updated++;
			}

			pkg_repo_binary_update_item_free(item);
		}
	}

	rc = EPKG_OK;

	pkg_debug(1, "Pkgrepo, removing old entries for '%s'", name);

	rc = pkgdb_transaction_begin(sqlite, "REPO");
	if (rc != EPKG_OK)
		goto cleanup;

	in_trans = true;

	removed = HASH_COUNT(ldel);
	hash_it = 0;
	if (removed > 0)
		pkg_emit_progress_start("Removing expired repository entries");
	HASH_ITER(hh, ldel, item, tmp_item) {
		pkg_emit_progress_tick(++hash_it, removed);
		if (rc == EPKG_OK) {
			rc = pkgdb_repo_remove_package(item->origin);
		}
		else {
			pkg_emit_progress_tick(removed, removed);
		}
		HASH_DEL(ldel, item);
		pkg_repo_binary_update_item_free(item);
	}
	if (removed > 0)
		pkg_emit_progress_tick(removed, removed);

	pkg_debug(1, "Pkgrepo, pushing new entries for '%s'", name);
	pkg = NULL;

	if (len > 0 && len < SSIZE_MAX) {
		map = mmap(NULL, len, PROT_READ, MAP_SHARED, fileno(fmanifest), 0);
		fclose(fmanifest);
	} else {
		if (len == 0)
			pkg_emit_error("Empty catalogue");
		else
			pkg_emit_error("Catalogue too large");
		goto cleanup;
	}

	hash_it = 0;
	pushed = HASH_COUNT(ladd);
	if (pushed > 0)
		pkg_emit_progress_start("Processing new repository entries");
	HASH_ITER(hh, ladd, item, tmp_item) {
		pkg_emit_progress_tick(++hash_it, pushed);
		if (rc == EPKG_OK) {
			if (item->length != 0) {
				rc = pkg_repo_binary_add_from_manifest(map + item->offset, item->origin,
				    item->digest, item->length, sqlite, &keys, &pkg, legacy_repo,
				    repo);
			}
			else {
				rc = pkg_repo_binary_add_from_manifest(map + item->offset, item->origin,
				    item->digest, len - item->offset, sqlite, &keys, &pkg,
				    legacy_repo, repo);
			}
		}
		else {
			pkg_emit_progress_tick(pushed, pushed);
		}
		HASH_DEL(ladd, item);
		pkg_repo_binary_update_item_free(item);
	}
	if (pushed > 0) {
		pkg_emit_progress_tick(pushed, pushed);
		pkg_manifest_keys_free(keys);
	}

	if (rc == EPKG_OK)
		pkg_emit_incremental_update(repo->name, updated, removed,
		    added, processed);

cleanup:

	if (in_trans) {
		if (rc != EPKG_OK)
			pkgdb_transaction_rollback(sqlite, "REPO");

		if (pkgdb_transaction_commit(sqlite, "REPO") != EPKG_OK)
			rc = EPKG_FATAL;
	}

	if (pkg != NULL)
		pkg_free(pkg);
	if (map == MAP_FAILED && fmanifest)
		fclose(fmanifest);
	if (fdigests)
		fclose(fdigests);
	/* if (fconflicts)
		fclose(fconflicts);*/
	if (map != MAP_FAILED)
		munmap(map, len);
	if (linebuf != NULL)
		free(linebuf);

	return (rc);
}

int
pkg_repo_binary_update(struct pkg_repo *repo, bool force)
{
	char filepath[MAXPATHLEN];
	const char update_finish_sql[] = ""
		"DROP TABLE repo_update;";
	sqlite3 *sqlite;

	const char *dbdir = NULL;
	struct stat st;
	time_t t = 0;
	int res = EPKG_FATAL;

	bool got_meta = false;

	sqlite3_initialize();

	if (!pkg_repo_enabled(repo))
		return (EPKG_OK);

	dbdir = pkg_object_string(pkg_config_get("PKG_DBDIR"));
	pkg_debug(1, "PkgRepo: verifying update for %s", pkg_repo_name(repo));

	/* First of all, try to open and init repo and check whether it is fine */
	if (repo->ops->open(repo, R_OK|W_OK) != EPKG_OK) {
		pkg_debug(1, "PkgRepo: need forced update of %s", pkg_repo_name(repo));
		t = 0;
		force = true;
		snprintf(filepath, sizeof(filepath), "%s/%s", dbdir,
		    pkg_repo_binary_get_filename(pkg_repo_name(repo)));
	}
	else {
		repo->ops->close(repo, false);
		snprintf(filepath, sizeof(filepath), "%s/%s.meta", dbdir, pkg_repo_name(repo));
		if (stat(filepath, &st) != -1) {
			t = force ? 0 : st.st_mtime;
			got_meta = true;
		}

		snprintf(filepath, sizeof(filepath), "%s/%s", dbdir,
			pkg_repo_binary_get_filename(pkg_repo_name(repo)));
		if (stat(filepath, &st) != -1) {
			if (!got_meta && !force)
				t = st.st_mtime;
		}
	}

	res = pkg_repo_binary_update_incremental(filepath, repo, &t, force);
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
	/* Set mtime from http request if possible */
	if (t != 0 && res == EPKG_OK) {
		struct timeval ftimes[2] = {
			{
			.tv_sec = t,
			.tv_usec = 0
			},
			{
			.tv_sec = t,
			.tv_usec = 0
			}
		};

		utimes(filepath, ftimes);
		if (got_meta) {
			snprintf(filepath, sizeof(filepath), "%s/%s.meta", dbdir, pkg_repo_name(repo));
			utimes(filepath, ftimes);
		}
	}

	if (repo->priv != NULL)
		repo->ops->close(repo, false);

	return (res);
}
