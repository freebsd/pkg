/*-
 * Copyright (c) 2011-2016 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2016, Vsevolod Stakhov
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

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <pwd.h>
#include <grp.h>
#include <sys/time.h>
#include <time.h>

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkg.h"
#include "private/pkgdb.h"

#if defined(UF_NOUNLINK)
#define NOCHANGESFLAGS	(UF_IMMUTABLE | UF_APPEND | UF_NOUNLINK | SF_IMMUTABLE | SF_APPEND | SF_NOUNLINK)
#else
#define NOCHANGESFLAGS	(UF_IMMUTABLE | UF_APPEND | SF_IMMUTABLE | SF_APPEND)
#endif

static const unsigned char litchar[] =
"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static void
pkg_add_file_random_suffix(char *buf, int buflen, int suflen)
{
	int nchars = strlen(buf);
	char *pos;
	int r;

	if (nchars + suflen > buflen - 1) {
		suflen = buflen - nchars - 1;
		if (suflen <= 0)
			return;
	}

	buf[nchars++] = '.';
	pos = buf + nchars;

	while(suflen --) {
#ifndef HAVE_ARC4RANDOM
		r = rand() % (sizeof(litchar) - 1);
#else
		r = arc4random_uniform(sizeof(litchar) - 1);
#endif
		*pos++ = litchar[r];
	}

	*pos = '\0';
}

static void
attempt_to_merge(int rootfd, struct pkg_config_file *rcf, struct pkg *local,
    bool merge)
{
	const struct pkg_file *lf = NULL;
	struct sbuf *newconf;
	struct pkg_config_file *lcf = NULL;

	char *localconf = NULL;
	off_t sz;
	char *localsum;

	if (rcf == NULL) {
		pkg_debug(3, "No remote config file");
		return;
	}

	if (local == NULL) {
		pkg_debug(3, "No local package");
		return;
	}

	if (!pkg_is_config_file(local, rcf->path, &lf, &lcf)) {
		pkg_debug(3, "No local package");
		return;
	}

	if (lcf->content == NULL) {
		pkg_debug(3, "Empty configuration content for local package");
		return;
	}

	pkg_debug(1, "Config file found %s", rcf->path);
	if (file_to_bufferat(rootfd, RELATIVE_PATH(rcf->path), &localconf, &sz) != EPKG_OK)
		return;

	pkg_debug(2, "size: %d vs %d", sz, strlen(lcf->content));

	if (sz == strlen(lcf->content)) {
		pkg_debug(2, "Ancient vanilla and deployed conf are the same size testing checksum");
		localsum = pkg_checksum_data(localconf, sz,
		    PKG_HASH_TYPE_SHA256_HEX);
		if (localsum && strcmp(localsum, lf->sum) == 0) {
			pkg_debug(2, "Checksum are the same %d", strlen(localconf));
			free(localconf);
			free(localsum);
			return;
		}
		free(localsum);
		pkg_debug(2, "Checksum are different %d", strlen(localconf));
	}
	rcf->status = MERGE_FAILED;
	if (!merge) {
		free(localconf);
		return;
	}

	pkg_debug(1, "Attempting to merge %s", rcf->path);
	newconf = sbuf_new_auto();
	if (merge_3way(lcf->content, localconf, rcf->content, newconf) != 0) {
		pkg_emit_error("Impossible to merge configuration file");
	} else {
		sbuf_finish(newconf);
		rcf->newcontent = strdup(sbuf_data(newconf));
		rcf->status = MERGE_SUCCESS;
	}
	sbuf_delete(newconf);
	free(localconf);
}

static uid_t
get_uid_from_archive(struct archive_entry *ae)
{
	char buffer[128];
	struct passwd pwent, *result;

	if ((getpwnam_r(archive_entry_uname(ae), &pwent, buffer, sizeof(buffer),
	    &result)) < 0)
		return (0);
	if (result == NULL)
		return (0);
	return (pwent.pw_uid);
}

static gid_t
get_gid_from_archive(struct archive_entry *ae)
{
	char buffer[128];
	struct group grent, *result;

	if ((getgrnam_r(archive_entry_gname(ae), &grent, buffer, sizeof(buffer),
	    &result)) < 0)
		return (0);
	if (result == NULL)
		return (0);
	return (grent.gr_gid);
}

static int
set_attrs(int fd, char *path, mode_t perm, uid_t uid, gid_t gid,
    const struct timespec *ats, const struct timespec *mts)
{

#ifdef HAVE_UTIMENSAT
	struct timespec times[2];

	times[0] = *ats;
	times[1] = *mts;
	if (utimensat(fd, RELATIVE_PATH(path), times,
	    AT_SYMLINK_NOFOLLOW) == -1){
		pkg_emit_error("Fail to set time on %s: %s", path,
		    strerror(errno));
		return (EPKG_FATAL);
	}
#else
	struct timeval tv[2];
	char *saved_cwd[MAXPATHLEN];

	tv[0].tv_sec = ats->tv_sec;
	tv[0].tv_usec = ats->tv_nsec / 1000;
	tv[1].tv_sec = mts->tv_sec;
	tv[1].tv_usec = mts->tv_nsec / 1000;

	memset(saved_cwd, 0, sizeof (saved_cwd));

	if (getcwd(saved_cwd, sizeof(saved_cwd) - 1) == NULL) {
		pkg_emit_error("Fail to call getcwd: %s", strerror(errno));
		return (EPKG_FATAL);
	}

	fchdir(fd);

	if (lutimes(RELATIVE_PATH(path), tv) == -1) {

		if (errno != ENOSYS) {
			pkg_emit_error("Fail to set time on %s: %s", path,
					strerror(errno));
			return (EPKG_FATAL);
		}
		else {
			/* Fallback to utimes */
			if (utimes(RELATIVE_PATH(path), tv) == -1) {
				pkg_emit_error("Fail to set time(fallback) on %s: %s", path,
						strerror(errno));
				return (EPKG_FATAL);
			}
		}
	}
	chdir(saved_cwd);
