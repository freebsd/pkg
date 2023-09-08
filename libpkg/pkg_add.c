/*-
 * Copyright (c) 2011-2022 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2016, Vsevolod Stakhov
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
#include <xstring.h>
#include <tllist.h>

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

static void
attempt_to_merge(int rootfd, struct pkg_config_file *rcf, struct pkg *local,
    bool merge)
{
	const struct pkg_file *lf = NULL;
	struct stat st;
	xstring *newconf;
	struct pkg_config_file *lcf = NULL;

	char *localconf = NULL;
	off_t sz;
	char *localsum;

	if (local == NULL) {
		pkg_debug(3, "No local package");
		if (fstatat(rootfd, RELATIVE_PATH(rcf->path), &st, 0) == 0) {
			rcf->status = MERGE_NOT_LOCAL;
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
	newconf = xstring_new();
	if (merge_3way(lcf->content, localconf, rcf->content, newconf) != 0) {
		xstring_free(newconf);
		pkg_emit_error("Impossible to merge configuration file");
	} else {
		char *conf = xstring_get(newconf);
		rcf->newcontent = conf;
		rcf->status = MERGE_SUCCESS;
	}
	free(localconf);
}

static uid_t
get_uid_from_archive(struct archive_entry *ae)
{
	static char user_buffer[1024];
	const char *user;
	static struct passwd pwent;
	struct passwd *result;
	int err;

	user = archive_entry_uname(ae);
	if (pwent.pw_name != NULL && strcmp(user, pwent.pw_name) == 0)
		goto out;
	pwent.pw_name = NULL;
	err = getpwnam_r(user, &pwent, user_buffer, sizeof(user_buffer),
	    &result);
	if (err != 0) {
		pkg_emit_errno("getpwnam_r", user );
		return (0);
	}
	if (result == NULL)
		return (0);
out:
	return (pwent.pw_uid);
}

static gid_t
get_gid_from_archive(struct archive_entry *ae)
{
	static char group_buffer[1024];
	static struct group grent;
	struct group *result;
	const char *group;
	int err;

	group = archive_entry_gname(ae);
	if (grent.gr_name != NULL && strcmp(group, grent.gr_name) == 0)
		goto out;
	grent.gr_name = NULL;
	err = getgrnam_r(group, &grent, group_buffer, sizeof(group_buffer),
	    &result);
	if (err != 0) {
		pkg_emit_errno("getgrnam_r", group );
		return (0);
	}
	if (result == NULL)
		return (0);
out:
	return (grent.gr_gid);
}

static int
set_chflags(int fd, const char *path, u_long fflags)
{
#ifdef HAVE_CHFLAGSAT
	if (getenv("INSTALL_AS_USER"))
		return (EPKG_OK);
	if (fflags == 0)
		return (EPKG_OK);
	if (chflagsat(fd, RELATIVE_PATH(path), fflags, AT_SYMLINK_NOFOLLOW) == -1) {
		pkg_fatal_errno("Fail to chflags %s", path);
	}
#endif
	return (EPKG_OK);
}
int
set_attrsat(int fd, const char *path, mode_t perm, uid_t uid, gid_t gid,
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

	if ((fdcwd = open(".", O_DIRECTORY|O_CLOEXEC)) == -1) {
		pkg_fatal_errno("Failed to open .%s", "");
	}
	fchdir(fd);

	if (lutimes(RELATIVE_PATH(path), tv) == -1) {

		if (errno != ENOSYS) {
			close(fdcwd);
			pkg_fatal_errno("Fail to set time on %s", path);
		}
		else {
			/* Fallback to utimes */
			if (utimes(RELATIVE_PATH(path), tv) == -1) {
				close(fdcwd);
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

static void
reopen_tempdir(int rootfd, struct tempdir *t)
{
	if (t->fd != -1)
		return;
	t->fd = openat(rootfd, RELATIVE_PATH(t->temp), O_DIRECTORY|O_CLOEXEC);
}

static struct tempdir *
get_tempdir(int rootfd, const char *path, tempdirs_t *tempdirs)
{
	struct tempdir *tmpdir = NULL;

	tll_foreach(*tempdirs, t) {
		if (strncmp(t->item->name, path, t->item->len) == 0 && path[t->item->len] == '/') {
			reopen_tempdir(rootfd, t->item);
			return (t->item);
		}
	}

	tmpdir = open_tempdir(rootfd, path);
	if (tmpdir != NULL)
		tll_push_back(*tempdirs, tmpdir);

	return (tmpdir);
}

static void
close_tempdir(struct tempdir *t)
{
	if (t == NULL)
		return;
	if (t->fd != -1)
		close(t->fd);
	t->fd = -1;
}

static int
create_dir(struct pkg *pkg, struct pkg_dir *d, tempdirs_t *tempdirs)
{
	struct stat st;
	struct tempdir *tmpdir = NULL;
	int fd;
	const char *path;

	tmpdir = get_tempdir(pkg->rootfd, d->path, tempdirs);
	if (tmpdir == NULL) {
		fd = pkg->rootfd;
		path = d->path;
	} else {
		fd = tmpdir->fd;
		path = d->path + tmpdir->len;
	}

	if (mkdirat(fd, RELATIVE_PATH(path), 0755) == -1)
		if (!mkdirat_p(fd, RELATIVE_PATH(path))) {
			close_tempdir(tmpdir);
			return (EPKG_FATAL);
		}
	if (fstatat(fd, RELATIVE_PATH(path), &st, 0) == -1) {
		if (errno != ENOENT) {
			close_tempdir(tmpdir);
			pkg_fatal_errno("Fail to stat directory %s", path);
		}
		if (fstatat(fd, RELATIVE_PATH(path), &st, AT_SYMLINK_NOFOLLOW) == 0) {
			unlinkat(fd, RELATIVE_PATH(path), 0);
		}
		if (mkdirat(fd, RELATIVE_PATH(path), 0755) == -1) {
			if (tmpdir != NULL) {
				close_tempdir(tmpdir);
				pkg_fatal_errno("Fail to create directory '%s/%s'", tmpdir->temp, path);
			}
			pkg_fatal_errno("Fail to create directory %s", path);
		}
	}

	if (st.st_uid == d->uid && st.st_gid == d->gid &&
	    (st.st_mode & ~S_IFMT) == (d->perm & ~S_IFMT)) {
		d->noattrs = true;
	}

	close_tempdir(tmpdir);
	return (EPKG_OK);
}

/* In case of directories create the dir and extract the creds */
static int
do_extract_dir(struct pkg* pkg, struct archive *a __unused, struct archive_entry *ae,
    const char *path, struct pkg *local __unused, tempdirs_t *tempdirs)
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

	if (create_dir(pkg, d, tempdirs) == EPKG_FATAL) {
		return (EPKG_FATAL);
	}

	metalog_add(PKG_METALOG_DIR, RELATIVE_PATH(path),
	    archive_entry_uname(ae), archive_entry_gname(ae),
	    aest->st_mode & ~S_IFDIR, d->fflags, NULL);

	return (EPKG_OK);
}


