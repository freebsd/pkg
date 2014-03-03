/*-
 * Copyright (c) 2012-2014 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include <dirent.h>
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

struct sig_cert {
	char name[MAXPATHLEN];
	unsigned char *sig;
	int siglen;
	unsigned char *cert;
	int certlen;
	UT_hash_handle hh;
	bool trusted;
};

typedef enum {
	HASH_UNKNOWN,
	HASH_SHA256,
} hash_t;

struct fingerprint {
	hash_t type;
	char hash[BUFSIZ];
	UT_hash_handle hh;
};

/* Return opened file descriptor */
static int
repo_fetch_remote_tmp(struct pkg_repo *repo, const char *filename, const char *extension, time_t *t, int *rc)
{
	char url[MAXPATHLEN];
	char tmp[MAXPATHLEN];
	int fd;
	mode_t mask;
	const char *tmpdir;

	snprintf(url, sizeof(url), "%s/%s.%s", pkg_repo_url(repo), filename, extension);

	tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL)
		tmpdir = "/tmp";
	mkdirs(tmpdir);
	snprintf(tmp, sizeof(tmp), "%s/%s.%s.XXXXXX", tmpdir, filename, extension);

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

static bool
has_ext(const char *path, const char *ext)
{
	size_t n, l;
	const char *p = NULL;

	n = strlen(path);
	l = strlen(ext);
	p = &path[n - l];

	if (strcmp(p, ext) == 0)
		return (true);

	return (false);
}

static struct fingerprint *
parse_fingerprint(ucl_object_t *obj)
{
	ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	const char *function = NULL, *fp = NULL;
	hash_t fct = HASH_UNKNOWN;
	struct fingerprint *f = NULL;
	const char *key;

	while ((cur = ucl_iterate_object(obj, &it, true))) {
		key = ucl_object_key(cur);
		if (cur->type != UCL_STRING)
			continue;

		if (strcasecmp(key, "function") == 0) {
			function = ucl_object_tostring(cur);
			continue;
		}

		if (strcasecmp(key, "fingerprint") == 0) {
			fp = ucl_object_tostring(cur);
			continue;
		}
	}

	if (fp == NULL || function == NULL)
		return (NULL);

	if (strcasecmp(function, "sha256") == 0)
		fct = HASH_SHA256;

	if (fct == HASH_UNKNOWN) {
		pkg_emit_error("Unsupported hashing function: %s", function);
		return (NULL);
	}

	f = calloc(1, sizeof(struct fingerprint));
	f->type = fct;
	strlcpy(f->hash, fp, sizeof(f->hash));

	return (f);
}

static struct fingerprint *
load_fingerprint(const char *dir, const char *filename)
{
	ucl_object_t *obj = NULL;
	struct ucl_parser *p = NULL;
	char path[MAXPATHLEN];
	struct fingerprint *f = NULL;

	snprintf(path, sizeof(path), "%s/%s", dir, filename);

	p = ucl_parser_new(0);

	if (!ucl_parser_add_file(p, path)) {
		pkg_emit_error("%s", ucl_parser_get_error(p));
		ucl_parser_free(p);
		return (NULL);
	}

	obj = ucl_parser_get_object(p);

	if (obj->type == UCL_OBJECT)
		f = parse_fingerprint(obj);

	ucl_object_free(obj);
	ucl_parser_free(p);

	return (f);
}

static int
load_fingerprints(const char *path, struct fingerprint **f)
{
	DIR *d;
	struct dirent *ent;
	struct fingerprint *finger = NULL;

	*f = NULL;

	if ((d = opendir(path)) == NULL)
		return (EPKG_FATAL);

	while ((ent = readdir(d))) {
		if (strcmp(ent->d_name, ".") == 0 ||
		    strcmp(ent->d_name, "..") == 0)
			continue;
		finger = load_fingerprint(path, ent->d_name);
		if (finger != NULL)
			HASH_ADD_STR(*f, hash, finger);
	}

	closedir(d);

	return (EPKG_OK);
}

