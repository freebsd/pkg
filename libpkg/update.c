/*-
 * Copyright (c) 2012 Julien Laffaye <jlaffaye@FreeBSD.org>
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

//#define EXTRACT_ARCHIVE_FLAGS  (ARCHIVE_EXTRACT_OWNER |ARCHIVE_EXTRACT_PERM)

/* Add indexes to the repo */
static int
remote_add_indexes(const char *repo)
{
	struct pkgdb *db = NULL;
	int ret = EPKG_FATAL;

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK)
		goto cleanup;

	/* Initialize the remote remote */
	if (pkgdb_remote_init(db, repo) != EPKG_OK)
		goto cleanup;

	ret = EPKG_OK;

	cleanup:
	if (db)
		pkgdb_close(db);
	return (ret);
}

/* Return opened file descriptor */
static int
repo_fetch_remote_tmp(struct pkg_repo *repo, const char *filename, const char *extension, time_t *t, int *rc)
{
	char url[MAXPATHLEN];
	char tmp[MAXPATHLEN];
	int fd;
	mode_t mask;
	const char *tmpdir;

	snprintf(url, MAXPATHLEN, "%s/%s.%s", pkg_repo_url(repo), filename, extension);

	tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL)
		tmpdir = "/tmp";
	mkdirs(tmpdir);
	snprintf(tmp, MAXPATHLEN, "%s/%s.%s.XXXXXX", tmpdir, filename, extension);

	mask = umask(022);
	fd = mkstemp(tmp);
	umask(mask);
	if (fd == -1) {
		pkg_emit_error("Could not create temporary file %s, "
		    "aborting update.\n", tmp);
		*rc = EPKG_FATAL;
		return (-1);
	}
	(void)unlink(tmp);

	if ((*rc = pkg_fetch_file_to_fd(repo, url, fd, t)) != EPKG_OK) {
		close(fd);
		fd = -1;
	}

	return (fd);
}

static int
repo_archive_extract_file(int fd, const char *file, const char *dest, const char *repokey, int dest_fd)
{
	struct archive *a = NULL;
	struct archive_entry *ae = NULL;
	unsigned char *sig = NULL;
	int siglen = 0, ret, rc = EPKG_OK;

	a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_tar(a);

	/* Seek to the begin of file */
	(void)lseek(fd, 0, SEEK_SET);
	archive_read_open_fd(a, fd, 4096);

	while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
		if (strcmp(archive_entry_pathname(ae), file) == 0) {
			if (dest_fd == -1) {
				archive_entry_set_pathname(ae, dest);
				/*
				 * The repo should be owned by root and not writable
				 */
				archive_entry_set_uid(ae, 0);
				archive_entry_set_gid(ae, 0);
				archive_entry_set_perm(ae, 0644);

				if (archive_read_extract(a, ae, EXTRACT_ARCHIVE_FLAGS) != 0) {
					pkg_emit_errno("archive_read_extract", "extract error");
					rc = EPKG_FATAL;
					goto cleanup;
				}
			} else {
				if (archive_read_data_into_fd(a, dest_fd) != 0) {
					pkg_emit_errno("archive_read_extract", "extract error");
					rc = EPKG_FATAL;
					goto cleanup;
				}
				(void)lseek(dest_fd, 0, SEEK_SET);
			}
		}
		if (strcmp(archive_entry_pathname(ae), "signature") == 0) {
			siglen = archive_entry_size(ae);
			sig = malloc(siglen);
			archive_read_data(a, sig, siglen);
		}
	}

	if (repokey != NULL) {
		if (sig != NULL) {
			ret = rsa_verify(dest, repokey,
					sig, siglen - 1, dest_fd);
			if (ret != EPKG_OK) {
				pkg_emit_error("Invalid signature, "
						"removing repository.");
				if (dest != NULL)
					unlink(dest);
				free(sig);
				rc = EPKG_FATAL;
				goto cleanup;
			}
			free(sig);
		} else {
			pkg_emit_error("No signature found in the repository.  "
					"Can not validate against %s key.", repokey);
			rc = EPKG_FATAL;
			if (dest != NULL)
				unlink(dest);
			goto cleanup;
		}
	}
cleanup:
	if (a != NULL)
		archive_read_free(a);

	return rc;
}