static bool
try_mkdir(int fd, const char *path)
{
	char *p = get_dirname(xstrdup(path));

	if (!mkdirat_p(fd, RELATIVE_PATH(p))) {
		free(p);
		return (false);
	}
	free(p);
	return (true);
}

static int
create_symlinks(struct pkg *pkg, struct pkg_file *f, const char *target, tempdirs_t *tempdirs)
{
	struct tempdir *tmpdir = NULL;
	int fd;
	const char *path;
	bool tried_mkdir = false;

	tmpdir = get_tempdir(pkg->rootfd, f->path, tempdirs);
	if (tmpdir == NULL && errno == 0)
		hidden_tempfile(f->temppath, sizeof(f->temppath), f->path);
	if (tmpdir == NULL) {
		fd = pkg->rootfd;
		path = f->temppath;
	} else {
		fd = tmpdir->fd;
		path = f->path + tmpdir->len;
	}
retry:
	if (symlinkat(target, fd, RELATIVE_PATH(path)) == -1) {
		if (!tried_mkdir) {
			if (!try_mkdir(fd, path)) {
				close_tempdir(tmpdir);
				return (EPKG_FATAL);
			}
			tried_mkdir = true;
			goto retry;
		}

		pkg_fatal_errno("Fail to create symlink: %s", path);
	}

	if (set_attrsat(fd, path, f->perm, f->uid, f->gid,
	    &f->time[0], &f->time[1]) != EPKG_OK) {
		close_tempdir(tmpdir);
		return (EPKG_FATAL);
	}
	if (tmpdir != NULL)
		set_chflags(fd, path, f->fflags);
	close_tempdir(tmpdir);

	return (EPKG_OK);
}

/* In case of a symlink create it directly with a random name */
static int
do_extract_symlink(struct pkg *pkg, struct archive *a __unused, struct archive_entry *ae,
    const char *path, struct pkg *local __unused, tempdirs_t *tempdirs)
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

	if (create_symlinks(pkg, f, archive_entry_symlink(ae), tempdirs) == EPKG_FATAL)
		return (EPKG_FATAL);

	metalog_add(PKG_METALOG_LINK, RELATIVE_PATH(path),
	    archive_entry_uname(ae), archive_entry_gname(ae),
	    aest->st_mode & ~S_IFLNK, f->fflags, archive_entry_symlink(ae));

	return (EPKG_OK);
}