static int
repo_archive_extract_file(int fd, const char *file, const char *dest, struct pkg_repo *repo, int dest_fd)
{
	struct archive *a = NULL;
	struct archive_entry *ae = NULL;
	struct sig_cert *sc = NULL;
	struct sig_cert *s = NULL, *stmp = NULL;
	struct fingerprint *trusted = NULL;
	struct fingerprint *revoked = NULL;
	struct fingerprint *f = NULL;
	unsigned char *sig = NULL;
	int siglen = 0, ret, rc = EPKG_OK;
	char key[MAXPATHLEN], path[MAXPATHLEN];
	char hash[SHA256_DIGEST_LENGTH * 2 + 1];
	int nbgood = 0;

	pkg_debug(1, "PkgRepo: extracting repo %", pkg_repo_name(repo));

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
		if (pkg_repo_signature_type(repo) == SIG_PUBKEY &&
		    strcmp(archive_entry_pathname(ae), "signature") == 0) {
			siglen = archive_entry_size(ae);
			sig = malloc(siglen);
			archive_read_data(a, sig, siglen);
		}

		if (pkg_repo_signature_type(repo) == SIG_FINGERPRINT) {
			if (has_ext(archive_entry_pathname(ae), ".sig")) {
				snprintf(key, sizeof(key), "%.*s",
				    (int) strlen(archive_entry_pathname(ae)) - 4,
				    archive_entry_pathname(ae));
				HASH_FIND_STR(sc, key, s);
				if (s == NULL) {
					s = calloc(1, sizeof(struct sig_cert));
					strlcpy(s->name, key, sizeof(s->name));
					HASH_ADD_STR(sc, name, s);
				}
				s->siglen = archive_entry_size(ae);
				s->sig = malloc(s->siglen);
				archive_read_data(a, s->sig, s->siglen);
			}
			if (has_ext(archive_entry_pathname(ae), ".pub")) {
				snprintf(key, sizeof(key), "%.*s",
				    (int) strlen(archive_entry_pathname(ae)) - 4,
				    archive_entry_pathname(ae));
				HASH_FIND_STR(sc, key, s);
				if (s == NULL) {
					s = calloc(1, sizeof(struct sig_cert));
					strlcpy(s->name, key, sizeof(s->name));
					HASH_ADD_STR(sc, name, s);
				}
				s->certlen = archive_entry_size(ae);
				s->cert = malloc(s->certlen);
				archive_read_data(a, s->cert, s->certlen);
			}
		}
	}

	if (pkg_repo_signature_type(repo) == SIG_PUBKEY) {
		if (sig == NULL) {
			pkg_emit_error("No signature found in the repository.  "
					"Can not validate against %s key.", pkg_repo_key(repo));
			rc = EPKG_FATAL;
			goto cleanup;
		}
		ret = rsa_verify(dest, pkg_repo_key(repo),
		    sig, siglen - 1, dest_fd);
		if (ret != EPKG_OK) {
			pkg_emit_error("Invalid signature, "
					"removing repository.");
			free(sig);
			rc = EPKG_FATAL;
			goto cleanup;
		}
		free(sig);
	} else if (pkg_repo_signature_type(repo) == SIG_FINGERPRINT) {
		if (HASH_COUNT(sc) == 0) {
			pkg_emit_error("No signature found");
			rc = EPKG_FATAL;
			goto cleanup;
		}

		/* load fingerprints */
		snprintf(path, sizeof(path), "%s/trusted", pkg_repo_fingerprints(repo));
		if ((load_fingerprints(path, &trusted)) != EPKG_OK) {
			pkg_emit_error("Error loading trusted certificates");
			rc = EPKG_FATAL;
			goto cleanup;
		}

		if (HASH_COUNT(trusted) == 0) {
			pkg_emit_error("No trusted certificates");
			rc = EPKG_FATAL;
			goto cleanup;
		}

		snprintf(path, sizeof(path), "%s/revoked", pkg_repo_fingerprints(repo));
		if ((load_fingerprints(path, &revoked)) != EPKG_OK) {
			pkg_emit_error("Error loading revoked certificates");
			rc = EPKG_FATAL;
			goto cleanup;
		}

		HASH_ITER(hh, sc, s, stmp) {
			if (s->sig == NULL || s->cert == NULL) {
				pkg_emit_error("Number of signatures and certificates "
				    "mismatch");
				rc = EPKG_FATAL;
				goto cleanup;
			}
			s->trusted = false;
			sha256_buf(s->cert, s->certlen, hash);
			HASH_FIND_STR(revoked, hash, f);
			if (f != NULL) {
				pkg_emit_error("At least one of the "
				    " certificates has been revoked");
				rc = EPKG_FATAL;
				goto cleanup;
			}

			HASH_FIND_STR(trusted, hash, f);
			if (f != NULL) {
				nbgood++;
				s->trusted = true;
			}
		}

		if (nbgood == 0) {
			pkg_emit_error("No trusted certificate found");
			rc = EPKG_FATAL;
			goto cleanup;
		}

		nbgood = 0;

		HASH_ITER(hh, sc, s, stmp) {
			ret = rsa_verify_cert(dest, s->cert, s->certlen, s->sig, s->siglen, dest_fd);
			if (ret == EPKG_OK && s->trusted)
				nbgood++;
		}

		if (nbgood == 0) {
			pkg_emit_error("No trusted certificate has been used "
			    "to sign the repository");
			rc = EPKG_FATAL;
			goto cleanup;
		}
	}

