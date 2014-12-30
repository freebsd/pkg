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

#include <bsd_compat.h>

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
	const unsigned load_flags = PKG_LOAD_RDEPS|PKG_LOAD_FILES|PKG_LOAD_DIRS|
					PKG_LOAD_SCRIPTS|PKG_LOAD_ANNOTATIONS;

	assert(pkg != NULL);
	assert(db != NULL);

	if (pkgdb_ensure_loaded(db, pkg, load_flags) != EPKG_OK)
		return (EPKG_FATAL);

	if ((flags & PKG_DELETE_UPGRADE) == 0)
		pkg_emit_deinstall_begin(pkg);

	/* If the package is locked */
	if (pkg->locked) {
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

	ret = pkg_delete_dirs(db, pkg, NULL);
	if (ret != EPKG_OK)
		return (ret);

	if ((flags & PKG_DELETE_UPGRADE) == 0)
		pkg_emit_deinstall_finished(pkg);

	return (pkgdb_unregister_pkg(db, pkg->id));
}

void
pkg_add_dir_to_del(struct pkg *pkg, const char *file, const char *dir)
{
	char path[MAXPATHLEN];
	char *tmp;
	size_t i, len, len2;

	strlcpy(path, file != NULL ? file : dir, MAXPATHLEN);

	if (file != NULL) {
		tmp = strrchr(path, '/');
		tmp[1] = '\0';
	}

	len = strlen(path);

	/* make sure to finish by a / */
	if (path[len - 1] != '/') {
		path[len] = '/';
		len++;
		path[len] = '\0';
	}

	for (i = 0; i < pkg->dir_to_del_len ; i++) {
		len2 = strlen(pkg->dir_to_del[i]);
		if (len2 >= len && strncmp(path, pkg->dir_to_del[i], len) == 0)
			return;

		if (strncmp(path, pkg->dir_to_del[i], len2) == 0) {
			pkg_debug(1, "Replacing in deletion %s with %s",
			    pkg->dir_to_del[i], path);
			free(pkg->dir_to_del[i]);
			pkg->dir_to_del[i] = strdup(path);
			return;
		}
	}

	pkg_debug(1, "Adding to deletion %s", path);

	if (pkg->dir_to_del_len + 1 > pkg->dir_to_del_cap) {
		pkg->dir_to_del_cap += 64;
		pkg->dir_to_del = reallocf(pkg->dir_to_del,
		    pkg->dir_to_del_cap * sizeof(char *));
	}

	pkg->dir_to_del[pkg->dir_to_del_len++] = strdup(path);
}

static void
rmdir_p(struct pkgdb *db, struct pkg *pkg, char *dir, const char *prefix_r)
{
	char *tmp;
	int64_t cnt;
	char fullpath[MAXPATHLEN];
	size_t len;

	len = snprintf(fullpath, sizeof(fullpath), "/%s", dir);
	while (fullpath[len -1] == '/') {
		fullpath[len - 1] = '\0';
		len--;
	}
	if (pkgdb_is_dir_used(db, pkg, fullpath, &cnt) != EPKG_OK)
		return;

	pkg_debug(1, "Number of packages owning the directory '%s': %d",
	    fullpath, cnt);
	/*
	 * At this moment the package we are removing have already been removed
	 * from the local database so if anything else is owning the directory
	 * that is another package meaning only remove the diretory is cnt == 0
	 */
	if (cnt > 0)
		return;

	if (strcmp(prefix_r, fullpath + 1) == 0)
		return;

	pkg_debug(1, "removing directory %s", fullpath);
	if (unlinkat(pkg->rootfd, dir, AT_REMOVEDIR) == -1) {
		if (errno != ENOTEMPTY && errno != EBUSY)
			pkg_emit_errno("unlinkat", dir);
		/* If the directory was already removed by a bogus script, continue removing parents */
		if (errno != ENOENT)
			return;
	}

	/* No recursivity for packages out of the prefix */
	if (strncmp(prefix_r, dir, strlen(prefix_r)) != 0)
		return;

	/* remove the trailing '/' */
	tmp = strrchr(dir, '/');
	if (tmp == dir)
		return;

	tmp[0] = '\0';
	tmp = strrchr(dir, '/');
	if (tmp == NULL)
		return;

	tmp[1] = '\0';

	rmdir_p(db, pkg, dir, prefix_r);
}

static void
pkg_effective_rmdir(struct pkgdb *db, struct pkg *pkg)
{
	char prefix_r[MAXPATHLEN];
	size_t i;

	snprintf(prefix_r, sizeof(prefix_r), "%s", pkg->prefix + 1);
	for (i = 0; i < pkg->dir_to_del_len; i++)
		rmdir_p(db, pkg, pkg->dir_to_del[i], prefix_r);
}

void
pkg_delete_file(struct pkg *pkg, struct pkg_file *file, unsigned force)
{
	const char *path;
	const char *prefix_rel;
	struct stat st;
	size_t len;
	char sha256[SHA256_DIGEST_LENGTH * 2 + 1];

	pkg_open_root_fd(pkg);

	path = file->path;
	path++;

	prefix_rel = pkg->prefix;
	prefix_rel++;
	len = strlen(prefix_rel);

	/* Regular files and links */
	/* check sha256 */
	if (!force && file->sum[0] != '\0') {
		if (fstatat(pkg->rootfd, path, &st, AT_SYMLINK_NOFOLLOW) == -1) {
			pkg_emit_error("cannot stat %s%s%s: %s", pkg->rootpath,
			    pkg->rootpath[strlen(pkg->rootpath) - 1] == '/' ? "" : "/",
			    path, strerror(errno));
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
		if (strcmp(sha256, file->sum)) {
			pkg_emit_error("%s%s%s fails original SHA256 "
				"checksum, not removing", pkg->rootpath,
				pkg->rootpath[strlen(pkg->rootpath) - 1] == '/' ? "" : "/",
				path);
			return;
		}
	}

	if (unlinkat(pkg->rootfd, path, 0) == -1) {
		if (force < 2)
			pkg_emit_errno("unlinkat", path);
		return;
	}

	/* do not bother about directories not in prefix */
	if ((strncmp(prefix_rel, path, len) == 0) && path[len] == '/')
		pkg_add_dir_to_del(pkg, path, NULL);
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
	const char *prefix_rel;
	size_t len;

	pkg_open_root_fd(pkg);

	path = dir->path;
	/* remove the first / */
	path++;

	prefix_rel = pkg->prefix;
	prefix_rel++;
	len = strlen(prefix_rel);

	if ((strncmp(prefix_rel, path, len) == 0) && path[len] == '/') {
		pkg_add_dir_to_del(pkg, NULL, path);
	} else {
		if (pkg->dir_to_del_len + 1 > pkg->dir_to_del_cap) {
			pkg->dir_to_del_cap += 64;
			pkg->dir_to_del = reallocf(pkg->dir_to_del,
			    pkg->dir_to_del_cap * sizeof(char *));
		}
		pkg->dir_to_del[pkg->dir_to_del_len++] = strdup(path);
	}
}

int
pkg_delete_dirs(__unused struct pkgdb *db, struct pkg *pkg, struct pkg *new)
{
	struct pkg_dir	*dir = NULL;
	struct pkg_dir	*d;

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		d = NULL;
		if (new != NULL)
			HASH_FIND_STR(new->dirs, dir->path, d);
		if (d == NULL)
			pkg_delete_dir(pkg, dir);
	}

	pkg_effective_rmdir(db, pkg);

	return (EPKG_OK);
}