static FILE *
repo_fetch_remote_extract_tmp(struct pkg_repo *repo, const char *filename,
		const char *extension, time_t *t, int *rc, const char *archive_file)
{
	int fd, dest_fd;
	mode_t mask;
	FILE *res = NULL;
	const char *tmpdir;
	char tmp[MAXPATHLEN];

	fd = repo_fetch_remote_tmp(repo, filename, extension, t, rc);
	if (fd == -1) {
		return (NULL);
	}

	tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL)
		tmpdir = "/tmp";
	snprintf(tmp, MAXPATHLEN, "%s/%s.XXXXXX", tmpdir, archive_file);

	mask = umask(022);
	dest_fd = mkstemp(tmp);
	umask(mask);
	if (dest_fd == -1) {
		pkg_emit_error("Could not create temporary file %s, "
				"aborting update.\n", tmp);
		*rc = EPKG_FATAL;
		goto cleanup;
	}
	(void)unlink(tmp);
	if (repo_archive_extract_file(fd, archive_file, NULL, repo->pubkey, dest_fd) != EPKG_OK) {
		*rc = EPKG_FATAL;
		goto cleanup;
	}

	res = fdopen(dest_fd, "r");
	if (res == NULL) {
		pkg_emit_errno("fdopen", "digest open failed");
		*rc = EPKG_FATAL;
		goto cleanup;
	}
	dest_fd = -1;
	*rc = EPKG_OK;

cleanup:
	if (dest_fd != -1)
		close(dest_fd);
	/* Thus removing archived file as well */
	close(fd);
	return (res);
}

static int
pkg_register_repo(struct pkg_repo *repo, sqlite3 *sqlite)
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
pkg_update_full(const char *repofile, struct pkg_repo *repo, time_t *mtime)
{
	char repofile_unchecked[MAXPATHLEN];
	int fd = -1, rc = EPKG_FATAL;
	sqlite3 *sqlite = NULL;
	char *req = NULL;
	char *bad_abis = NULL;
	const char *myarch;

	snprintf(repofile_unchecked, sizeof(repofile_unchecked),
			"%s.unchecked", repofile);

	/* If the repo.sqlite file exists, test that we can write to
		   it.  If it doesn't exist, assume we can create it */

	if (eaccess(repofile, F_OK) == 0 && eaccess(repofile, W_OK) == -1) {
		pkg_emit_error("Insufficient privilege to update %s\n",
				repofile);
		rc = EPKG_ENOACCESS;
		goto cleanup;
	}

	if ((fd = repo_fetch_remote_tmp(repo, repo_db_archive, "txz", mtime, &rc)) == -1) {
		goto cleanup;
	}

	if ((rc = repo_archive_extract_file(fd, repo_db_file, repofile_unchecked, repo->pubkey, -1)) != EPKG_OK) {
		goto cleanup;
	}

	/* check if the repository is for valid architecture */
	if (access(repofile_unchecked, R_OK|W_OK) == -1) {
		pkg_emit_error("Archive file does not have repo.sqlite file");
		rc = EPKG_FATAL;
		goto cleanup;
	}
	if (sqlite3_open(repofile_unchecked, &sqlite) != SQLITE_OK) {
		unlink(repofile_unchecked);
		pkg_emit_error("Corrupted repository");
		rc = EPKG_FATAL;
		goto cleanup;
	}

	pkg_config_string(PKG_CONFIG_ABI, &myarch);

	req = sqlite3_mprintf("select group_concat(arch, ', ') from "
			"(select distinct arch from packages "
			"where arch not GLOB '%q')", myarch);

	if (get_sql_string(sqlite, req, &bad_abis) != EPKG_OK) {
		sqlite3_free(req);
		pkg_emit_error("Unable to query repository");
		rc = EPKG_FATAL;
		sqlite3_close(sqlite);
		goto cleanup;
	}

	if (bad_abis != NULL) {
		pkg_emit_error("At least one of the packages provided by "
				"the repository is not compatible with your ABI:\n"
				"    Your ABI: %s\n"
				"    Incompatible ABIs found: %s",
				myarch, bad_abis);
		rc = EPKG_FATAL;
		sqlite3_close(sqlite);
		goto cleanup;
	}

	/* register the packagesite */
	if (pkg_register_repo(repo, sqlite) != EPKG_OK) {
		sqlite3_close(sqlite);
		goto cleanup;
	}

	sqlite3_close(sqlite);
	sqlite3_shutdown();

	if (rename(repofile_unchecked, repofile) != 0) {
		pkg_emit_errno("rename", "");
		rc = EPKG_FATAL;
		goto cleanup;
	}

	if ((rc = remote_add_indexes(pkg_repo_ident(repo))) != EPKG_OK)
		goto cleanup;
	rc = EPKG_OK;

	cleanup:
	if (fd != -1)
		(void)close(fd);

	return (rc);
}