#endif

	if (getenv("INSTALL_AS_USER") == NULL) {
		if (fchownat(fd, RELATIVE_PATH(path), uid, gid,
				AT_SYMLINK_NOFOLLOW) == -1) {
			if (errno == ENOTSUP) {
				if (fchownat(fd, RELATIVE_PATH(path), uid, gid, 0) == -1) {
					pkg_emit_error("Fail to chown(fallback) %s: %s", path, strerror(errno));
					return (EPKG_FATAL);
				}
			}
			else {
				pkg_emit_error("Fail to chown %s: %s", path, strerror(errno));
				return (EPKG_FATAL);
			}
		}
	}

	/* zfs drops the setuid on fchownat */
	if (fchmodat(fd, RELATIVE_PATH(path), perm, AT_SYMLINK_NOFOLLOW) == -1) {
		if (errno == ENOTSUP) {
			if (fchmodat(fd, RELATIVE_PATH(path), perm, 0) == -1) {
				pkg_emit_error("Fail to chmod(fallback) %s: %s", path, strerror(errno));
				return (EPKG_FATAL);
			}
		}
		else {
			pkg_emit_error("Fail to chmod %s: %s", path, strerror(errno));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

static void
fill_timespec_buf(const struct stat *aest, struct timespec tspec[2])
{
#ifdef HAVE_STRUCT_STAT_ST_MTIM
	tspec[0].tv_sec = aest->st_atim.tv_sec;
	tspec[0].tv_nsec = aest->st_atim.tv_nsec;
	tspec[1].tv_sec = aest->st_mtim.tv_sec;
	tspec[1].tv_nsec = aest->st_mtim.tv_nsec;
#else
# ifdef _DARWIN_C_SOURCE
	tspec[0].tv_sec = aest->st_atimespec.tv_sec;
	tspec[0].tv_nsec = aest->st_atimespec.tv_nsec;
	tspec[1].tv_sec = aest->st_mtimespec.tv_sec;
	tspec[1].tv_nsec = aest->st_mtimespec.tv_nsec;
# else
	/* Portable unix version */
	tspec[0].tv_sec = aest->st_atime;
	tspec[0].tv_nsec = 0;
	tspec[1].tv_sec = aest->st_mtime;
	tspec[1].tv_nsec = 0;
# endif
#endif
}

/* In case of directories create the dir and extract the creds */
static int
do_extract_dir(struct pkg* pkg, struct archive *a __unused, struct archive_entry *ae,
    const char *path, struct pkg *local __unused)
{
	struct pkg_dir *d;
	const struct stat *aest;
	struct stat st;
	unsigned long clear;

	d = pkg_get_dir(pkg, path);
	if (d == NULL) {
		pkg_emit_error("Directory %s not specified in the manifest");
		return (EPKG_FATAL);
	}
	aest = archive_entry_stat(ae);
	d->perm = aest->st_mode;
	d->uid = get_uid_from_archive(ae);
	d->gid = get_gid_from_archive(ae);
	fill_timespec_buf(aest, d->time);
	archive_entry_fflags(ae, &d->fflags, &clear);

	if (!mkdirat_p(pkg->rootfd, path))
		return (EPKG_FATAL);
	if (fstatat(pkg->rootfd, path, &st, 0) == -1)
		return (EPKG_FATAL);
	if (st.st_uid == d->uid && st.st_gid == d->gid &&
	    (st.st_mode & S_IFMT) == (d->perm & S_IFMT)) {
		d->noattrs = true;
	}

	return (EPKG_OK);
}

/* In case of a symlink create it directly with a random name */
static int
do_extract_symlink(struct pkg *pkg, struct archive *a __unused, struct archive_entry *ae,
    const char *path, struct pkg *local __unused)
{
	struct pkg_file *f;
	const struct stat *aest;
	unsigned long clear;
	struct timespec tspec[2];

	f = pkg_get_file(pkg, path);
	if (f == NULL) {
		pkg_emit_error("Symlink %s not specified in the manifest");
		return (EPKG_FATAL);
	}

	if (!mkdirat_p(pkg->rootfd, bsd_dirname(path)))
		return (EPKG_FATAL);

	aest = archive_entry_stat(ae);
	archive_entry_fflags(ae, &f->fflags, &clear);

	strlcpy(f->temppath, path, sizeof(f->temppath));
	pkg_add_file_random_suffix(f->temppath, sizeof(f->temppath), 12);
	if (symlinkat(archive_entry_symlink(ae), pkg->rootfd,
	    RELATIVE_PATH(f->temppath)) == -1) {
		pkg_emit_error("Fail to create symlink: %s: %s\n", f->temppath,
		    strerror(errno));
		return (EPKG_FATAL);
	}

	fill_timespec_buf(aest, tspec);

	if (set_attrs(pkg->rootfd, f->temppath, aest->st_mode,
	    get_uid_from_archive(ae), get_gid_from_archive(ae),
	    &tspec[0], &tspec[1]) != EPKG_OK) {
		return (EPKG_FATAL);
	}
	return (EPKG_OK);
}

static int
do_extract_hardlink(struct pkg *pkg, struct archive *a __unused, struct archive_entry *ae,
    const char *path, struct pkg *local __unused)
{
	struct pkg_file *f, *fh;
	const char *lp;

	f = pkg_get_file(pkg, path);
	if (f == NULL) {
		pkg_emit_error("Hardlink %s not specified in the manifest");
		return (EPKG_FATAL);
	}
	lp = archive_entry_hardlink(ae);
	fh = pkg_get_file(pkg, lp);
	if (fh == NULL) {
		pkg_emit_error("Can't find the file %s is supposed to be"
		    " hardlinked to in the archive: %s", path, lp);
		return (EPKG_FATAL);
	}

	if (!mkdirat_p(pkg->rootfd, bsd_dirname(path)))
		return (EPKG_FATAL);

	strlcpy(f->temppath, path, sizeof(f->temppath));
	pkg_add_file_random_suffix(f->temppath, sizeof(f->temppath), 12);
	if (linkat(pkg->rootfd, RELATIVE_PATH(fh->temppath),
	    pkg->rootfd, RELATIVE_PATH(f->temppath), 0) == -1) {
		pkg_emit_error("Fail to create hardlink: %s: %s\n", f->temppath,
		    strerror(errno));
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

static int
do_extract_regfile(struct pkg *pkg, struct archive *a, struct archive_entry *ae,
    const char *path, struct pkg *local)
{
	struct pkg_file *f;
	const struct stat *aest;
	unsigned long clear;
	int fd = -1;
	size_t len;
	struct timespec tspec[2];

	f = pkg_get_file(pkg, path);
	if (f == NULL) {
		pkg_emit_error("File %s not specified in the manifest");
		return (EPKG_FATAL);
	}

	if (!mkdirat_p(pkg->rootfd, bsd_dirname(path)))
		return (EPKG_FATAL);

	aest = archive_entry_stat(ae);
	archive_entry_fflags(ae, &f->fflags, &clear);

	strlcpy(f->temppath, path, sizeof(f->temppath));
	pkg_add_file_random_suffix(f->temppath, sizeof(f->temppath), 12);

	/* Create the new temp file */
	fd = openat(pkg->rootfd, RELATIVE_PATH(f->temppath),
	    O_CREAT|O_WRONLY|O_EXCL, aest->st_mode & ~S_IFMT);
	if (fd == -1) {
		pkg_emit_error("Fail to create temporary file: %s: %s",
		    f->temppath, strerror(errno));
		return (EPKG_FATAL);
	}

	/* check if this is a config file */
	kh_find(pkg_config_files, pkg->config_files, f->path,
	    f->config);
	if (f->config) {
		const char *cfdata;
		bool merge = pkg_object_bool(pkg_config_get("AUTOMERGE"));

		pkg_debug(1, "Populating config_file %s", f->path);
		len = archive_entry_size(ae);
		f->config->content = malloc(len + 1);
		archive_read_data(a, f->config->content, len);
		f->config->content[len] = '\0';
		cfdata = f->config->content;
		attempt_to_merge(pkg->rootfd, f->config, local, merge);
		if (f->config->status == MERGE_SUCCESS)
			cfdata = f->config->newcontent;
		dprintf(fd, "%s", cfdata);
		if (f->config->newcontent != NULL)
			free(f->config->newcontent);
	}

	if (!f->config && archive_read_data_into_fd(a, fd) != ARCHIVE_OK) {
		pkg_emit_error("Fail to extract %s from package: %s",
		    path, archive_error_string(a));
		return (EPKG_FATAL);
	}
	if (fd != -1) {
		close(fd);
	}

	fill_timespec_buf(aest, tspec);

	if (set_attrs(pkg->rootfd, f->temppath, aest->st_mode,
	    get_uid_from_archive(ae), get_gid_from_archive(ae),
	    &tspec[0], &tspec[1]) != EPKG_OK)
		return (EPKG_FATAL);
	return (EPKG_OK);
}

static int
do_extract(struct archive *a, struct archive_entry *ae,
    int nfiles, struct pkg *pkg, struct pkg *local)
{
	int	retcode = EPKG_OK;
	int	ret = 0, cur_file = 0;
	char	path[MAXPATHLEN];
	int (*extract_cb)(struct pkg *pkg, struct archive *a,
	    struct archive_entry *ae, const char *path, struct pkg *local);

#ifndef HAVE_ARC4RANDOM
	srand(time(NULL));
#endif

	if (nfiles == 0)
		return (EPKG_OK);

	pkg_emit_extract_begin(pkg);
	pkg_open_root_fd(pkg);
	pkg_emit_progress_start(NULL);

	do {
		ret = ARCHIVE_OK;
		pkg_absolutepath(archive_entry_pathname(ae), path, sizeof(path), true);
		switch (archive_entry_filetype(ae)) {
		case AE_IFDIR:
			extract_cb = do_extract_dir;
			break;
		case AE_IFLNK:
			extract_cb = do_extract_symlink;
			break;
		case 0: /* HARDLINKS */
			extract_cb = do_extract_hardlink;
			break;
		case AE_IFREG:
			extract_cb = do_extract_regfile;
			break;
		case AE_IFMT:
			pkg_emit_error("Archive contains an unsupported filetype (AE_IFMT): %s", path);
			retcode = EPKG_FATAL;
			goto cleanup;
			break;
		case AE_IFSOCK:
			pkg_emit_error("Archive contains an unsupported filetype (AE_IFSOCK): %s", path);
			retcode = EPKG_FATAL;
			goto cleanup;
			break;
		case AE_IFCHR:
			pkg_emit_error("Archive contains an unsupported filetype (AE_IFCHR): %s", path);
			retcode = EPKG_FATAL;
			goto cleanup;
			break;
		case AE_IFIFO:
			pkg_emit_error("Archive contains an unsupported filetype (AE_IFFIFO): %s", path);
			retcode = EPKG_FATAL;
			goto cleanup;
			break;
		case AE_IFBLK:
			pkg_emit_error("Archive contains an unsupported filetype (AE_IFFIFO): %s", path);
			retcode = EPKG_FATAL;
			goto cleanup;
			break;
		default:
			pkg_emit_error("Archive contains an unsupported filetype (%d): %s", archive_entry_filetype(ae), path);
			retcode = EPKG_FATAL;
			goto cleanup;
			break;
		}

		if (extract_cb(pkg, a, ae, path, local) != EPKG_OK) {
			retcode = EPKG_FATAL;
			goto cleanup;
		}
		if (archive_entry_filetype(ae) != AE_IFDIR) {
			pkg_emit_progress_tick(cur_file++, nfiles);
		}
	} while ((ret = archive_read_next_header(a, &ae)) == ARCHIVE_OK);

	pkg_emit_progress_tick(cur_file++, nfiles);

	if (ret != ARCHIVE_EOF) {
		pkg_emit_error("archive_read_next_header(): %s",
		    archive_error_string(a));
		retcode = EPKG_FATAL;
	}

cleanup:
	pkg_emit_progress_tick(nfiles, nfiles);
	pkg_emit_extract_finished(pkg);

	return (retcode);
}

static int
pkg_extract_finalize(struct pkg *pkg)
{
	struct stat st;
	struct pkg_file *f = NULL;
	struct pkg_dir *d = NULL;
	char path[MAXPATHLEN];
	const char *fto;

	while (pkg_files(pkg, &f) == EPKG_OK) {
		if (*f->temppath == '\0')
			continue;
		fto = f->path;
		if (f->config && f->config->status == MERGE_FAILED) {
			snprintf(path, sizeof(path), "%s.pkgnew", f->path);
			fto = path;
		}
		/*
		 * enforce an unlink of the file to workaround a bug that
		 * results in renameat returning 0 of the from file is hardlink
		 * on the to file, but the to file is not removed
		 */
		if (fstatat(pkg->rootfd, RELATIVE_PATH(fto), &st,
		    AT_SYMLINK_NOFOLLOW) != -1) {
#ifdef HAVE_CHFLAGSAT
			if (st.st_flags & NOCHANGESFLAGS) {
				chflagsat(pkg->rootfd, RELATIVE_PATH(fto), 0,
				    AT_SYMLINK_NOFOLLOW);
			}
#endif
			unlinkat(pkg->rootfd, RELATIVE_PATH(fto), 0);
		}
		if (renameat(pkg->rootfd, RELATIVE_PATH(f->temppath),
		    pkg->rootfd, RELATIVE_PATH(fto)) == -1) {
			pkg_emit_error("Fail to rename %s -> %s: %s",
			    f->temppath, fto, strerror(errno));
			return (EPKG_FATAL);
		}

#ifdef HAVE_CHFLAGSAT
		if (f->fflags != 0) {
			if (chflagsat(pkg->rootfd, RELATIVE_PATH(fto),
			    f->fflags, AT_SYMLINK_NOFOLLOW) == -1) {
				pkg_emit_error("Fail to chflags %s: %s",
				    fto, strerror(errno));
				return (EPKG_FATAL);
			}
		}
#endif
	}

	while (pkg_dirs(pkg, &d) == EPKG_OK) {
		if (d->noattrs)
			continue;
		if (set_attrs(pkg->rootfd, d->path, d->perm,
		    d->uid, d->gid, &d->time[0], &d->time[1]) != EPKG_OK)
			return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

static char *
pkg_globmatch(char *pattern, const char *name)
{
	glob_t g;
	int i;
	char *buf, *buf2;
	char *path = NULL;

	if (glob(pattern, 0, NULL, &g) == GLOB_NOMATCH) {
		globfree(&g);

		return (NULL);
	}

	for (i = 0; i < g.gl_pathc; i++) {
		/* the version starts here */
		buf = strrchr(g.gl_pathv[i], '-');
		if (buf == NULL)
			continue;
		buf2 = strchr(g.gl_pathv[i], '/');
		if (buf2 == NULL)
			buf2 = g.gl_pathv[i];
		else
			buf2++;
		/* ensure we have match the proper name */
		if (strncmp(buf2, name, buf - buf2) != 0)
			continue;
		if (path == NULL) {
			path = g.gl_pathv[i];
			continue;
		}
		if (pkg_version_cmp(path, g.gl_pathv[i]) == 1)
			path = g.gl_pathv[i];
	}
	path = strdup(path);
	globfree(&g);

	return (path);
}

static int
pkg_add_check_pkg_archive(struct pkgdb *db, struct pkg *pkg,
	const char *path, int flags,
	struct pkg_manifest_key *keys, const char *location)
{
	const char	*arch;
	int	ret, retcode;
	struct pkg_dep	*dep = NULL;
	char	bd[MAXPATHLEN], *basedir = NULL;
	char	dpath[MAXPATHLEN], *ppath;
	const char	*ext = NULL;
	struct pkg	*pkg_inst = NULL;
	bool	fromstdin;

	arch = pkg->abi != NULL ? pkg->abi : pkg->arch;

	if (!is_valid_abi(arch, true) && (flags & PKG_ADD_FORCE) == 0) {
		return (EPKG_FATAL);
	}

	/* XX check */
	ret = pkg_try_installed(db, pkg->name, &pkg_inst, PKG_LOAD_BASIC);
	if (ret == EPKG_OK) {
		if ((flags & PKG_ADD_FORCE) == 0) {
			pkg_emit_already_installed(pkg_inst);
			pkg_free(pkg_inst);
			pkg_inst = NULL;
			return (EPKG_INSTALLED);
		}
		if (pkg_inst->locked) {
			pkg_emit_locked(pkg_inst);
			pkg_free(pkg_inst);
			pkg_inst = NULL;
			return (EPKG_LOCKED);
		}
		pkg_emit_notice("package %s is already installed, forced "
		    "install", pkg->name);
		pkg_free(pkg_inst);
		pkg_inst = NULL;
	} else if (ret != EPKG_END) {
		return (ret);
	}

	/*
	 * Check for dependencies by searching the same directory as
	 * the package archive we're reading.  Of course, if we're
	 * reading from a file descriptor or a unix domain socket or
	 * whatever, there's no valid directory to search.
	 */
	fromstdin = (strcmp(path, "-") == 0);
	strlcpy(bd, path, sizeof(bd));
	if (!fromstdin) {
		basedir = bsd_dirname(bd);
		strlcpy(bd, basedir, sizeof(bd));
		if ((ext = strrchr(path, '.')) == NULL) {
			pkg_emit_error("%s has no extension", path);
			return (EPKG_FATAL);
		}
	}

	retcode = EPKG_FATAL;
	pkg_emit_add_deps_begin(pkg);

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		if (pkg_is_installed(db, dep->name) == EPKG_OK)
			continue;

		if (fromstdin) {
			pkg_emit_missing_dep(pkg, dep);
			if ((flags & PKG_ADD_FORCE_MISSING) == 0)
				goto cleanup;
			continue;
		}

		if (dep->version != NULL && dep->version[0] != '\0') {
			snprintf(dpath, sizeof(dpath), "%s/%s-%s%s", bd,
				dep->name, dep->version, ext);

			if ((flags & PKG_ADD_UPGRADE) == 0 &&
			    access(dpath, F_OK) == 0) {
				ret = pkg_add(db, dpath, PKG_ADD_AUTOMATIC,
				    keys, location);

				if (ret != EPKG_OK)
					goto cleanup;
			} else {
				pkg_emit_missing_dep(pkg, dep);
				if ((flags & PKG_ADD_FORCE_MISSING) == 0)
					goto cleanup;
			}
		} else {
			snprintf(dpath, sizeof(dpath), "%s/%s-*%s", bd,
			    dep->name, ext);
			ppath = pkg_globmatch(dpath, dep->name);
			if (ppath == NULL) {
				pkg_emit_missing_dep(pkg, dep);
				if ((flags & PKG_ADD_FORCE_MISSING) == 0)
					goto cleanup;
				continue;
			}
			if ((flags & PKG_ADD_UPGRADE) == 0 &&
			    access(ppath, F_OK) == 0) {
				ret = pkg_add(db, ppath, PKG_ADD_AUTOMATIC,
				    keys, location);

				free(ppath);
				if (ret != EPKG_OK)
					goto cleanup;
			} else {
				free(ppath);
				pkg_emit_missing_dep(pkg, dep);
				if ((flags & PKG_ADD_FORCE_MISSING) == 0)
					goto cleanup;
				continue;
			}
		}
	}

	retcode = EPKG_OK;
cleanup:
	pkg_emit_add_deps_finished(pkg);

	return (retcode);
}

static int
pkg_add_cleanup_old(struct pkgdb *db, struct pkg *old, struct pkg *new, int flags)
{
	struct pkg_file *f;
	int ret = EPKG_OK;
	bool handle_rc;

	handle_rc = pkg_object_bool(pkg_config_get("HANDLE_RC_SCRIPTS"));
	if (handle_rc)
		pkg_start_stop_rc_scripts(old, PKG_RC_STOP);

	/*
	 * Execute pre deinstall scripts
	 */
	if ((flags & PKG_ADD_NOSCRIPT) == 0) {
		if ((flags & PKG_ADD_USE_UPGRADE_SCRIPTS) == PKG_ADD_USE_UPGRADE_SCRIPTS)
			ret = pkg_script_run(old, PKG_SCRIPT_PRE_UPGRADE);
		else
			ret = pkg_script_run(old, PKG_SCRIPT_PRE_DEINSTALL);
		if (ret != EPKG_OK && pkg_object_bool(pkg_config_get("DEVELOPER_MODE")))
			return (ret);
		else
			ret = EPKG_OK;
	}

	/* Now remove files that no longer exist in the new package */
	if (new != NULL) {
		f = NULL;
		while (pkg_files(old, &f) == EPKG_OK) {
			if (!pkg_has_file(new, f->path)) {
				pkg_debug(2, "File %s is not in the new package", f->path);
				pkg_delete_file(old, f, flags & PKG_DELETE_FORCE ? 1 : 0);
			}
		}

		pkg_delete_dirs(db, old, new);
	}

	return (ret);
}

static void
pkg_rollback_pkg(struct pkg *p)
{
	struct pkg_file *f = NULL;

	while (pkg_files(p, &f) == EPKG_OK) {
		if (*f->temppath != '\0') {
			unlinkat(p->rootfd, f->temppath, 0);
		}
	}
}

static void
pkg_rollback_cb(void *data)
{
	pkg_rollback_pkg((struct pkg *)data);
}

static int
pkg_add_common(struct pkgdb *db, const char *path, unsigned flags,
    struct pkg_manifest_key *keys, const char *reloc, struct pkg *remote,
    struct pkg *local)
{
	struct archive		*a;
	struct archive_entry	*ae;
	struct pkg		*pkg = NULL;
	struct sbuf		*message;
	struct pkg_message	*msg;
	const char		*msgstr;
	bool			 extract = true;
	bool			 handle_rc = false;
	int			 retcode = EPKG_OK;
	int			 ret;
	int			 nfiles;

	assert(path != NULL);

	if (local != NULL)
		flags |= PKG_ADD_UPGRADE;

	/*
	 * Open the package archive file, read all the meta files and set the
	 * current archive_entry to the first non-meta file.
	 * If there is no non-meta files, EPKG_END is returned.
	 */
	ret = pkg_open2(&pkg, &a, &ae, path, keys, 0, -1);
	if (ret == EPKG_END)
		extract = false;
	else if (ret != EPKG_OK) {
		retcode = ret;
		goto cleanup;
	}
	if ((flags & PKG_ADD_SPLITTED_UPGRADE) != PKG_ADD_SPLITTED_UPGRADE)
		pkg_emit_new_action();
	if ((flags & PKG_ADD_UPGRADE) == 0)
		pkg_emit_install_begin(pkg);
	else {
		if (local != NULL)
			pkg_emit_upgrade_begin(pkg, local);
		else
			pkg_emit_install_begin(pkg);
	}

	if (pkg_is_valid(pkg) != EPKG_OK) {
		pkg_emit_error("the package is not valid");
		return (EPKG_FATAL);
	}

	if (flags & PKG_ADD_AUTOMATIC)
		pkg->automatic = true;

	/*
	 * Additional checks for non-remote package
	 */
	if (remote == NULL) {
		ret = pkg_add_check_pkg_archive(db, pkg, path, flags, keys,
		    reloc);
		if (ret != EPKG_OK) {
			/* Do not return error on installed package */
			retcode = (ret == EPKG_INSTALLED ? EPKG_OK : ret);
			goto cleanup;
		}
	}
	else {
		if (remote->repo != NULL) {
			/* Save reponame */
			pkg_kv_add(&pkg->annotations, "repository", remote->repo->name, "annotation");
			pkg_kv_add(&pkg->annotations, "repo_type", remote->repo->ops->type, "annotation");
		}

		free(pkg->digest);
		pkg->digest = strdup(remote->digest);
		/* only preserve flags is -A has not been passed */
		if ((flags & PKG_ADD_AUTOMATIC) == 0)
			pkg->automatic = remote->automatic;
	}

	if (reloc != NULL)
		pkg_kv_add(&pkg->annotations, "relocated", reloc, "annotation");

	/* register the package before installing it in case there are
	 * problems that could be caught here. */
	retcode = pkgdb_register_pkg(db, pkg,
			flags & PKG_ADD_FORCE);

	if (retcode != EPKG_OK)
		goto cleanup;

	/*
	 * Execute pre-install scripts
	 */
	if ((flags & (PKG_ADD_NOSCRIPT | PKG_ADD_USE_UPGRADE_SCRIPTS)) == 0)
		if ((retcode = pkg_script_run(pkg, PKG_SCRIPT_PRE_INSTALL)) != EPKG_OK)
			goto cleanup;


	/* add the user and group if necessary */

	nfiles = kh_count(pkg->filehash) + kh_count(pkg->dirhash);
	/*
	 * Extract the files on disk.
	 */
	if (extract) {
		pkg_register_cleanup_callback(pkg_rollback_cb, pkg);
		retcode = do_extract(a, ae, nfiles, pkg, local);
		pkg_unregister_cleanup_callback(pkg_rollback_cb, pkg);
		if (retcode != EPKG_OK) {
			/* If the add failed, clean up (silently) */

			pkg_rollback_pkg(pkg);
			pkg_delete_dirs(db, pkg, NULL);
			goto cleanup_reg;
		}
	}

	if (local != NULL) {
		pkg_debug(1, "Cleaning up old version");
		if (pkg_add_cleanup_old(db, local, pkg, flags) != EPKG_OK) {
			retcode = EPKG_FATAL;
			goto cleanup;
		}
	}


	/* Update configuration file content with db with newer versions */
	pkgdb_update_config_file_content(pkg, db->sqlite);

	retcode = pkg_extract_finalize(pkg);
cleanup_reg:
	pkgdb_register_finale(db, retcode);
	/*
	 * Execute post install scripts
	 */

	if (retcode != EPKG_OK)
		goto cleanup;
	if ((flags & PKG_ADD_NOSCRIPT) == 0) {
		if ((flags & PKG_ADD_USE_UPGRADE_SCRIPTS) == PKG_ADD_USE_UPGRADE_SCRIPTS)
			pkg_script_run(pkg, PKG_SCRIPT_POST_UPGRADE);
		else
			pkg_script_run(pkg, PKG_SCRIPT_POST_INSTALL);
	}

	/*
	 * start the different related services if the users do want that
	 * and that the service is running
	 */

	handle_rc = pkg_object_bool(pkg_config_get("HANDLE_RC_SCRIPTS"));
	if (handle_rc)
		pkg_start_stop_rc_scripts(pkg, PKG_RC_START);

	if ((flags & PKG_ADD_UPGRADE) == 0)
		pkg_emit_install_finished(pkg, local);
	else {
		if (local != NULL)
			pkg_emit_upgrade_finished(pkg, local);
		else
			pkg_emit_install_finished(pkg, local);
	}

	if (pkg->message != NULL)
		message = sbuf_new_auto();
	LL_FOREACH(pkg->message, msg) {
		msgstr = NULL;
		if (msg->type == PKG_MESSAGE_ALWAYS) {
			msgstr = msg->str;
		} else if (local != NULL &&
		     msg->type == PKG_MESSAGE_UPGRADE) {
			if (msg->maximum_version == NULL &&
			    msg->minimum_version == NULL) {
				msgstr = msg->str;
			} else if (msg->maximum_version == NULL) {
				if (pkg_version_cmp(local->version, msg->minimum_version) == 1) {
					msgstr = msg->str;
				}
			} else if (msg->minimum_version == NULL) {
				if (pkg_version_cmp(local->version, msg->maximum_version) == -1) {
					msgstr = msg->str;
				}
			} else if (pkg_version_cmp(local->version, msg->maximum_version) == -1 &&
				    pkg_version_cmp(local->version, msg->minimum_version) == 1) {
				msgstr = msg->str;
			}
		} else if (local == NULL &&
		    msg->type == PKG_MESSAGE_INSTALL) {
			msgstr = msg->str;
		}
		if (msgstr != NULL) {
			if (sbuf_len(message) == 0) {
				pkg_sbuf_printf(message, "Message from "
				    "%n-%v:\n", pkg, pkg);
			}
			sbuf_printf(message, "%s\n", msgstr);
		}
	}
	if (pkg->message != NULL) {
		if (sbuf_len(message) > 0) {
			sbuf_finish(message);
			pkg_emit_message(sbuf_data(message));
		}
		sbuf_delete(message);
	}

	cleanup:
	if (a != NULL) {
		archive_read_close(a);
		archive_read_free(a);
	}

	pkg_free(pkg);

	return (retcode);
}

int
pkg_add(struct pkgdb *db, const char *path, unsigned flags,
    struct pkg_manifest_key *keys, const char *location)
{
	return pkg_add_common(db, path, flags, keys, location, NULL, NULL);
}

int
pkg_add_from_remote(struct pkgdb *db, const char *path, unsigned flags,
    struct pkg_manifest_key *keys, const char *location, struct pkg *rp)
{
	return pkg_add_common(db, path, flags, keys, location, rp, NULL);
}

int
pkg_add_upgrade(struct pkgdb *db, const char *path, unsigned flags,
    struct pkg_manifest_key *keys, const char *location,
    struct pkg *rp, struct pkg *lp)
{
	if (pkgdb_ensure_loaded(db, lp,
	    PKG_LOAD_FILES|PKG_LOAD_SCRIPTS|PKG_LOAD_DIRS) != EPKG_OK)
		return (EPKG_FATAL);

	return pkg_add_common(db, path, flags, keys, location, rp, lp);
}
