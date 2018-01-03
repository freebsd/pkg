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
#include <utstring.h>

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkg.h"
#include "private/pkgdb.h"

KHASH_MAP_INIT_INT(hls, char *);

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
pkg_hidden_tempfile(char *buf, int buflen, const char *path)
{
	const char *fname;

	fname = strrchr(path, '/');
	if (fname != NULL)
		fname++;

	if (fname != NULL)
		snprintf(buf, buflen, "%.*s.%s", (int)(fname - path), path, fname);
	else
		snprintf(buf, buflen, ".%s", path);

	pkg_add_file_random_suffix(buf, buflen, 12);
}

static void
attempt_to_merge(int rootfd, struct pkg_config_file *rcf, struct pkg *local,
    bool merge)
{
	const struct pkg_file *lf = NULL;
	struct stat st;
	UT_string *newconf;
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
		if (fstatat(rootfd, RELATIVE_PATH(rcf->path), &st, 0) == 0) {
			rcf->status = MERGE_FAILED;
		}
		return;
	}

	if (!pkg_is_config_file(local, rcf->path, &lf, &lcf)) {
		pkg_debug(3, "No local package");
		rcf->status = MERGE_FAILED;
		return;
	}

	if (lcf->content == NULL) {
		pkg_debug(3, "Empty configuration content for local package");
		return;
	}

	pkg_debug(1, "Config file found %s", rcf->path);
	if (file_to_bufferat(rootfd, RELATIVE_PATH(rcf->path), &localconf, &sz) != EPKG_OK)
		return;

	pkg_debug(2, "size: %jd vs %jd", (intmax_t)sz, (intmax_t)strlen(lcf->content));

	if (sz == strlen(lcf->content)) {
		pkg_debug(2, "Ancient vanilla and deployed conf are the same size testing checksum");
		localsum = pkg_checksum_data(localconf, sz,
		    PKG_HASH_TYPE_SHA256_HEX);
		if (localsum && strcmp(localsum, lf->sum) == 0) {
			pkg_debug(2, "Checksum are the same %jd", (intmax_t)strlen(localconf));
			free(localconf);
			free(localsum);
			return;
		}
		free(localsum);
		pkg_debug(2, "Checksum are different %jd", (intmax_t)strlen(localconf));
	}
	rcf->status = MERGE_FAILED;
	if (!merge) {
		free(localconf);
		return;
	}

	pkg_debug(1, "Attempting to merge %s", rcf->path);
	utstring_new(newconf);
	if (merge_3way(lcf->content, localconf, rcf->content, newconf) != 0) {
		pkg_emit_error("Impossible to merge configuration file");
	} else {
		rcf->newcontent = xstrdup(utstring_body(newconf));
		rcf->status = MERGE_SUCCESS;
	}
	utstring_free(newconf);
	free(localconf);
}

static uid_t
get_uid_from_archive(struct archive_entry *ae)
{
	static char user_buffer[128];
	const char *user;
	static struct passwd pwent;
	struct passwd *result;

	user = archive_entry_uname(ae);
	if (pwent.pw_name != NULL && strcmp(user, pwent.pw_name) == 0)
		goto out;
	pwent.pw_name = NULL;
	if ((getpwnam_r(user, &pwent, user_buffer, sizeof(user_buffer),
	    &result)) < 0)
		return (0);
	if (result == NULL)
		return (0);
out:
	return (pwent.pw_uid);
}

static gid_t
get_gid_from_archive(struct archive_entry *ae)
{
	static char group_buffer[128];
	static struct group grent;
	struct group *result;
	const char *group;

	group = archive_entry_gname(ae);
	if (grent.gr_name != NULL && strcmp(group, grent.gr_name) == 0)
		goto out;
	grent.gr_name = NULL;
	if ((getgrnam_r(group, &grent, group_buffer, sizeof(group_buffer),
	    &result)) < 0)
		return (0);
	if (result == NULL)
		return (0);
out:
	return (grent.gr_gid);
}