static int
pkg_add_from_manifest(FILE *f, char *buf, const char *origin, long offset,
		const char *manifest_digest, const char *local_arch, sqlite3 *sqlite,
		struct pkg_manifest_parser **parser, struct pkg **p)
{
	int rc = EPKG_OK;
	struct pkg *pkg;
	const char *local_origin, *pkg_arch;

	if (buf == NULL && fseek(f, offset, SEEK_SET) == -1) {
		pkg_emit_errno("fseek", "invalid manifest offset");
		return (EPKG_FATAL);
	}

	if (*p == NULL) {
		rc = pkg_new(p, PKG_REMOTE);
		if (rc != EPKG_OK)
			return (EPKG_FATAL);
	} else {
		pkg_reset(*p, PKG_REMOTE);
	}

	pkg = *p;

	pkg_manifest_parser_new(parser);
	if (buf == NULL) {
		rc = pkg_parse_manifest_file_ev(pkg, f, *parser);
	} else {
		rc = pkg_parse_manifest_ev(pkg, buf, offset, *parser);
	}
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
	if (pkg_arch == NULL || strcmp(pkg_arch, local_arch) != 0) {
		pkg_emit_error("package %s is built for %s arch, and local arch is %s",
				origin, pkg_arch ? pkg_arch : "NULL", local_arch);
		rc = EPKG_FATAL;
		goto cleanup;
	}

	rc = pkgdb_repo_add_package(pkg, NULL, sqlite, manifest_digest, true, false);

cleanup:
	return (rc);
}

struct pkg_increment_task_item {
	char *origin;
	char *digest;
	long offset;
	UT_hash_handle hh;
};

static void
pkg_update_increment_item_new(struct pkg_increment_task_item **head, const char *origin,
		const char *digest, long offset)
{
	struct pkg_increment_task_item *item;

	item = calloc(1, sizeof(struct pkg_increment_task_item));
	item->origin = strdup(origin);
	if (digest == NULL)
		digest = "";
	item->digest = strdup(digest);
	item->offset = offset;

	HASH_ADD_KEYPTR(hh, *head, item->origin, strlen(item->origin), item);
}