static int
create_hardlink(struct pkg *pkg, struct pkg_file *f, const char *path, tempdirs_t *tempdirs)
{
	bool tried_mkdir = false;
	struct pkg_file *fh;
	int fd, fdh;
	const char *pathfrom, *pathto;
	struct tempdir *tmpdir = NULL;
	struct tempdir *tmphdir = NULL;

	tmpdir = get_tempdir(pkg->rootfd, f->path, tempdirs);
	if (tmpdir == NULL && errno == 0)
		hidden_tempfile(f->temppath, sizeof(f->temppath), f->path);
	if (tmpdir != NULL) {
		fd = tmpdir->fd;
	} else {
		fd = pkg->rootfd;
	}
	fh = pkg_get_file(pkg, path);
	if (fh == NULL) {
		close_tempdir(tmpdir);
		pkg_emit_error("Can't find the file %s is supposed to be"
		    " hardlinked to %s", f->path, path);
		return (EPKG_FATAL);
	}
	if (fh->temppath[0] == '\0') {
		tll_foreach(*tempdirs, t) {
			if (strncmp(t->item->name, fh->path, t->item->len) == 0 &&
			    fh->path[t->item->len] == '/' ) {
				tmphdir = t->item;
				reopen_tempdir(pkg->rootfd, tmphdir);
				break;
			}
		}
	}
	if (tmpdir == NULL) {
		pathto = f->temppath;
		fd = pkg->rootfd;
	} else {
		pathto = f->path + tmpdir->len;
		fd = tmpdir->fd;
	}

	if (tmphdir == NULL) {
		pathfrom = fh->temppath;
		fdh = pkg->rootfd;
	} else {
		pathfrom = fh->path + tmphdir->len;
		fdh = tmphdir->fd;
	}

retry:
	if (linkat(fdh, RELATIVE_PATH(pathfrom),
	    fd, RELATIVE_PATH(pathto), 0) == -1) {
		if (!tried_mkdir) {
			if (!try_mkdir(fd, pathto)) {
				close_tempdir(tmpdir);
				close_tempdir(tmphdir);
				return (EPKG_FATAL);
			}
			tried_mkdir = true;
			goto retry;
		}

		close_tempdir(tmpdir);
		close_tempdir(tmphdir);
		pkg_fatal_errno("Fail to create hardlink: %s <-> %s", pathfrom, pathto);
	}
	close_tempdir(tmpdir);
	close_tempdir(tmphdir);

	return (EPKG_OK);
}

static int
do_extract_hardlink(struct pkg *pkg, struct archive *a __unused, struct archive_entry *ae,
    const char *path, struct pkg *local __unused, tempdirs_t *tempdirs)
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

	if (create_hardlink(pkg, f, lp, tempdirs) == EPKG_FATAL)
		return (EPKG_FATAL);

	metalog_add(PKG_METALOG_FILE, RELATIVE_PATH(path),
	    archive_entry_uname(ae), archive_entry_gname(ae),
	    aest->st_mode & ~S_IFREG, 0, NULL);

	return (EPKG_OK);
}

static int
open_tempfile(int rootfd, const char *path, int perm)
{
	int fd;
	bool tried_mkdir = false;

retry:
	fd = openat(rootfd, RELATIVE_PATH(path), O_CREAT|O_WRONLY|O_EXCL, perm);
	if (fd == -1) {
		if (!tried_mkdir) {
			if (!try_mkdir(rootfd, path))
				return (-2);
			tried_mkdir = true;
			goto retry;
		}
		return (-1);
	}
	return (fd);
}

static int
create_regfile(struct pkg *pkg, struct pkg_file *f, struct archive *a,
    struct archive_entry *ae, int fromfd, struct pkg *local, tempdirs_t *tempdirs)
{
	int fd = -1;
	size_t len;
	char buf[32768];
	char *path;
	struct tempdir *tmpdir = NULL;

	tmpdir = get_tempdir(pkg->rootfd, f->path, tempdirs);
	if (tmpdir == NULL && errno == 0)
		hidden_tempfile(f->temppath, sizeof(f->temppath), f->path);

	if (tmpdir != NULL) {
		fd = open_tempfile(tmpdir->fd, f->path + tmpdir->len, f->perm);
	} else {
		fd = open_tempfile(pkg->rootfd, f->temppath,  f->perm);
	}
	if (fd == -2) {
		close_tempdir(tmpdir);
		return (EPKG_FATAL);
	}