static int
set_attrs(int fd, char *path, mode_t perm, uid_t uid, gid_t gid,
    const struct timespec *ats, const struct timespec *mts)
{

	struct timeval tv[2];
	struct stat st;
	int fdcwd;
#ifdef HAVE_UTIMENSAT
	struct timespec times[2];

	times[0] = *ats;
	times[1] = *mts;
	if (utimensat(fd, RELATIVE_PATH(path), times,
	    AT_SYMLINK_NOFOLLOW) == -1 && errno != EOPNOTSUPP){
		pkg_fatal_errno("Fail to set time on %s", path);
	}
	if (errno == EOPNOTSUPP) {
#endif

	tv[0].tv_sec = ats->tv_sec;
	tv[0].tv_usec = ats->tv_nsec / 1000;
	tv[1].tv_sec = mts->tv_sec;
	tv[1].tv_usec = mts->tv_nsec / 1000;

	fdcwd = open(".", O_DIRECTORY|O_CLOEXEC);
	fchdir(fd);

	if (lutimes(RELATIVE_PATH(path), tv) == -1) {

		if (errno != ENOSYS) {
			pkg_fatal_errno("Fail to set time on %s", path);
		}
		else {
			/* Fallback to utimes */
			if (utimes(RELATIVE_PATH(path), tv) == -1) {
				pkg_fatal_errno("Fail to set time(fallback) on "
				    "%s", path);
			}
		}
	}
	fchdir(fdcwd);
	close(fdcwd);
#ifdef HAVE_UTIMENSAT
	}
#endif

	if (getenv("INSTALL_AS_USER") == NULL) {
		if (fchownat(fd, RELATIVE_PATH(path), uid, gid,
				AT_SYMLINK_NOFOLLOW) == -1) {
			if (errno == ENOTSUP) {
				if (fchownat(fd, RELATIVE_PATH(path), uid, gid, 0) == -1) {
					pkg_fatal_errno("Fail to chown(fallback) %s", path);
				}
			}
			else {
				pkg_fatal_errno("Fail to chown %s", path);
			}
		}
	}

	/* zfs drops the setuid on fchownat */
	if (fchmodat(fd, RELATIVE_PATH(path), perm, AT_SYMLINK_NOFOLLOW) == -1) {
		if (errno == ENOTSUP) {
			/* 
			 * Executing fchmodat on a symbolic link results in
			 * ENOENT (file not found) on platforms that do not
			 * support AT_SYMLINK_NOFOLLOW. The file mode of
			 * symlinks cannot be modified via file descriptor
			 * reference on these systems. The lchmod function is
			 * also not an option because it is not a posix
			 * standard, nor is implemented everywhere. Since
			 * symlink permissions have never been evaluated and
			 * thus cosmetic, just skip them on these systems.
			 */
			if (fstatat(fd, RELATIVE_PATH(path), &st, AT_SYMLINK_NOFOLLOW) == -1) {
				pkg_fatal_errno("Fail to get file status %s", path);
			}
			if (!S_ISLNK(st.st_mode)) {
				if (fchmodat(fd, RELATIVE_PATH(path), perm, 0) == -1) {
					pkg_fatal_errno("Fail to chmod(fallback) %s", path);
				}
			}
		}
		else {
			pkg_fatal_errno("Fail to chmod %s", path);
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
# if defined(_DARWIN_C_SOURCE) || defined(__APPLE__)
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

static int
create_dir(struct pkg *pkg, struct pkg_dir *d)
{
	struct stat st;

	if (mkdirat(pkg->rootfd, RELATIVE_PATH(d->path), 0755) == -1)
		if (!mkdirat_p(pkg->rootfd, RELATIVE_PATH(d->path)))
			return (EPKG_FATAL);
	if (fstatat(pkg->rootfd, RELATIVE_PATH(d->path), &st, 0) == -1) {
		if (errno != ENOENT) {
			pkg_fatal_errno("Fail to stat directory %s", d->path);
		}
		if (fstatat(pkg->rootfd, RELATIVE_PATH(d->path), &st, AT_SYMLINK_NOFOLLOW) == 0) {
			unlinkat(pkg->rootfd, RELATIVE_PATH(d->path), 0);
		}
		if (mkdirat(pkg->rootfd, RELATIVE_PATH(d->path), 0755) == -1) {
			pkg_fatal_errno("Fail to create directory %s", d->path);
		}
	}

	if (st.st_uid == d->uid && st.st_gid == d->gid &&
	    (st.st_mode & ~S_IFMT) == (d->perm & ~S_IFMT)) {
		d->noattrs = true;
	}

	return (EPKG_OK);
}

/* In case of directories create the dir and extract the creds */
static int
do_extract_dir(struct pkg* pkg, struct archive *a __unused, struct archive_entry *ae,
    const char *path, struct pkg *local __unused)
{
	struct pkg_dir *d;
	const struct stat *aest;
	unsigned long clear;

	d = pkg_get_dir(pkg, path);
	if (d == NULL) {
		pkg_emit_error("Directory %s not specified in the manifest, skipping",
				path);
		return (EPKG_OK);
	}
	aest = archive_entry_stat(ae);
	d->perm = aest->st_mode;
	d->uid = get_uid_from_archive(ae);
	d->gid = get_gid_from_archive(ae);
	fill_timespec_buf(aest, d->time);
	archive_entry_fflags(ae, &d->fflags, &clear);

	if (create_dir(pkg, d) == EPKG_FATAL) {
		return (EPKG_FATAL);
	}

	metalog_add(PKG_METALOG_DIR, RELATIVE_PATH(path),
	    archive_entry_uname(ae), archive_entry_gname(ae),
	    aest->st_mode & ~S_IFDIR, NULL);

	return (EPKG_OK);
}

static int
create_symlinks(struct pkg *pkg, struct pkg_file *f, const char *target)
{
	bool tried_mkdir = false;

	pkg_hidden_tempfile(f->temppath, sizeof(f->temppath), f->path);
retry:
	if (symlinkat(target, pkg->rootfd, RELATIVE_PATH(f->temppath)) == -1) {
		if (!tried_mkdir) {
			if (!mkdirat_p(pkg->rootfd, RELATIVE_PATH(bsd_dirname(f->path))))
				return (EPKG_FATAL);
			tried_mkdir = true;
			goto retry;
		}

		pkg_fatal_errno("Fail to create symlink: %s", f->temppath);
	}

	if (set_attrs(pkg->rootfd, f->temppath, f->perm, f->uid, f->gid,
	    &f->time[0], &f->time[1]) != EPKG_OK) {
		return (EPKG_FATAL);
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

	f = pkg_get_file(pkg, path);
	if (f == NULL) {
		pkg_emit_error("Symlink %s not specified in the manifest", path);
		return (EPKG_FATAL);
	}

	aest = archive_entry_stat(ae);
	archive_entry_fflags(ae, &f->fflags, &clear);
	f->uid = get_uid_from_archive(ae);
	f->gid = get_gid_from_archive(ae);
	f->perm = aest->st_mode;
	fill_timespec_buf(aest, f->time);
	archive_entry_fflags(ae, &f->fflags, &clear);

	if (create_symlinks(pkg, f, archive_entry_symlink(ae)) == EPKG_FATAL)
		return (EPKG_FATAL);

	metalog_add(PKG_METALOG_LINK, RELATIVE_PATH(path),
	    archive_entry_uname(ae), archive_entry_gname(ae),
	    aest->st_mode & ~S_IFLNK, archive_entry_symlink(ae));

	return (EPKG_OK);
}

static int
create_hardlink(struct pkg *pkg, struct pkg_file *f, const char *path)
{
	bool tried_mkdir = false;
	struct pkg_file *fh;

	pkg_hidden_tempfile(f->temppath, sizeof(f->temppath), f->path);
	fh = pkg_get_file(pkg, path);
	if (fh == NULL) {
		pkg_emit_error("Can't find the file %s is supposed to be"
		    " hardlinked to %s", f->path, path);
		return (EPKG_FATAL);
	}


retry:
	if (linkat(pkg->rootfd, RELATIVE_PATH(fh->temppath),
	    pkg->rootfd, RELATIVE_PATH(f->temppath), 0) == -1) {
		if (!tried_mkdir) {
			if (!mkdirat_p(pkg->rootfd,
			    RELATIVE_PATH(bsd_dirname(f->path))))
				return (EPKG_FATAL);
			tried_mkdir = true;
			goto retry;
		}

		pkg_fatal_errno("Fail to create hardlink: %s", f->temppath);
	}

	return (EPKG_OK);
}

static int
do_extract_hardlink(struct pkg *pkg, struct archive *a __unused, struct archive_entry *ae,
    const char *path, struct pkg *local __unused)
{
	struct pkg_file *f;
	const struct stat *aest;
	const char *lp;

	f = pkg_get_file(pkg, path);
	if (f == NULL) {
		pkg_emit_error("Hardlink %s not specified in the manifest", path);
		return (EPKG_FATAL);
	}
	lp = archive_entry_hardlink(ae);
	aest = archive_entry_stat(ae);

	if (create_hardlink(pkg, f, lp) == EPKG_FATAL)
		return (EPKG_FATAL);

	metalog_add(PKG_METALOG_FILE, RELATIVE_PATH(path),
	    archive_entry_uname(ae), archive_entry_gname(ae),
	    aest->st_mode & ~S_IFREG, NULL);

	return (EPKG_OK);
}

static int
create_regfile(struct pkg *pkg, struct pkg_file *f, struct archive *a,
    struct archive_entry *ae, int fromfd, struct pkg *local)
{
	int fd = -1;
	bool tried_mkdir = false;
	size_t len;
	char buf[32768];

	pkg_hidden_tempfile(f->temppath, sizeof(f->temppath), f->path);

retry:
	/* Create the new temp file */
	fd = openat(pkg->rootfd, RELATIVE_PATH(f->temppath),
	    O_CREAT|O_WRONLY|O_EXCL, f->perm);
	if (fd == -1) {
		if (!tried_mkdir) {
			if (!mkdirat_p(pkg->rootfd,
			    RELATIVE_PATH(bsd_dirname(f->path)))) {
				return (EPKG_FATAL);
			}
			tried_mkdir = true;
			goto retry;
		}
		pkg_fatal_errno("Fail to create temporary file: %s",
		    f->temppath);
	}

	if (fromfd == -1) {
		/* check if this is a config file */
		kh_find(pkg_config_files, pkg->config_files, f->path,
		    f->config);
		if (f->config) {
			const char *cfdata;
			bool merge = pkg_object_bool(pkg_config_get("AUTOMERGE"));

			pkg_debug(1, "Populating config_file %s", f->path);
			len = archive_entry_size(ae);
			f->config->content = xmalloc(len + 1);
			archive_read_data(a, f->config->content, len);
			f->config->content[len] = '\0';
			cfdata = f->config->content;
			attempt_to_merge(pkg->rootfd, f->config, local, merge);
			if (f->config->status == MERGE_SUCCESS)
				cfdata = f->config->newcontent;
			dprintf(fd, "%s", cfdata);
			if (f->config->newcontent != NULL)
				free(f->config->newcontent);
		} else {
			if (ftruncate(fd, archive_entry_size(ae)) == -1) {
				pkg_errno("Fail to truncate file: %s", f->temppath);
			}
		}

		if (!f->config && archive_read_data_into_fd(a, fd) != ARCHIVE_OK) {
			pkg_emit_error("Fail to extract %s from package: %s",
			    f->path, archive_error_string(a));
			return (EPKG_FATAL);
		}
	} else {
		while ((len = read(fromfd, buf, sizeof(buf))) > 0)
			if (write(fd, buf, len) == -1) {
				pkg_errno("Fail to write file: %s", f->temppath);
			}
	}
	if (fd != -1) {
		close(fd);
	}

	if (set_attrs(pkg->rootfd, f->temppath, f->perm, f->uid, f->gid,
	    &f->time[0], &f->time[1]) != EPKG_OK)
			return (EPKG_FATAL);

	return (EPKG_OK);
}

static int
do_extract_regfile(struct pkg *pkg, struct archive *a, struct archive_entry *ae,
    const char *path, struct pkg *local)
{
	struct pkg_file *f;
	const struct stat *aest;
	unsigned long clear;

	f = pkg_get_file(pkg, path);
	if (f == NULL) {
		pkg_emit_error("File %s not specified in the manifest", path);
		return (EPKG_FATAL);
	}

	aest = archive_entry_stat(ae);
	archive_entry_fflags(ae, &f->fflags, &clear);
	f->perm = aest->st_mode;
	f->uid = get_uid_from_archive(ae);
	f->gid = get_gid_from_archive(ae);
	fill_timespec_buf(aest, f->time);
	archive_entry_fflags(ae, &f->fflags, &clear);

	if (create_regfile(pkg, f, a, ae, -1, local) == EPKG_FATAL)
		return (EPKG_FATAL);

	metalog_add(PKG_METALOG_FILE, RELATIVE_PATH(path),
	    archive_entry_uname(ae), archive_entry_gname(ae),
	    aest->st_mode & ~S_IFREG, NULL);

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
			pkg_fatal_errno("Fail to rename %s -> %s",
			    f->temppath, fto);
		}

#ifdef HAVE_CHFLAGSAT
		if (f->fflags != 0) {
			if (chflagsat(pkg->rootfd, RELATIVE_PATH(fto),
			    f->fflags, AT_SYMLINK_NOFOLLOW) == -1) {
				pkg_fatal_errno("Fail to chflags %s", fto);
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
	path = xstrdup(path);
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

	if (!is_valid_os_version(pkg) && (flags & PKG_ADD_FORCE) == 0) {
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

void
pkg_rollback_pkg(struct pkg *p)
{
	struct pkg_file *f = NULL;

	while (pkg_files(p, &f) == EPKG_OK) {
		if (*f->temppath != '\0') {
			unlinkat(p->rootfd, f->temppath, 0);
		}
	}
}

void
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
	UT_string		*message;
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
		pkg->digest = xstrdup(remote->digest);
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
		utstring_new(message);
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
			if (utstring_len(message) == 0) {
				pkg_utstring_printf(message, "Message from "
				    "%n-%v:\n\n", pkg, pkg);
			}
			utstring_printf(message, "%s\n", msgstr);
		}
	}
	if (pkg->message != NULL) {
		if (utstring_len(message) > 0) {
			pkg_emit_message(utstring_body(message));
		}
		utstring_free(message);
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

int
pkg_add_fromdir(struct pkg *pkg, const char *src)
{
	struct stat st;
	struct pkg_dir *d = NULL;
	struct pkg_file *f = NULL;
	char target[MAXPATHLEN];
	struct passwd *pw, pwent;
	struct group *gr, grent;
	int fd, fromfd;
	int retcode;
	kh_hls_t *hardlinks = NULL;;
	const char *path;
	char buffer[128];
	size_t link_len;
	bool install_as_user;

	install_as_user = (getenv("INSTALL_AS_USER") != NULL);

	fromfd = open(src, O_DIRECTORY);
	if (fromfd == -1) {
		pkg_fatal_errno("Unable to open source directory '%s'", src);
	}
	pkg_open_root_fd(pkg);

	while (pkg_dirs(pkg, &d) == EPKG_OK) {
		if (fstatat(fromfd, RELATIVE_PATH(d->path), &st, 0) == -1) {
			pkg_fatal_errno("%s%s", src, d->path);
		}
		if (d->perm == 0)
			d->perm = st.st_mode & ~S_IFMT;
		if (d->uname[0] != '\0') {
			if (getpwnam_r(d->uname, &pwent, buffer, sizeof(buffer),
			    &pw) < 0) {
				pkg_emit_error("Unknown user: '%s'", d->uname);
				return (EPKG_FATAL);
			}
			d->uid = pwent.pw_uid;
		} else {
			d->uid = install_as_user ? st.st_uid : 0;
		}
		if (d->gname[0] != '\0') {
			if (getgrnam_r(d->gname, &grent, buffer, sizeof(buffer),
			    &gr) < 0) {
				pkg_emit_error("Unknown group: '%s'", d->gname);
				return (EPKG_FATAL);
			}
			d->gid = grent.gr_gid;
		} else {
			d->gid = st.st_gid;
		}
#ifdef HAVE_STRUCT_STAT_ST_MTIM
		d->time[0] = st.st_atim;
		d->time[1] = st.st_mtim;
#else
#if defined(_DARWIN_C_SOURCE) || defined(__APPLE__)
		d->time[0] = st.st_atimespec;
		d->time[1] = st.st_mtimespec;
#else
		d->time[0].tv_sec = st.st_atime;
		d->time[0].tv_nsec = 0;
		d->time[1].tv_sec = st.st_mtime;
		d->time[1].tv_nsec = 0;
#endif
#endif

		if (create_dir(pkg, d) == EPKG_FATAL)
			return (EPKG_FATAL);
	}

	hardlinks = kh_init_hls();
	while (pkg_files(pkg, &f) == EPKG_OK) {
		if (fstatat(fromfd, RELATIVE_PATH(f->path), &st,
		    AT_SYMLINK_NOFOLLOW) == -1) {
			pkg_fatal_errno("%s%s", src, f->path);
		}
		if (f->uname[0] != '\0') {
			if (getpwnam_r(f->uname, &pwent, buffer, sizeof(buffer),
			    &pw) < 0) {
				pkg_emit_error("Unknown user: '%s'", f->uname);
				return (EPKG_FATAL);
			}
			f->uid = pwent.pw_uid;
		} else {
			f->uid = install_as_user ? st.st_uid : 0;
		}

		if (f->gname[0] != '\0') {
			if (getgrnam_r(f->gname, &grent, buffer, sizeof(buffer),
			    &gr) < 0) {
				pkg_emit_error("Unknown group: '%s'", f->gname);
				return (EPKG_FATAL);
			}
			f->gid = grent.gr_gid;
		} else {
			f->gid = st.st_gid;
		}

		if (f->perm == 0)
			f->perm = st.st_mode & ~S_IFMT;
		if (f->uid == 0 && install_as_user)
			f->uid = st.st_uid;
#ifdef HAVE_STRUCT_STAT_ST_MTIM
		f->time[0] = st.st_atim;
		f->time[1] = st.st_mtim;
#else
#if defined(_DARWIN_C_SOURCE) || defined(__APPLE__)
		f->time[0] = st.st_atimespec;
		f->time[1] = st.st_mtimespec;
#else
		f->time[0].tv_sec = st.st_atime;
		f->time[0].tv_nsec = 0;
		f->time[1].tv_sec = st.st_mtime;
		f->time[1].tv_nsec = 0;
#endif
#endif

		if (S_ISLNK(st.st_mode)) {
			if ((link_len = readlinkat(fromfd,
			    RELATIVE_PATH(f->path), target,
			    sizeof(target))) == -1) {
				pkg_fatal_errno("Impossible to read symlinks "
				    "'%s'", f->path);
			}
			target[link_len] = '\0';
			if (create_symlinks(pkg, f, target) == EPKG_FATAL) {
				return (EPKG_FATAL);
			}
		} else if (S_ISREG(st.st_mode)) {
			if ((fd = openat(fromfd, RELATIVE_PATH(f->path),
			    O_RDONLY)) == -1) {
				pkg_fatal_errno("Impossible to open source file"
				    " '%s'", RELATIVE_PATH(f->path));
			}
			kh_find(hls, hardlinks, st.st_ino, path);
			if (path != NULL) {
				if (create_hardlink(pkg, f, path) == EPKG_FATAL) {
					close(fd);
					return (EPKG_FATAL);
				}
			} else {
				if (create_regfile(pkg, f, NULL, NULL, fd, NULL) == EPKG_FATAL) {
					close(fd);
					return (EPKG_FATAL);
				}
				kh_safe_add(hls, hardlinks, f->path, st.st_ino);
			}
			close(fd);
		} else {
			pkg_emit_error("Invalid file type");
			return (EPKG_FATAL);
		}
	}
	kh_destroy_hls(hardlinks);

	retcode = pkg_extract_finalize(pkg);
	close(fromfd);
	return (retcode);
}

