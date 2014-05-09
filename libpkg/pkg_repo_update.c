/*-
 * Copyright (c) 2012-2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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
#include "private/repodb.h"
#include "private/pkg.h"

static int
pkg_repo_register(struct pkg_repo *repo, sqlite3 *sqlite)
{
	sqlite3_stmt *stmt;
	const char sql[] = ""
	    "INSERT OR REPLACE INTO repodata (key, value) "
	    "VALUES (\"packagesite\", ?1);";

	/* register the packagesite */
	if (sql_exec(sqlite, "CREATE TABLE IF NOT EXISTS repodata ("
			"   key TEXT UNIQUE NOT NULL,"
			"   value TEXT NOT NULL"
			");") != EPKG_OK) {
		pkg_emit_error("Unable to register the packagesite in the "
				"database");
		return (EPKG_FATAL);
	}

	if (sqlite3_prepare_v2(sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite);
		return (EPKG_FATAL);
	}

	sqlite3_bind_text(stmt, 1, pkg_repo_url(repo), -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		ERROR_SQLITE(sqlite);
		sqlite3_finalize(stmt);
		return (EPKG_FATAL);
	}

	sqlite3_finalize(stmt);

	return (EPKG_OK);
}

static int
pkg_repo_add_from_manifest(char *buf, const char *origin, long offset,
		const char *manifest_digest, sqlite3 *sqlite,
		struct pkg_manifest_key **keys, struct pkg **p)
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
		goto cleanup;
	}

	rc = pkgdb_repo_add_package(pkg, NULL, sqlite, manifest_digest, true);

cleanup:
	return (rc);
}

struct pkg_increment_task_item {
	char *origin;
	char *digest;
	long offset;
	long length;
	UT_hash_handle hh;
};

static void
pkg_repo_update_increment_item_new(struct pkg_increment_task_item **head, const char *origin,
		const char *digest, long offset, long length)
{
	struct pkg_increment_task_item *item;

	item = calloc(1, sizeof(struct pkg_increment_task_item));
	item->origin = strdup(origin);
	if (digest == NULL)
		digest = "";
	item->digest = strdup(digest);
	item->offset = offset;
	item->length = length;

	HASH_ADD_KEYPTR(hh, *head, item->origin, strlen(item->origin), item);
}

static void __unused
pkg_repo_parse_conflicts_file(FILE *f, sqlite3 *sqlite)
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
		pkgdb_repo_register_conflicts(origin, deps, ndep, sqlite);
		free(deps);
	}

	if (linebuf != NULL)
		free(linebuf);
}

static int
pkg_repo_update_incremental(const char *name, struct pkg_repo *repo, time_t *mtime)
{
	FILE *fmanifest = NULL, *fdigests = NULL /*, *fconflicts = NULL*/;
	sqlite3 *sqlite = NULL;
	struct pkg *pkg = NULL;
	int rc = EPKG_FATAL;
	const char *origin, *digest, *offset, *length;
	struct pkgdb_it *it = NULL;
	char *linebuf = NULL, *p;
	int updated = 0, removed = 0, added = 0, processed = 0, pushed = 0;
	long num_offset, num_length;
	time_t local_t = *mtime;
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
	time_t now, last;

	pkg_debug(1, "Pkgrepo, begin incremental update of '%s'", name);
	if ((rc = pkgdb_repo_open(name, false, &sqlite)) != EPKG_OK) {
		return (EPKG_FATAL);
	}

	if ((rc = pkgdb_repo_init(sqlite)) != EPKG_OK) {
		goto cleanup;
	}

	if ((rc = pkg_repo_register(repo, sqlite)) != EPKG_OK)
		goto cleanup;

	it = pkgdb_repo_origins(sqlite);
	if (it == NULL) {
		rc = EPKG_FATAL;
		goto cleanup;
	}

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		pkg_get(pkg, PKG_ORIGIN, &origin, PKG_DIGEST, &digest);
		pkg_repo_update_increment_item_new(&ldel, origin, digest, 4, 0);
	}

	if (pkg_repo_fetch_meta(repo, NULL) == EPKG_FATAL)
		pkg_emit_notice("repository %s has no meta file, using "
		    "default settings", repo->name);

	fdigests = pkg_repo_fetch_remote_extract_tmp(repo,
			repo->meta->digests, &local_t, &rc);
	if (fdigests == NULL)
		goto cleanup;
	digest_t = local_t;
	local_t = *mtime;
	fmanifest = pkg_repo_fetch_remote_extract_tmp(repo,
			repo->meta->manifests, &local_t, &rc);
	if (fmanifest == NULL)
		goto cleanup;
	packagesite_t = digest_t;
	*mtime = packagesite_t > digest_t ? packagesite_t : digest_t;
	/*fconflicts = repo_fetch_remote_extract_tmp(repo,
			repo_conflicts_archive, "txz", &local_t,
			&rc, repo_conflicts_file);*/
	fseek(fmanifest, 0, SEEK_END);
	len = ftell(fmanifest);

	pkg_debug(1, "Pkgrepo, reading new packagesite.yaml for '%s'", name);
	/* load the while digests */
	while ((linelen = getline(&linebuf, &linecap, fdigests)) > 0) {
		p = linebuf;
		origin = strsep(&p, ":");
		digest = strsep(&p, ":");
		offset = strsep(&p, ":");
		/* files offset */
		strsep(&p, ":");
		length = strsep(&p, ":");

		if (origin == NULL || digest == NULL ||
				offset == NULL) {
			pkg_emit_error("invalid digest file format");
			assert(0);
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
			pkg_repo_update_increment_item_new(&ladd, origin, digest, num_offset,
					num_length);
		} else {
			if (strcmp(digest, item->digest) == 0) {
				free(item->origin);
				free(item->digest);
				HASH_DEL(ldel, item);
				free(item);
				item = NULL;
			} else {
				free(item->origin);
				free(item->digest);
				HASH_DEL(ldel, item);
				free(item);
				item = NULL;
				pkg_repo_update_increment_item_new(&ladd, origin, digest,
						num_offset, num_length);
				updated++;
			}
		}
	}

	rc = EPKG_OK;

	pkg_debug(1, "Pkgrepo, removing old entries for '%s'", name);
	removed = HASH_COUNT(ldel);
	hash_it = 0;
	last = 0;
	HASH_ITER(hh, ldel, item, tmp_item) {
		now = time(NULL);
		if (++hash_it == removed || now > last) {
			pkg_emit_update_remove(removed, hash_it);
			last = now;
		}
		if (rc == EPKG_OK) {
			rc = pkgdb_repo_remove_package(item->origin);
		}
		free(item->origin);
		free(item->digest);
		HASH_DEL(ldel, item);
		free(item);
	}

	pkg_debug(1, "Pkgrepo, pushing new entries for '%s'", name);
	pkg = NULL;

	if (len > 0 && len < SSIZE_MAX) {
		map = mmap(NULL, len, PROT_READ, MAP_SHARED, fileno(fmanifest), 0);
		fclose(fmanifest);
	} else {
		if (len == 0)
			pkg_emit_error("Empty catalog");
		else
			pkg_emit_error("Catalog too large");
		return (EPKG_FATAL);
	}

	hash_it = 0;
	last = 0;
	pushed = HASH_COUNT(ladd);
	HASH_ITER(hh, ladd, item, tmp_item) {
		now = time(NULL);
		if (++hash_it == pushed || now > last) {
			pkg_emit_update_add(pushed, hash_it);
			last = now;
		}
		if (rc == EPKG_OK) {
			if (item->length != 0) {
				rc = pkg_repo_add_from_manifest(map + item->offset, item->origin,
						item->length, item->digest,
						sqlite, &keys, &pkg);
			}
			else {
				rc = pkg_repo_add_from_manifest(map + item->offset, item->origin,
						len - item->offset, item->digest, sqlite, &keys, &pkg);
			}
		}
		free(item->origin);
		free(item->digest);
		HASH_DEL(ladd, item);
		free(item);
	}
	pkg_manifest_keys_free(keys);
	pkg_emit_incremental_update(updated, removed, added, processed);

