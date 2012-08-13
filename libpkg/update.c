/*
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <archive.h>
#include <archive_entry.h>

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkgdb.h"

//#define EXTRACT_ARCHIVE_FLAGS  (ARCHIVE_EXTRACT_OWNER |ARCHIVE_EXTRACT_PERM)

/* Add indexes to the repo */
static int
remote_add_indexes(const char *reponame)
{
	struct pkgdb *db = NULL;
	int ret = EPKG_FATAL;

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK)
		goto cleanup;

	/* Initialize the remote remote */
	if (pkgdb_remote_init(db, reponame) != EPKG_OK)
		goto cleanup;

	ret = EPKG_OK;

	cleanup:
	if (db)
		pkgdb_close(db);
	return (ret);
}

int
pkg_update(const char *name, const char *packagesite, bool force)
{
	char url[MAXPATHLEN];
	struct archive *a = NULL;
	struct archive_entry *ae = NULL;
	char repofile[MAXPATHLEN];
	char repofile_unchecked[MAXPATHLEN];
	char tmp[MAXPATHLEN];
	const char *dbdir = NULL;
	const char *repokey;
	unsigned char *sig = NULL;
	int siglen = 0;
	int rc = EPKG_FATAL, ret;
	struct stat st;
	time_t t = 0;
	sqlite3 *sqlite;
	char *archreq = NULL;
	const char *myarch;
	int64_t res;
	const char *tmpdir;

	snprintf(url, MAXPATHLEN, "%s/repo.txz", packagesite);

	tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL)
		tmpdir = "/tmp";
	strlcpy(tmp, tmpdir, sizeof(tmp));
	strlcat(tmp, "/repo.txz.XXXXXX", sizeof(tmp));

	if (mktemp(tmp) == NULL) {
		pkg_emit_error("Could not create temporary file %s, "
		    "aborting update.\n", tmp);
		return (EPKG_FATAL);
	}

	if (pkg_config_string(PKG_CONFIG_DBDIR, &dbdir) != EPKG_OK) {
		pkg_emit_error("Cant get dbdir config entry");
		return (EPKG_FATAL);
	}

	snprintf(repofile, sizeof(repofile), "%s/%s.sqlite", dbdir, name);
	if (force)
		t = 0;		/* Always fetch */
	else {
		if (stat(repofile, &st) != -1) {
			t = st.st_mtime;
			/* add 1 minute to the timestamp because
			 * repo.sqlite is always newer than repo.txz,
			 * 1 minute should be enough.
			 */
			t += 60;
		}
	}

	if ((rc = pkg_fetch_file(url, tmp, t)) != EPKG_OK) {
		/*
		 * No need to unlink(tmp) here as it is already
		 * done in pkg_fetch_file() in case fetch failed.
		 */
		return (rc);
	}

	a = archive_read_new();
	archive_read_support_compression_all(a);
	archive_read_support_format_tar(a);

	archive_read_open_filename(a, tmp, 4096);

	while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
		if (strcmp(archive_entry_pathname(ae), "repo.sqlite") == 0) {
			snprintf(repofile_unchecked, sizeof(repofile_unchecked),
			    "%s.unchecked", repofile);
			archive_entry_set_pathname(ae, repofile_unchecked);

			/*
			 * The repo should be owned by root and not writable
			 */
			archive_entry_set_uid(ae, 0);
			archive_entry_set_gid(ae, 0);
			archive_entry_set_perm(ae, 0644);

			archive_read_extract(a, ae, EXTRACT_ARCHIVE_FLAGS);
		}
		if (strcmp(archive_entry_pathname(ae), "signature") == 0) {
			siglen = archive_entry_size(ae);
			sig = malloc(siglen);
			archive_read_data(a, sig, siglen);
		}
	}

	if (pkg_config_string(PKG_CONFIG_REPOKEY, &repokey) != EPKG_OK) {
		if (sig != NULL)
			free(sig);
		
		return (EPKG_FATAL);
	}

	if (repokey != NULL) {
		if (sig != NULL) {
			ret = rsa_verify(repofile_unchecked, repokey,
			    sig, siglen - 1);
			if (ret != EPKG_OK) {
				pkg_emit_error("Invalid signature, "
				    "removing repository.\n");
				unlink(repofile_unchecked);
				free(sig);
				rc = EPKG_FATAL;
				goto cleanup;
			}
			free(sig);
		} else {
			pkg_emit_error("No signature found in the repository.  "
			    "Can not validate against %s key.", repokey);
			rc = EPKG_FATAL;
			unlink(repofile_unchecked);
			goto cleanup;
		}
	}

	/* check is the repository is for valid architecture */
	sqlite3_initialize();

	if (sqlite3_open(repofile_unchecked, &sqlite) != SQLITE_OK) {
		unlink(repofile_unchecked);
		pkg_emit_error("Corrupted repository");
		rc = EPKG_FATAL;
		goto cleanup;
	}

	pkg_config_string(PKG_CONFIG_ABI, &myarch);

	archreq = sqlite3_mprintf("select count(arch) from packages "
	    "where arch not GLOB '%q'", myarch);
	if (get_pragma(sqlite, archreq, &res) != EPKG_OK) {
		sqlite3_free(archreq);
		pkg_emit_error("Unable to query repository");
		rc = EPKG_FATAL;
		sqlite3_close(sqlite);
		goto cleanup;
	}

	if (res > 0) {
		pkg_emit_error("At least one of the packages provided by"
		    "the repository is not compatible with your abi: %s",
		    myarch);
		rc = EPKG_FATAL;
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

	if ((rc = remote_add_indexes(name)) != EPKG_OK)
		goto cleanup;

	rc = EPKG_OK;

	cleanup:
	if (a != NULL)
		archive_read_finish(a);

	(void)unlink(tmp);

	return (rc);
}
