/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
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

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/utils.h"

int
pkg_delete(struct pkg *pkg, struct pkgdb *db, unsigned flags)
{
	struct pkg_dep	*rdep = NULL;
	int		 ret;
	bool		 handle_rc = false;
	bool		 locked;
	const char	*origin;

	assert(pkg != NULL);
	assert(db != NULL);

	/*
	 * Do not trust the existing entries as it may have changed if we
	 * delete packages in batch.
	 */
	pkg_list_free(pkg, PKG_RDEPS);

	/*
	 * Ensure that we have all the informations we need
	 */
	if ((ret = pkgdb_load_rdeps(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_load_files(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_load_dirs(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_load_scripts(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_load_mtree(db, pkg)) != EPKG_OK)
		return (ret);

	if (flags & PKG_DELETE_UPGRADE)
		pkg_emit_upgrade_begin(pkg);
	else
		pkg_emit_deinstall_begin(pkg);

	/* If the package is locked */
	pkg_get(pkg, PKG_LOCKED, &locked);
	if (locked) {
		pkg_emit_locked(pkg);
		return (EPKG_LOCKED);
	}

	/* If there are dependencies */
	if (pkg_rdeps(pkg, &rdep) == EPKG_OK) {
		pkg_emit_required(pkg, flags & PKG_DELETE_FORCE);
		if ((flags & PKG_DELETE_FORCE) == 0)
			return (EPKG_REQUIRED);
	}

	/*
	 * stop the different related services if the users do want that
	 * and that the service is running
	 */
	pkg_config_bool(PKG_CONFIG_HANDLE_RC_SCRIPTS, &handle_rc);
	if (handle_rc)
		pkg_start_stop_rc_scripts(pkg, PKG_RC_STOP);

	if (flags & PKG_DELETE_UPGRADE) {
		ret = pkg_script_run(pkg, PKG_SCRIPT_PRE_UPGRADE);
		if (ret != EPKG_OK)
			return (ret);
	} else {
		ret = pkg_script_run(pkg, PKG_SCRIPT_PRE_DEINSTALL);
		if (ret != EPKG_OK)
			return (ret);
	}

	if ((ret = pkg_delete_files(pkg, flags & PKG_DELETE_FORCE)) != EPKG_OK)
		return (ret);

	if ((flags & PKG_DELETE_UPGRADE) == 0) {
		ret = pkg_script_run(pkg, PKG_SCRIPT_POST_DEINSTALL);
		if (ret != EPKG_OK)
			return (ret);
	}

	ret = pkg_delete_dirs(db, pkg, flags & PKG_DELETE_FORCE);
	if (ret != EPKG_OK)
		return (ret);

	if ((flags & PKG_DELETE_UPGRADE) == 0)
		pkg_emit_deinstall_finished(pkg);

	pkg_get(pkg, PKG_ORIGIN, &origin);
	return (pkgdb_unregister_pkg(db, origin));
}

int
pkg_delete_files(struct pkg *pkg, bool force)
{
	struct pkg_file	*file = NULL;
	char		 sha256[SHA256_DIGEST_LENGTH * 2 + 1];
	const char	*path;

	while (pkg_files(pkg, &file) == EPKG_OK) {
		const char *sum = pkg_file_cksum(file);

		if (file->keep == 1)
			continue;

		path = pkg_file_path(file);

		/* Regular files and links */
		/* check sha256 */
		if (!force && sum[0] != '\0') {
			if (sha256_file(path, sha256) != EPKG_OK)
				continue;
			if (strcmp(sha256, sum)) {
				pkg_emit_error("%s fails original SHA256 "
				    "checksum, not removing", path);
				continue;
			}
		}

		if (unlink(path) == -1) {
			pkg_emit_errno("unlink", path);
			continue;
		}
	}

	return (EPKG_OK);
}

int
pkg_delete_dirs(__unused struct pkgdb *db, struct pkg *pkg, bool force)
{
	struct pkg_dir	*dir = NULL;

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		if (dir->keep == 1)
			continue;

		if (pkg_dir_try(dir)) {
			if (rmdir(pkg_dir_path(dir)) == -1 &&
			    errno != ENOTEMPTY && errno != EBUSY && !force)
				pkg_emit_errno("rmdir", pkg_dir_path(dir));
		} else {
			if (rmdir(pkg_dir_path(dir)) == -1 && !force)
				pkg_emit_errno("rmdir", pkg_dir_path(dir));
		}
	}

	return (EPKG_OK);
}
