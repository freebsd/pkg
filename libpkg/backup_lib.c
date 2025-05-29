/*-
 * Copyright (c) 2020 Baptiste Daroussin <bapt@FreeBSD.org>
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

static int
register_backup(struct pkgdb *db, int fd, const char *path)
{
	struct pkgdb_it *it;
	struct pkg *pkg = NULL;
	time_t t;
	char buf[BUFSIZ];
	char *sum;
	struct pkg_file *f;
	char *lpath;
	struct stat st;
	pkghash_entry *e;
	int retcode;

	sum = pkg_checksum_generate_fileat(fd, RELATIVE_PATH(path), PKG_HASH_TYPE_SHA256_HEX);

	it = pkgdb_query(db, "compat-libraries", MATCH_EXACT);
	if (it != NULL) {
		pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_FILES);
		pkgdb_it_free(it);
	}
	if (pkg == NULL) {
		if (pkg_new(&pkg, PKG_FILE) != EPKG_OK) {
			return (EPKG_FATAL);
		}
		pkg->name = xstrdup("compat-libraries");
		pkg->origin = xstrdup("compat/libraries");
		pkg->comment = xstrdup("Compatibility libraries saved during local packages upgrade\n");
		pkg->desc = xstrdup("Compatibility libraries saved during local packages upgrade\n");
		pkg->maintainer = xstrdup("root@localhost");
		pkg->www = xstrdup("N/A");
		pkg->prefix = xstrdup("/");
		pkg->abi = "*";
	}
	free(pkg->version);
	t = time(NULL);
	strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", localtime(&t));
	if ((e = pkghash_get(pkg->filehash, path)) != NULL) {
		DL_DELETE(pkg->files, (struct pkg_file *)e->value);
		pkg_file_free(e->value);
		pkghash_del(pkg->filehash, path);
	}
	xasprintf(&lpath, "%s/%s", ctx.backup_library_path, path);
	pkg_addfile(pkg, lpath, sum, false);
	free(lpath);
	pkg->version = xstrdup(buf);
	pkg_analyse_files(NULL, pkg, ctx.pkg_rootdir);
	pkg_open_root_fd(pkg);
	f = NULL;
	while (pkg_files(pkg, &f) == EPKG_OK) {
		if (fstatat(pkg->rootfd, RELATIVE_PATH(f->path), &st, AT_SYMLINK_NOFOLLOW) != -1)
			pkg->flatsize += st.st_size;
	}
	retcode = pkgdb_register_pkg(db, pkg, 0, "backuplib");
	if (retcode == EPKG_OK)
		pkgdb_register_finale(db, EPKG_OK, "backuplib");
	return (retcode);
}

void
backup_library(struct pkgdb *db, struct pkg *p, const char *path)
{
	const char *libname;
	char buf[BUFSIZ];
	char *outbuf;
	int from, to, backupdir;
	ssize_t nread, nwritten;

	if ((libname = strrchr(path, '/')) == NULL)
		return;

	pkg_open_root_fd(p);
	to = -1;

	/* skip the initial / */
	libname++;

	from = openat(p->rootfd, RELATIVE_PATH(path), O_RDONLY);
	if (from == -1) {
		pkg_debug(2, "unable to backup %s:%s", path, strerror(errno));
		return;
	}

	if (mkdirat(p->rootfd, RELATIVE_PATH(ctx.backup_library_path), 0755) == -1) {
		if (!mkdirat_p(p->rootfd, RELATIVE_PATH(ctx.backup_library_path))) {
			pkg_emit_errno("Impossible to create the library backup "
			    "directory", ctx.backup_library_path);
			close(from);
			return;
		}
	}
	backupdir = openat(p->rootfd, RELATIVE_PATH(ctx.backup_library_path),
	    O_DIRECTORY);
	if (backupdir == -1) {
		pkg_emit_error("Impossible to open the library backup "
		    "directory %s", ctx.backup_library_path);
		goto out;
	}

	/*
	 * always overwrite the existing backup library, it might be older than
	 * this one
	 */
	/* first always unlink to ensure we are not truncating a used library */
	unlinkat(backupdir, libname, 0);
	to = openat(backupdir, libname, O_EXCL|O_CREAT|O_WRONLY, 0644);
	if (to == -1) {
		pkg_emit_errno("Impossible to create the backup library", libname);
		goto out;
	}

	memset(buf, '\0', sizeof(buf));
	while ((nread = read(from, buf, sizeof(buf))) > 0) {
		outbuf = buf;
		do {
			nwritten = write(to, outbuf, nread);
			if (nwritten >= 0) {
				nread -= nwritten;
				outbuf += nwritten;
			} else if (errno != EINTR) {
				goto out;
			}
		} while (nread > 0);
	}

	if (nread == 0) {
		if (close(to) < 0) {
			to = -1;
			goto out;
		}
		close(from);
		register_backup(db, backupdir, libname);
		close(backupdir);
		return;
	}


out:
	pkg_emit_errno("Fail to backup the library", libname);
	if (backupdir >= 0)
		close(backupdir);
	if (from >= 0)
		close(from);
	if (to >= 0)
		close(to);
}