	if (fd == -1) {
		if (tmpdir != NULL) {
			close_tempdir(tmpdir);
			pkg_fatal_errno("Fail to create temporary file '%s/%s' for %s", tmpdir->name, f->path + tmpdir->len, f->path);
		}
		pkg_fatal_errno("Fail to create temporary file for %s", f->path);
	}

	if (fromfd == -1) {
		/* check if this is a config file */
		f->config = pkghash_get_value(pkg->config_files_hash, f->path);
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
				close_tempdir(tmpdir);
				pkg_errno("Fail to truncate file: %s", f->temppath);
			}
		}

		if (!f->config && archive_read_data_into_fd(a, fd) != ARCHIVE_OK) {
			close_tempdir(tmpdir);
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
	if (fd != -1)
		close(fd);
	if (tmpdir == NULL) {
		fd = pkg->rootfd;
		path = f->temppath;
	} else {
		fd = tmpdir->fd;
		path = f->path + tmpdir->len;
	}

	if (set_attrsat(fd, path, f->perm, f->uid, f->gid,
	    &f->time[0], &f->time[1]) != EPKG_OK) {
		close_tempdir(tmpdir);
		return (EPKG_FATAL);
	}
	if (tmpdir != NULL)
		set_chflags(fd, path, f->fflags);

	close_tempdir(tmpdir);
	return (EPKG_OK);
}

static int
do_extract_regfile(struct pkg *pkg, struct archive *a, struct archive_entry *ae,
    const char *path, struct pkg *local, tempdirs_t *tempdirs)
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

	if (create_regfile(pkg, f, a, ae, -1, local, tempdirs) == EPKG_FATAL)
		return (EPKG_FATAL);

	metalog_add(PKG_METALOG_FILE, RELATIVE_PATH(path),
	    archive_entry_uname(ae), archive_entry_gname(ae),
	    aest->st_mode & ~S_IFREG, f->fflags, NULL);

	return (EPKG_OK);
}

