/*-
 * Copyright (c) 2020-2026 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <errno.h>
#include <fcntl.h>
#include <time.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"

static const char *
backup_library_relative_path(void)
{
	const char *path = ctx.backup_library_path;

	if (ctx.pkg_rootdir != NULL) {
		size_t rootlen = strlen(ctx.pkg_rootdir);
		if (strncmp(path, ctx.pkg_rootdir, rootlen) == 0 &&
		    (path[rootlen] == '/' || path[rootlen] == '\0'))
			path += rootlen;
	}
	return (path);
}

static int
register_backup(struct pkgdb *db, struct pkg *orig, int fd, const char *libname)
{
	struct pkgdb_it *it;
	struct pkg *pkg = NULL;
	time_t t;
	char buf[BUFSIZ];
	char *lpath, *name, *sum;
	struct pkg_file *f;
	struct stat st;
	int retcode;

	sum = pkg_checksum_generate_fileat(fd, RELATIVE_PATH(libname),
	    PKG_HASH_TYPE_BLAKE2_BASE32);

	(void)xasprintf(&name, "%s-backup-%s", orig->name, libname);

	it = pkgdb_query(db, name, MATCH_EXACT);
	if (it != NULL) {
		pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_FILES);
		pkgdb_it_free(it);
	}
	if (pkg == NULL) {
		char *origin;

		if (pkg_new(&pkg, PKG_FILE) != EPKG_OK) {
			free(sum);
			free(name);
			return (EPKG_FATAL);
		}
		pkg->name = name;
		(void)xasprintf(&origin, "%s-backup-%s", orig->origin, libname);
		pkg->origin = origin;
		pkg->comment = xstrdup(
		    "Compatibility libraries saved during package upgrade");
		pkg->desc = xstrdup(
		    "Compatibility libraries saved during package upgrade\n");
		pkg->maintainer = xstrdup("root@localhost");
		pkg->www = xstrdup("N/A");
		pkg->prefix = xstrdup("/");
		pkg->abi = orig->abi ? xstrdup(orig->abi) : NULL;

		if (pkgdb_ensure_loaded(db, orig, PKG_LOAD_SHLIBS_PROVIDED_IGNORE |
		    PKG_LOAD_SHLIBS_REQUIRED_IGNORE) != EPKG_OK) {
			free(sum);
			pkg_free(pkg);
			return (EPKG_FATAL);
		}
		vec_foreach(orig->shlibs_provided_ignore, i) {
			pkg_addshlib_provided_ignore(pkg, orig->shlibs_provided_ignore.d[i]);
		}
		vec_foreach(orig->shlibs_required_ignore, i) {
			pkg_addshlib_required_ignore(pkg, orig->shlibs_required_ignore.d[i]);
		}
	} else {
		free(name);
		name = NULL;
	}
	/* Remove any existing file entry for this library.  Match
	 * by the trailing library name rather than the full key. */
	vec_foreach(pkg->files, _fi) {
		const char *fname = strrchr(pkg->files.d[_fi].path, '/');
		if (fname != NULL && strcmp(fname + 1, libname) == 0) {
			pkg_file_free_content(&pkg->files.d[_fi]);
			vec_remove(&pkg->files, _fi);
			break;
		}
	}
	xasprintf(&lpath, "%s/%s", backup_library_relative_path(), libname);
	pkg_addfile(pkg, lpath, sum, false);
	free(lpath);
	free(sum);

	t = time(NULL);
	strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", localtime(&t));
	free(pkg->version);
	pkg->version = xstrdup(buf);

	pkg_analyse_files(NULL, pkg, ctx.pkg_rootdir);
	pkg_open_root_fd(pkg);
	f = NULL;
	while (pkg_files(pkg, &f) == EPKG_OK) {
		if (fstatat(pkg->rootfd, RELATIVE_PATH(f->path), &st,
		    AT_SYMLINK_NOFOLLOW) != -1)
			pkg->flatsize += st.st_size;
	}
	retcode = pkgdb_register_pkg(db, pkg, 1, "backuplib");
	if (retcode == EPKG_OK)
		retcode = pkgdb_register_finale(db, EPKG_OK, "backuplib");
	return (retcode);
}

static void
backup_library(struct pkgdb *db, struct pkg *p, const char *path)
{
	const char *libname;
	const char *bkpath;
	const char *rootdir;
	int from, to, backupdir;

	if ((libname = strrchr(path, '/')) == NULL)
		return;
	/* skip the initial / */
	libname++;

	pkg_open_root_fd(p);
	to = -1;
	bkpath = backup_library_relative_path();

	from = openat(p->rootfd, RELATIVE_PATH(path), O_RDONLY);
	if (from == -1) {
		pkg_emit_notice("Unable to backup library %s: %s",
		    path, strerror(errno));
		return;
	}

	if (mkdirat(p->rootfd, RELATIVE_PATH(bkpath), 0755) == -1) {
		if (!mkdirat_p(p->rootfd, RELATIVE_PATH(bkpath))) {
			pkg_emit_errno("Unable to create the library backup "
			    "directory", bkpath);
			close(from);
			return;
		}
	}
	backupdir = openat(p->rootfd, RELATIVE_PATH(bkpath),
	    O_DIRECTORY);
	if (backupdir == -1) {
		pkg_emit_error("Unable to open the library backup "
		    "directory %s", bkpath);
		goto out;
	}

	/*
	 * Write to a temporary file and atomically rename into place.
	 * This avoids corrupting or removing a backup library that may
	 * still be in use if the process is interrupted.
	 */
	char tmpname[MAXPATHLEN];
	char tmppath[MAXPATHLEN];
	rootdir = ctx.pkg_rootdir != NULL ? ctx.pkg_rootdir : "";
	snprintf(tmpname, sizeof(tmpname), ".%s.XXXXXX", libname);
	snprintf(tmppath, sizeof(tmppath), "%s%s/%s", rootdir, bkpath, tmpname);
	to = mkstemp(tmppath);
	/* Update tmpname with the resolved XXXXXX suffix */
	strlcpy(tmpname, strrchr(tmppath, '/') + 1, sizeof(tmpname));
	if (to == -1) {
		pkg_emit_errno("Unable to create temporary backup library",
		    libname);
		goto out;
	}
	if (fchmod(to, 0644) == -1) {
		pkg_emit_errno("Unable to set permissions on backup library",
		    libname);
		goto out;
	}

	if (!pkg_copy_file(from, to))
		goto out;
	if (close(to) < 0) {
		to = -1;
		goto out;
	}
	to = -1;
	close(from);
	from = -1;

	if (renameat(backupdir, tmpname, backupdir, libname) == -1) {
		pkg_emit_errno("Unable to rename backup library", libname);
		unlinkat(backupdir, tmpname, 0);
		goto out;
	}

	register_backup(db, p, backupdir, libname);
	close(backupdir);
	return;

out:
	pkg_emit_errno("Failed to backup the library", libname);
	if (backupdir >= 0)
		close(backupdir);
	if (from >= 0)
		close(from);
	if (to >= 0) {
		unlinkat(backupdir, tmpname, 0);
		close(to);
	}
}

/*
 * We're about to remove an installed file as part of an upgrade.  See if it's a
 * library and whether the user asked us to back up libraries, and if so, back
 * it up.
 */
void
pkg_maybe_backup_library(struct pkgdb *db, struct pkg *pkg, const char *path)
{
	const char *libname;

	if (!ctx.backup_libraries)
		return;

	libname = strrchr(path, '/');
	if (libname != NULL &&
	    charv_search(&pkg->shlibs_provided, libname + 1) != NULL)
		backup_library(db, pkg, path);
}
