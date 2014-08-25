/*-
 * Copyright (c) 2011-2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
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

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/utils.h"

int
pkg_delete(struct pkg *pkg, struct pkgdb *db, unsigned flags)
{
	int		 ret;
	bool		 handle_rc = false;
	int64_t		id;
	const unsigned load_flags = PKG_LOAD_RDEPS|PKG_LOAD_FILES|PKG_LOAD_DIRS|
					PKG_LOAD_SCRIPTS|PKG_LOAD_MTREE|PKG_LOAD_ANNOTATIONS;

	assert(pkg != NULL);
	assert(db != NULL);

	if (pkgdb_ensure_loaded(db, pkg, load_flags) != EPKG_OK)
		return (EPKG_FATAL);

	if ((flags & PKG_DELETE_UPGRADE) == 0)
		pkg_emit_deinstall_begin(pkg);

	/* If the package is locked */
	if (pkg_is_locked(pkg)) {
		pkg_emit_locked(pkg);
		return (EPKG_LOCKED);
	}

	/*
	 * stop the different related services if the users do want that
	 * and that the service is running
	 */
	handle_rc = pkg_object_bool(pkg_config_get("HANDLE_RC_SCRIPTS"));
	if (handle_rc)
		pkg_start_stop_rc_scripts(pkg, PKG_RC_STOP);

	if ((flags & PKG_DELETE_NOSCRIPT) == 0) {
		if (flags & PKG_DELETE_UPGRADE) {
			ret = pkg_script_run(pkg, PKG_SCRIPT_PRE_UPGRADE);
			if (ret != EPKG_OK)
				return (ret);
		} else {
			ret = pkg_script_run(pkg, PKG_SCRIPT_PRE_DEINSTALL);
			if (ret != EPKG_OK)
				return (ret);
		}
	}

	if ((ret = pkg_delete_files(pkg, flags & PKG_DELETE_FORCE ? 1 : 0))
            != EPKG_OK)
		return (ret);

	if ((flags & (PKG_DELETE_NOSCRIPT | PKG_DELETE_UPGRADE)) == 0) {
		ret = pkg_script_run(pkg, PKG_SCRIPT_POST_DEINSTALL);
		if (ret != EPKG_OK)
			return (ret);
	}

	ret = pkg_delete_dirs(db, pkg);
	if (ret != EPKG_OK)
		return (ret);

	if ((flags & PKG_DELETE_UPGRADE) == 0)
		pkg_emit_deinstall_finished(pkg);

	pkg_get(pkg, PKG_ROWID, &id);

	return (pkgdb_unregister_pkg(db, id));
}

void
pkg_delete_file(struct pkg *pkg, struct pkg_file *file, unsigned force)
{
	const char *sum = pkg_file_cksum(file);
	const char *path;
	struct stat st;
	char sha256[SHA256_DIGEST_LENGTH * 2 + 1];

	pkg_open_root_fd(pkg);

	path = pkg_file_path(file);
	path++;

	/* Regular files and links */
	/* check sha256 */
	if (!force && sum[0] != '\0') {
		if (fstatat(pkg->rootfd, path, &st, AT_SYMLINK_NOFOLLOW) == -1) {
			pkg_emit_error("cannot stat %s: %s", path, strerror(errno));
			return;
		}
		if (S_ISLNK(st.st_mode)) {
			if (pkg_symlink_cksumat(pkg->rootfd, path, NULL,
			    sha256) != EPKG_OK)
				return;
		}
		else {
			if (sha256_fileat(pkg->rootfd, path, sha256) != EPKG_OK)
				return;
		}
		if (strcmp(sha256, sum)) {
			pkg_emit_error("%s fails original SHA256 "
				"checksum, not removing", path);
			return;
		}
	}

	if (unlinkat(pkg->rootfd, path, 0) == -1) {
		if (force < 2)
			pkg_emit_errno("unlinkat", path);
		return;
	}
}

int
pkg_delete_files(struct pkg *pkg, unsigned force)
	/* force: 0 ... be careful and vocal about it. 
	 *        1 ... remove files without bothering about checksums.
	 *        2 ... like 1, but remain silent if removal fails.
	 */
{
	struct pkg_file	*file = NULL;

	int		nfiles, cur_file = 0;

	nfiles = HASH_COUNT(pkg->files);

	if (nfiles == 0)
		return (EPKG_OK);

	pkg_emit_delete_files_begin(pkg);
	pkg_emit_progress_start(NULL);

	while (pkg_files(pkg, &file) == EPKG_OK) {
		pkg_emit_progress_tick(cur_file++, nfiles);

		if (file->keep == 1)
			continue;
		pkg_delete_file(pkg, file, force);
	}

	pkg_emit_progress_tick(nfiles, nfiles);
	pkg_emit_delete_files_finished(pkg);

	return (EPKG_OK);
}

void
pkg_delete_dir(struct pkg *pkg, struct pkg_dir *dir)
{
	const char *path;
	pkg_open_root_fd(pkg);

	path = pkg_dir_path(dir);
	/* remove the first / */
	path++;

	if (unlinkat(pkg->rootfd, path, AT_REMOVEDIR) == -1 &&
	    errno != ENOTEMPTY && errno != EBUSY)
		pkg_emit_errno("rmdir", pkg_dir_path(dir));
}

int
pkg_delete_dirs(__unused struct pkgdb *db, struct pkg *pkg)
{
	struct pkg_dir		*dir = NULL;

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		if (dir->keep == 1)
			continue;

		pkg_delete_dir(pkg, dir);
	}

	return (EPKG_OK);
}