static int
do_extract(struct archive *a, struct archive_entry *ae,
    int nfiles, struct pkg *pkg, struct pkg *local, tempdirs_t *tempdirs)
{
	int	retcode = EPKG_OK;
	int	ret = 0, cur_file = 0;
	char	path[MAXPATHLEN];
	int (*extract_cb)(struct pkg *pkg, struct archive *a,
	    struct archive_entry *ae, const char *path, struct pkg *local,
	    tempdirs_t *tempdirs);

#ifndef HAVE_ARC4RANDOM
	srand(time(NULL));
#endif

	if (nfiles == 0)
		return (EPKG_OK);

	pkg_emit_extract_begin(pkg);
	pkg_emit_progress_start(NULL);

	do {
		pkg_absolutepath(archive_entry_pathname(ae), path, sizeof(path), true);
		if (match_ucl_lists(path,
		    pkg_config_get("FILES_IGNORE_GLOB"),
		    pkg_config_get("FILES_IGNORE_REGEX")))
			continue;
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
			pkg_emit_error("Archive contains an unsupported filetype (AE_IFIFO): %s", path);
			retcode = EPKG_FATAL;
			goto cleanup;
			break;
		case AE_IFBLK:
			pkg_emit_error("Archive contains an unsupported filetype (AE_IFBLK): %s", path);
			retcode = EPKG_FATAL;
			goto cleanup;
			break;
		default:
			pkg_emit_error("Archive contains an unsupported filetype (%d): %s", archive_entry_filetype(ae), path);
			retcode = EPKG_FATAL;
			goto cleanup;
			break;
		}

		if (extract_cb(pkg, a, ae, path, local, tempdirs) != EPKG_OK) {
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
pkg_extract_finalize(struct pkg *pkg, tempdirs_t *tempdirs)
{
	struct stat st;
	struct pkg_file *f = NULL;
	struct pkg_dir *d = NULL;
	char path[MAXPATHLEN + 8];
	const char *fto;
#ifdef HAVE_CHFLAGSAT
	bool install_as_user;

	install_as_user = (getenv("INSTALL_AS_USER") != NULL);
#endif


	if (tempdirs != NULL) {
		tll_foreach(*tempdirs, t) {
			if (renameat(pkg->rootfd, RELATIVE_PATH(t->item->temp),
			    pkg->rootfd, RELATIVE_PATH(t->item->name)) != 0) {
				pkg_fatal_errno("Fail to rename %s -> %s",
				    t->item->temp, t->item->name);
			}
			free(t->item);
		}
	}
	while (pkg_files(pkg, &f) == EPKG_OK) {

		if (match_ucl_lists(f->path,
		    pkg_config_get("FILES_IGNORE_GLOB"),
		    pkg_config_get("FILES_IGNORE_REGEX")))
			continue;
		append_touched_file(f->path);
		if (*f->temppath == '\0')
			continue;
		fto = f->path;
		if (f->config && f->config->status == MERGE_FAILED &&
		    f->previous != PKG_FILE_NONE) {
			snprintf(path, sizeof(path), "%s.pkgnew", f->path);
			fto = path;
		}

		if (f->config && f->config->status == MERGE_NOT_LOCAL) {
			snprintf(path, sizeof(path), "%s.pkgsave", f->path);
			if (renameat(pkg->rootfd, RELATIVE_PATH(fto),
			    pkg->rootfd, RELATIVE_PATH(path)) == -1) {
				pkg_fatal_errno("Fail to rename %s -> %s",
				  fto, path);
			}
		}

		/*
		 * enforce an unlink of the file to workaround a bug that
		 * results in renameat returning 0 of the from file is hardlink
		 * on the to file, but the to file is not removed
		 */
		if (f->previous != PKG_FILE_NONE &&
		    fstatat(pkg->rootfd, RELATIVE_PATH(fto), &st,
		    AT_SYMLINK_NOFOLLOW) != -1) {
#ifdef HAVE_CHFLAGSAT
			if (!install_as_user && st.st_flags & NOCHANGESFLAGS) {
				chflagsat(pkg->rootfd, RELATIVE_PATH(fto), 0,
				    AT_SYMLINK_NOFOLLOW);
			}
#endif
			/* if the files does not belong to any package, we do save it */
			if (f->previous == PKG_FILE_SAVE) {
				snprintf(path, sizeof(path), "%s.pkgsave", f->path);
				renameat(pkg->rootfd, RELATIVE_PATH(fto),
				    pkg->rootfd, RELATIVE_PATH(path));
			}
			unlinkat(pkg->rootfd, RELATIVE_PATH(fto), 0);
		}
		if (renameat(pkg->rootfd, RELATIVE_PATH(f->temppath),
		    pkg->rootfd, RELATIVE_PATH(fto)) == -1) {
			pkg_fatal_errno("Fail to rename %s -> %s",
			    f->temppath, fto);
		}

		if (set_chflags(pkg->rootfd, fto, f->fflags) != EPKG_OK)
			return (EPKG_FATAL);
	}

	while (pkg_dirs(pkg, &d) == EPKG_OK) {
		append_touched_dir(d->path);
		if (d->noattrs)
			continue;
		if (set_attrsat(pkg->rootfd, d->path, d->perm,
		    d->uid, d->gid, &d->time[0], &d->time[1]) != EPKG_OK)
			return (EPKG_FATAL);
		if (set_chflags(pkg->rootfd, d->path, d->fflags) != EPKG_OK)
			return (EPKG_FATAL);
	}
	if (tempdirs != NULL)
		tll_free(*tempdirs);

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
		buf2 = strrchr(g.gl_pathv[i], '/');
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
	if (path)
		path = xstrdup(path);
	globfree(&g);

	return (path);
}

static int
pkg_add_check_pkg_archive(struct pkgdb *db, struct pkg *pkg,
	const char *path, int flags, const char *location)
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
		basedir = get_dirname(bd);
		strlcpy(bd, basedir, sizeof(bd));
		if ((ext = strrchr(path, '.')) == NULL) {
			pkg_emit_error("%s has no extension", path);
			return (EPKG_FATAL);
		}
	}

	retcode = EPKG_FATAL;
	pkg_emit_add_deps_begin(pkg);

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		dpath[0] = '\0';

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
		}

		if (strlen(dpath) == 0 || access(dpath, F_OK) != 0) {
			snprintf(dpath, sizeof(dpath), "%s/%s-*%s", bd,
			    dep->name, ext);
			ppath = pkg_globmatch(dpath, dep->name);
			if (ppath == NULL) {
				pkg_emit_missing_dep(pkg, dep);
				if ((flags & PKG_ADD_FORCE_MISSING) == 0)
					goto cleanup;
				continue;
			}
			strlcpy(dpath, ppath, sizeof(dpath));
			free(ppath);
		}

		if ((flags & PKG_ADD_UPGRADE) == 0 &&
				access(dpath, F_OK) == 0) {
			ret = pkg_add(db, dpath, PKG_ADD_AUTOMATIC, location);

			if (ret != EPKG_OK)
				goto cleanup;
		} else {
			pkg_emit_missing_dep(pkg, dep);
			if ((flags & PKG_ADD_FORCE_MISSING) == 0)
				goto cleanup;
		}
	}

	retcode = EPKG_OK;