cleanup:
	if (pkg != NULL)
		pkg_free(pkg);
	if (it != NULL)
		pkgdb_it_free(it);
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

	pkgdb_repo_close(sqlite, rc == EPKG_OK);

	return (rc);
}

int
pkg_repo_update_binary_pkgs(struct pkg_repo *repo, bool force)
{
	char filepath[MAXPATHLEN];

	const char *dbdir = NULL;
	struct stat st;
	time_t t = 0;
	sqlite3 *sqlite = NULL;
	char *req = NULL;
	int64_t res;
	bool got_meta = false;

	sqlite3_initialize();

	if (!pkg_repo_enabled(repo))
		return (EPKG_OK);

	dbdir = pkg_object_string(pkg_config_get("PKG_DBDIR"));
	pkg_debug(1, "PkgRepo: verifying update for %s", pkg_repo_name(repo));

	snprintf(filepath, sizeof(filepath), "%s/%s.meta", dbdir, pkg_repo_name(repo));
	if (stat(filepath, &st) != -1) {
		t = force ? 0 : st.st_mtime;
		got_meta = true;
	}

	snprintf(filepath, sizeof(filepath), "%s/%s.sqlite", dbdir, pkg_repo_name(repo));
	if (stat(filepath, &st) != -1) {
		if (!got_meta && !force)
			t = st.st_mtime;
	}

	if (t != 0) {
		if (sqlite3_open(filepath, &sqlite) != SQLITE_OK) {
			pkg_emit_error("Unable to open local database");
			return (EPKG_FATAL);
		}

		if (get_pragma(sqlite, "SELECT count(name) FROM sqlite_master "
		    "WHERE type='table' AND name='repodata';", &res, false) != EPKG_OK) {
			pkg_emit_error("Unable to query repository");
			sqlite3_close(sqlite);
			return (EPKG_FATAL);
		}

		if (res != 1) {
			t = 0;

			if (sqlite != NULL) {
				sqlite3_close(sqlite);
				sqlite = NULL;
			}
		}
	}

	if (t != 0) {
		req = sqlite3_mprintf("select count(key) from repodata "
		    "WHERE key = \"packagesite\" and value = '%q'", pkg_repo_url(repo));

		res = 0;
		/*
		 * Ignore error here:
		 * if an error occure it means the database is unusable
		 * therefor it is better to rebuild it from scratch
		 */
		get_pragma(sqlite, req, &res, true);
		sqlite3_free(req);
		if (res != 1) {
			t = 0;

			if (sqlite != NULL) {
				sqlite3_close(sqlite);
				sqlite = NULL;
			}
			unlink(filepath);
		}
	}

	res = pkg_repo_update_incremental(filepath, repo, &t);
	if (res != EPKG_OK && res != EPKG_UPTODATE) {
		pkg_emit_notice("Unable to find catalogs");
		goto cleanup;
	}

cleanup:
	/* Set mtime from http request if possible */
	if (t != 0) {
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

		if (got_meta)
			snprintf(filepath, sizeof(filepath), "%s/%s.meta", dbdir, pkg_repo_name(repo));

		utimes(filepath, ftimes);
	}

	return (res);
}

int
pkg_update(struct pkg_repo *repo, bool force)
{
	return (repo->update(repo, force));
}
