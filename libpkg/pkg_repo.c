/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 *
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
#include <dirent.h>
#define _WITH_GETLINE
#include <stdio.h>
#include <stdbool.h>
#include <sysexits.h>
#include <unistd.h>
#include <errno.h>

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/repodb.h"
#include "private/thd_repo.h"


struct sig_cert {
	char name[MAXPATHLEN];
	unsigned char *sig;
	int siglen;
	unsigned char *cert;
	int certlen;
	UT_hash_handle hh;
	bool trusted;
};

int
pkg_repo_fetch_package(struct pkg *pkg)
{
	char dest[MAXPATHLEN];
	char url[MAXPATHLEN];
	int fetched = 0;
	char cksum[SHA256_DIGEST_LENGTH * 2 +1];
	char *path = NULL;
	const char *packagesite = NULL;
	const char *cachedir = NULL;
	int retcode = EPKG_OK;
	const char *sum, *name, *version, *reponame;
	struct pkg_repo *repo;

	assert((pkg->type & PKG_REMOTE) == PKG_REMOTE);

	cachedir = pkg_object_string(pkg_config_get("PKG_CACHEDIR"));

	pkg_get(pkg, PKG_REPONAME, &reponame,
	    PKG_CKSUM, &sum, PKG_NAME, &name, PKG_VERSION, &version);

	pkg_snprintf(dest, sizeof(dest), "%S/%n-%v-%z",
			cachedir, pkg, pkg, pkg);

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
	repo = pkg_repo_find_name(reponame);
	packagesite = pkg_repo_url(repo);

	if (packagesite == NULL || packagesite[0] == '\0') {
		pkg_emit_error("PACKAGESITE is not defined");
		retcode = 1;
		goto cleanup;
	}

	if (packagesite[strlen(packagesite) - 1] == '/')
		pkg_snprintf(url, sizeof(url), "%S%R", packagesite, pkg);
	else
		pkg_snprintf(url, sizeof(url), "%S/%R", packagesite, pkg);

	if (strncasecmp(packagesite, "file://", 7) == 0) {
		pkg_set(pkg, PKG_REPOPATH, url + 7);
		return (EPKG_OK);
	}

	retcode = pkg_fetch_file(repo, url, dest, 0);
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
				return (pkg_repo_fetch_package(pkg));
			}
		}

	cleanup:
	if (retcode != EPKG_OK)
		unlink(dest);

	return (retcode);
}

static int
pkg_repo_fetch_remote_tmp(struct pkg_repo *repo,
		const char *filename, const char *extension, time_t *t, int *rc)
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
pkg_repo_file_has_ext(const char *path, const char *ext)
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

static int
pkg_repo_archive_extract_file(int fd, const char *file,
		const char *dest, struct pkg_repo *repo, int dest_fd)
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
			if (pkg_repo_file_has_ext(archive_entry_pathname(ae), ".sig")) {
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
			if (pkg_repo_file_has_ext(archive_entry_pathname(ae), ".pub")) {
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
		if ((pkg_repo_load_fingerprints(path, &trusted)) != EPKG_OK) {
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
		if ((pkg_repo_load_fingerprints(path, &revoked)) != EPKG_OK) {
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

FILE *
pkg_repo_fetch_remote_extract_tmp(struct pkg_repo *repo, const char *filename,
		const char *extension, time_t *t, int *rc, const char *archive_file)
{
	int fd, dest_fd;
	mode_t mask;
	FILE *res = NULL;
	const char *tmpdir;
	char tmp[MAXPATHLEN];

	fd = pkg_repo_fetch_remote_tmp(repo, filename, extension, t, rc);
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
	if (pkg_repo_archive_extract_file(fd, archive_file, NULL, repo, dest_fd) != EPKG_OK) {
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

static struct fingerprint *
pkg_repo_parse_fingerprint(ucl_object_t *obj)
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
pkg_repo_load_fingerprint(const char *dir, const char *filename)
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
		f = pkg_repo_parse_fingerprint(obj);

	ucl_object_free(obj);
	ucl_parser_free(p);

	return (f);
}

int
pkg_repo_load_fingerprints(const char *path, struct fingerprint **f)
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
		finger = pkg_repo_load_fingerprint(path, ent->d_name);
		if (finger != NULL)
			HASH_ADD_STR(*f, hash, finger);
	}

	closedir(d);

	return (EPKG_OK);
}