cleanup:
	pkg_emit_add_deps_finished(pkg);

	return (retcode);
}

static int
pkg_add_cleanup_old(struct pkgdb *db, struct pkg *old, struct pkg *new, struct triggers *t, int flags)
{
	struct pkg_file *f;
	int ret = EPKG_OK;

	pkg_start_stop_rc_scripts(old, PKG_RC_STOP);

	/*
	 * Execute pre deinstall scripts
	 */
	if ((flags & PKG_ADD_NOSCRIPT) == 0) {
		ret = pkg_lua_script_run(old, PKG_LUA_PRE_DEINSTALL, (old != NULL));
		if (ret != EPKG_OK && ctx.developer_mode) {
			return (ret);
		} else {
			ret = pkg_script_run(old, PKG_SCRIPT_PRE_DEINSTALL, (old != NULL));
			if (ret != EPKG_OK && ctx.developer_mode) {
				return (ret);
			} else {
				ret = EPKG_OK;
			}
		}
	}

	/* Now remove files that no longer exist in the new package */
	if (new != NULL) {
		f = NULL;
		while (pkg_files(old, &f) == EPKG_OK) {
			if (!pkg_has_file(new, f->path) || match_ucl_lists(f->path,
			    pkg_config_get("FILES_IGNORE_GLOB"),
			    pkg_config_get("FILES_IGNORE_REGEX"))) {
				pkg_debug(2, "File %s is not in the new package", f->path);
				if (ctx.backup_libraries) {
					const char *libname;
					libname = strrchr(f->path, '/');
					if (libname != NULL &&
					    stringlist_contains(&old->shlibs_provided, libname+1)) {
						backup_library(db, old, f->path);
					}
				}

				trigger_is_it_a_cleanup(t, f->path);
				pkg_delete_file(old, f);
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
		if (match_ucl_lists(f->path,
		    pkg_config_get("FILES_IGNORE_GLOB"),
		    pkg_config_get("FILES_IGNORE_REGEX")))
			continue;
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

int
pkg_add_triggers(void)
{
	return (triggers_execute(NULL));
}

static int
pkg_add_common(struct pkgdb *db, const char *path, unsigned flags,
    const char *reloc, struct pkg *remote,
    struct pkg *local, struct triggers *t)
{
	struct archive		*a;
	struct archive_entry	*ae;
	struct pkg		*pkg = NULL;
	xstring			*message = NULL;
	struct pkg_message	*msg;
	struct pkg_file		*f;
	const char		*msgstr;
	bool			 extract = true;
	int			 retcode = EPKG_OK;
	int			 ret;
	int			 nfiles;
	tempdirs_t		 tempdirs = tll_init();

	assert(path != NULL);

	/*
	 * Open the package archive file, read all the meta files and set the
	 * current archive_entry to the first non-meta file.
	 * If there is no non-meta files, EPKG_END is returned.
	 */
	ret = pkg_open2(&pkg, &a, &ae, path, 0, -1);
	if (ret == EPKG_END)
		extract = false;
	else if (ret != EPKG_OK) {
		retcode = ret;
		goto cleanup;
	}
	if ((flags & PKG_ADD_SPLITTED_UPGRADE) == 0)
		pkg_emit_new_action();
	if ((flags & (PKG_ADD_UPGRADE | PKG_ADD_SPLITTED_UPGRADE)) !=
	    PKG_ADD_UPGRADE)
		pkg_emit_install_begin(pkg);
	else
		pkg_emit_upgrade_begin(pkg, local);

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
		ret = pkg_add_check_pkg_archive(db, pkg, path, flags, reloc);
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
		/* only preserve flags if -A has not been passed */
		if ((flags & PKG_ADD_AUTOMATIC) == 0)
			pkg->automatic = remote->automatic;
	}

	if (reloc != NULL)
		pkg_kv_add(&pkg->annotations, "relocated", reloc, "annotation");

	pkg_open_root_fd(pkg);
	/* analyse previous files */
	f = NULL;
	while (pkg_files(pkg, &f) == EPKG_OK) {
		if (match_ucl_lists(f->path,
		    pkg_config_get("FILES_IGNORE_GLOB"),
		    pkg_config_get("FILES_IGNORE_REGEX"))) {
			continue;
		}
		if (faccessat(pkg->rootfd, RELATIVE_PATH(f->path), F_OK, 0) == 0) {
			f->previous = PKG_FILE_EXIST;
			if (!pkgdb_file_exists(db, f->path)) {
				f->previous = PKG_FILE_SAVE;
			}
		}
	}

	/* register the package before installing it in case there are
	 * problems that could be caught here. */
	retcode = pkgdb_register_pkg(db, pkg, flags & PKG_ADD_FORCE, NULL);

	if (retcode != EPKG_OK)
		goto cleanup;

	/*
	 * Execute pre-install scripts
	 */
	if ((flags & PKG_ADD_NOSCRIPT) == 0) {
		if ((retcode = pkg_lua_script_run(pkg, PKG_LUA_PRE_INSTALL, (local != NULL))) != EPKG_OK)
			goto cleanup;
		if ((retcode = pkg_script_run(pkg, PKG_SCRIPT_PRE_INSTALL, (local != NULL))) != EPKG_OK)
			goto cleanup;
	}


	/* add the user and group if necessary */

	nfiles = pkghash_count(pkg->filehash) + pkghash_count(pkg->dirhash);
	/*
	 * Extract the files on disk.
	 */
	if (extract) {
		pkg_register_cleanup_callback(pkg_rollback_cb, pkg);
		retcode = do_extract(a, ae, nfiles, pkg, local, &tempdirs);
		pkg_unregister_cleanup_callback(pkg_rollback_cb, pkg);
		if (retcode != EPKG_OK) {
			/* If the add failed, clean up (silently) */

			pkg_rollback_pkg(pkg);
			pkg_delete_dirs(db, pkg, NULL);
			pkgdb_register_finale(db, retcode, NULL);
			goto cleanup;
		}
	}

	/*
	 * If this was a split upgrade, the old package has been entirely
	 * removed already.
	 */
	if (local != NULL && (flags & PKG_ADD_SPLITTED_UPGRADE) == 0) {
		pkg_open_root_fd(local);
		pkg_debug(1, "Cleaning up old version");
		if (pkg_add_cleanup_old(db, local, pkg, t, flags) != EPKG_OK) {
			retcode = EPKG_FATAL;
			goto cleanup;
		}
	}


	/* Update configuration file content with db with newer versions */
	pkgdb_update_config_file_content(pkg, db->sqlite);

	retcode = pkg_extract_finalize(pkg, &tempdirs);

	pkgdb_register_finale(db, retcode, NULL);
	/*
	 * Execute post install scripts
	 */

	if (retcode != EPKG_OK)
		goto cleanup;
	if ((flags & PKG_ADD_NOSCRIPT) == 0) {
		pkg_lua_script_run(pkg, PKG_LUA_POST_INSTALL, (local != NULL));
		pkg_script_run(pkg, PKG_SCRIPT_POST_INSTALL, (local != NULL));
	}

	/*
	 * start the different related services if the users do want that
	 * and that the service is running
	 */

	pkg_start_stop_rc_scripts(pkg, PKG_RC_START);

	if ((flags & (PKG_ADD_UPGRADE | PKG_ADD_SPLITTED_UPGRADE)) !=
	    PKG_ADD_UPGRADE)
		pkg_emit_install_finished(pkg, local);
	else
		pkg_emit_upgrade_finished(pkg, local);

	tll_foreach(pkg->message, m) {
		msg = m->item;
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
			if (message == NULL) {
				message = xstring_new();
				pkg_fprintf(message->fp, "=====\nMessage from "
				    "%n-%v:\n\n", pkg, pkg);
			}
			fprintf(message->fp, "--\n%s\n", msgstr);
		}
	}
	if (pkg_has_message(pkg) && message != NULL) {
		fflush(message->fp);
		pkg_emit_message(message->buf);
		xstring_free(message);
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
    const char *location)
{
	return pkg_add_common(db, path, flags, location, NULL, NULL, NULL);
}

int
pkg_add_from_remote(struct pkgdb *db, const char *path, unsigned flags,
    const char *location, struct pkg *rp, struct triggers *t)
{
	return pkg_add_common(db, path, flags, location, rp, NULL, t);
}

int
pkg_add_upgrade(struct pkgdb *db, const char *path, unsigned flags,
    const char *location,
    struct pkg *rp, struct pkg *lp, struct triggers *t)
{
	if (pkgdb_ensure_loaded(db, lp,
	    PKG_LOAD_FILES|PKG_LOAD_SCRIPTS|PKG_LOAD_DIRS|PKG_LOAD_LUA_SCRIPTS) != EPKG_OK)
		return (EPKG_FATAL);

	return pkg_add_common(db, path, flags, location, rp, lp, t);
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
	int err, fd, fromfd;
	int retcode;
	hardlinks_t hardlinks = tll_init();
	const char *path;
	char buffer[1024];
	size_t link_len;
	bool install_as_user;
	tempdirs_t tempdirs = tll_init();

	install_as_user = (getenv("INSTALL_AS_USER") != NULL);

	fromfd = open(src, O_DIRECTORY);
	if (fromfd == -1) {
		pkg_fatal_errno("Unable to open source directory '%s'", src);
	}
	pkg_open_root_fd(pkg);

	while (pkg_dirs(pkg, &d) == EPKG_OK) {
		if (fstatat(fromfd, RELATIVE_PATH(d->path), &st, 0) == -1) {
			close(fromfd);
			pkg_fatal_errno("%s%s", src, d->path);
		}
		if (d->perm == 0)
			d->perm = st.st_mode & ~S_IFMT;
		if (d->uname[0] != '\0') {
			err = getpwnam_r(d->uname, &pwent, buffer,
			    sizeof(buffer), &pw);
			if (err != 0) {
				pkg_emit_errno("getpwnam_r", d->uname);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			d->uid = pwent.pw_uid;
		} else {
			d->uid = install_as_user ? st.st_uid : 0;
		}
		if (d->gname[0] != '\0') {
			err = getgrnam_r(d->gname, &grent, buffer,
			    sizeof(buffer), &gr);
			if (err != 0) {
				pkg_emit_errno("getgrnam_r", d->gname);
				retcode = EPKG_FATAL;
				goto cleanup;
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

		if (create_dir(pkg, d, &tempdirs) == EPKG_FATAL) {
			retcode = EPKG_FATAL;
			goto cleanup;
		}
	}

	while (pkg_files(pkg, &f) == EPKG_OK) {
		if (match_ucl_lists(f->path,
		    pkg_config_get("FILES_IGNORE_GLOB"),
		    pkg_config_get("FILES_IGNORE_REGEX")))
			continue;
		if (fstatat(fromfd, RELATIVE_PATH(f->path), &st,
		    AT_SYMLINK_NOFOLLOW) == -1) {
			tll_free_and_free(hardlinks, free);
			close(fromfd);
			pkg_fatal_errno("%s%s", src, f->path);
		}
		if (f->uname[0] != '\0') {
			err = getpwnam_r(f->uname, &pwent, buffer,
			    sizeof(buffer), &pw);
			if (err != 0) {
				pkg_emit_errno("getpwnam_r", f->uname);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			f->uid = pwent.pw_uid;
		} else {
			f->uid = install_as_user ? st.st_uid : 0;
		}

		if (f->gname[0] != '\0') {
			err = getgrnam_r(f->gname, &grent, buffer,
			    sizeof(buffer), &gr);
			if (err != 0) {
				pkg_emit_errno("getgrnam_r", f->gname);
				retcode = EPKG_FATAL;
				goto cleanup;
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
				tll_free_and_free(hardlinks, free);
				close(fromfd);
				pkg_fatal_errno("Impossible to read symlinks "
				    "'%s'", f->path);
			}
			target[link_len] = '\0';
			if (create_symlinks(pkg, f, target, &tempdirs) == EPKG_FATAL) {
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		} else if (S_ISREG(st.st_mode)) {
			if ((fd = openat(fromfd, RELATIVE_PATH(f->path),
			    O_RDONLY)) == -1) {
				tll_free_and_free(hardlinks, free);
				close(fromfd);
				pkg_fatal_errno("Impossible to open source file"
				    " '%s'", RELATIVE_PATH(f->path));
			}
			path = NULL;
			tll_foreach(hardlinks, hit) {
				if (hit->item->ino == st.st_ino &&
				    hit->item->dev == st.st_dev) {
					path = hit->item->path;
					break;
				}
			}
			if (path != NULL) {
				if (create_hardlink(pkg, f, path, &tempdirs) == EPKG_FATAL) {
					close(fd);
					retcode = EPKG_FATAL;
					goto cleanup;
				}
			} else {
				if (create_regfile(pkg, f, NULL, NULL, fd, NULL, &tempdirs) == EPKG_FATAL) {
					close(fd);
					retcode = EPKG_FATAL;
					goto cleanup;
				}
				struct hardlink *h = xcalloc(1, sizeof(*h));
				h->ino = st.st_ino;
				h->dev = st.st_dev;
				h->path = f->path;
				tll_push_back(hardlinks, h);
			}
			close(fd);
		} else {
			pkg_emit_error("Invalid file type");
			retcode = EPKG_FATAL;
			goto cleanup;
		}
	}

	retcode = pkg_extract_finalize(pkg, &tempdirs);

cleanup:
	tll_free_and_free(hardlinks, free);
	close(fromfd);
	return (retcode);
}