cleanup:
	if (rc != EPKG_OK && dest != NULL)
		unlink(dest);

	if (a != NULL) {
		archive_read_close(a);
		archive_read_free(a);
	}

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
	snprintf(tmp, sizeof(tmp), "%s/%s.XXXXXX", tmpdir, archive_file);

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
	if (repo_archive_extract_file(fd, archive_file, NULL, repo, dest_fd) != EPKG_OK) {
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
pkg_add_from_manifest(char *buf, const char *origin, long offset,
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
pkg_update_increment_item_new(struct pkg_increment_task_item **head, const char *origin,
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
pkg_parse_conflicts_file(FILE *f, sqlite3 *sqlite)
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
pkg_update_incremental(const char *name, struct pkg_repo *repo, time_t *mtime)
{
	FILE *fmanifest = NULL, *fdigests = NULL, *fconflicts = NULL;
	sqlite3 *sqlite = NULL;
	struct pkg *pkg = NULL;
	int rc = EPKG_FATAL;
	const char *origin, *digest, *offset, *length;
	struct pkgdb_it *it = NULL;
	char *linebuf = NULL, *p;
	int updated = 0, removed = 0, added = 0, processed = 0;
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

	pkg_debug(1, "Pkgrepo, begin incremental update of '%s'", name);
	if ((rc = pkgdb_repo_open(name, false, &sqlite)) != EPKG_OK) {
		return (EPKG_FATAL);
	}

	if ((rc = pkgdb_repo_init(sqlite)) != EPKG_OK) {
		goto cleanup;
	}

	if ((rc = pkg_register_repo(repo, sqlite)) != EPKG_OK)
		goto cleanup;

	it = pkgdb_repo_origins(sqlite);
	if (it == NULL) {
		rc = EPKG_FATAL;
		goto cleanup;
	}

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		pkg_get(pkg, PKG_ORIGIN, &origin, PKG_DIGEST, &digest);
		pkg_update_increment_item_new(&ldel, origin, digest, 4, 0);
	}

	fdigests = repo_fetch_remote_extract_tmp(repo,
			repo_digests_archive, "txz", &local_t,
			&rc, repo_digests_file);
	if (fdigests == NULL)
		goto cleanup;
	digest_t = local_t;
	local_t = *mtime;
	fmanifest = repo_fetch_remote_extract_tmp(repo,
			repo_packagesite_archive, "txz", &local_t,
			&rc, repo_packagesite_file);
	if (fmanifest == NULL)
		goto cleanup;
	packagesite_t = digest_t;
	*mtime = packagesite_t > digest_t ? packagesite_t : digest_t;
	fconflicts = repo_fetch_remote_extract_tmp(repo,
			repo_conflicts_archive, "txz", &local_t,
			&rc, repo_conflicts_file);
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
			pkg_update_increment_item_new(&ladd, origin, digest, num_offset, num_length);
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
				pkg_update_increment_item_new(&ladd, origin, digest, num_offset, num_length);
				updated++;
			}
		}
	}

	rc = EPKG_OK;

	pkg_debug(1, "Pkgrepo, removing old entries for '%s'", name);
	removed = HASH_COUNT(ldel);
	HASH_ITER(hh, ldel, item, tmp_item) {
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

	HASH_ITER(hh, ladd, item, tmp_item) {
		if (rc == EPKG_OK) {
			if (item->length != 0) {
				rc = pkg_add_from_manifest(map + item->offset, item->origin,
						item->length, item->digest,
						sqlite, &keys, &pkg);
			}
			else {
				rc = pkg_add_from_manifest(map + item->offset, item->origin,
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
	if (fconflicts)
		fclose(fconflicts);
	if (map != MAP_FAILED)
		munmap(map, len);
	if (linebuf != NULL)
		free(linebuf);

	pkgdb_repo_close(sqlite, rc == EPKG_OK);

	return (rc);
}

int
repo_update_binary_pkgs(struct pkg_repo *repo, bool force)
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

	pkg_debug(1, "PkgRepo: verifying update for %s", pkg_repo_name(repo));
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
		pkg_emit_notice("Unable to find catalogs");
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

int
pkg_update(struct pkg_repo *repo, bool force)
{
	return (repo->update(repo, force));
}