static int
pkg_update_incremental(const char *name, struct pkg_repo *repo, time_t *mtime)
{
	FILE *fmanifest = NULL, *fdigests = NULL;
	sqlite3 *sqlite = NULL;
	struct pkg *pkg = NULL;
	int rc = EPKG_FATAL;
	const char *origin, *digest, *offset;
	struct pkgdb_it *it = NULL;
	char *linebuf = NULL, *p;
	int updated = 0, removed = 0, added = 0, processed = 0;
	long num_offset;
	time_t local_t = *mtime;
	struct pkg_increment_task_item *ldel = NULL, *ladd = NULL,
			*item, *tmp_item;
	const char *myarch;
	struct pkg_manifest_parser *parser = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	char *map = MAP_FAILED;
	size_t len = 0;

	pkg_debug(1, "Pkgrepo, begin incremental update of '%s'", name);
	if ((rc = pkgdb_repo_open(name, false, &sqlite, false)) != EPKG_OK) {
		return (EPKG_FATAL);
	}

	if ((rc = pkgdb_repo_init(sqlite, false)) != EPKG_OK)
		goto cleanup;

	if ((rc = pkg_register_repo(repo, sqlite)) != EPKG_OK)
		goto cleanup;

	it = pkgdb_repo_origins(sqlite);
	if (it == NULL) {
		rc = EPKG_FATAL;
		goto cleanup;
	}

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		pkg_get(pkg, PKG_ORIGIN, &origin, PKG_DIGEST, &digest);
		pkg_update_increment_item_new(&ldel, origin, digest, 4);
	}

	fdigests = repo_fetch_remote_extract_tmp(repo,
			repo_digests_archive, "txz", &local_t,
			&rc, repo_digests_file);
	if (fdigests == NULL)
		goto cleanup;
	local_t = *mtime;
	fmanifest = repo_fetch_remote_extract_tmp(repo,
			repo_packagesite_archive, "txz", &local_t,
			&rc, repo_packagesite_file);
	if (fmanifest == NULL)
		goto cleanup;
	*mtime = local_t;
	fseek(fmanifest, 0, SEEK_END);
	len = ftell(fmanifest);

	pkg_debug(1, "Pkgrepo, reading new packagesite.yaml for '%s'", name);
	/* load the while digests */
	while ((linelen = getline(&linebuf, &linecap, fdigests)) > 0) {
		p = linebuf;
		origin = strsep(&p, ":");
		digest = strsep(&p, ":");
		offset = strsep(&p, ":");

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
		processed++;
		HASH_FIND_STR(ldel, __DECONST(char *, origin), item);
		if (item == NULL) {
			added++;
			pkg_update_increment_item_new(&ladd, origin, digest, num_offset);
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
				pkg_update_increment_item_new(&ladd, origin, digest, num_offset);
				updated++;
			}
		}
	}

	rc = EPKG_OK;

	pkg_debug(1, "Pkgrepo, removing old entries for '%s'", name);
	removed = HASH_COUNT(ldel) - updated;
	HASH_ITER(hh, ldel, item, tmp_item) {
		if (rc == EPKG_OK) {
			rc = pkgdb_repo_remove_package(item->origin);
		}
		free(item->origin);
		free(item->digest);
		HASH_DEL(ldel, item);
		free(item);
	}

	pkg_config_string(PKG_CONFIG_ABI, &myarch);

	pkg_debug(1, "Pkgrepo, pushing new entries for '%s'", name);
	pkg = NULL;

	if (len > 0 && len < SSIZE_MAX) {
		map = mmap(NULL, len, PROT_READ, MAP_SHARED, fileno(fmanifest), 0);
		fclose(fmanifest);
	}

	HASH_ITER(hh, ladd, item, tmp_item) {
		if (rc == EPKG_OK) {
			if (map != MAP_FAILED) {
				rc = pkg_add_from_manifest(NULL, map + item->offset, item->origin,
				    len - item->offset, item->digest, myarch, sqlite, &parser, &pkg);
			} else {
				rc = pkg_add_from_manifest(fmanifest, NULL, item->origin,
				    item->offset, item->digest, myarch, sqlite, &parser, &pkg);
			}
		}
		free(item->origin);
		free(item->digest);
		HASH_DEL(ladd, item);
		free(item);
	}
	pkg_manifest_parser_free(parser);
	pkg_emit_incremental_update(updated, removed, added, processed);

cleanup:
	if (pkg != NULL)
		pkg_free(pkg);
	if (it != NULL)
		pkgdb_it_free(it);
	if (pkgdb_repo_close(sqlite, rc == EPKG_OK) != EPKG_OK)
		rc = EPKG_FATAL;
	if (map == MAP_FAILED && fmanifest)
		fclose(fmanifest);
	if (fdigests)
		fclose(fdigests);
	if (map != MAP_FAILED)
		munmap(map, len);

	return (rc);
}

int
pkg_update(struct pkg_repo *repo, bool force)
{
	char repofile[MAXPATHLEN];

	const char *dbdir = NULL;
	struct stat st;
	time_t t = 0;
	sqlite3 *sqlite = NULL;
	char *req = NULL;
	int64_t res;

	sqlite3_initialize();

	if (!pkg_repo_enabled(repo))
		return (EPKG_OK);

	if (pkg_config_string(PKG_CONFIG_DBDIR, &dbdir) != EPKG_OK) {
		pkg_emit_error("Cant get dbdir config entry");
		return (EPKG_FATAL);
	}

	snprintf(repofile, sizeof(repofile), "%s/%s.sqlite", dbdir, pkg_repo_name(repo));

	if (stat(repofile, &st) != -1)
		t = force ? 0 : st.st_mtime;

	if (t != 0) {
		if (sqlite3_open(repofile, &sqlite) != SQLITE_OK) {
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
			unlink(repofile);
		}
	}

	res = pkg_update_incremental(repofile, repo, &t);
	if (res != EPKG_OK && res != EPKG_UPTODATE) {
		pkg_emit_notice("No digest falling back on legacy catalog format");

		/* Still try to do full upgrade */
		if ((res = pkg_update_full(repofile, repo, &t)) != EPKG_OK)
			goto cleanup;
	}

	res = EPKG_OK;
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
		utimes(repofile, ftimes);
	}

	return (res);
}
