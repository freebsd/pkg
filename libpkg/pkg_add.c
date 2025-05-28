/*-
 * Copyright (c) 2011-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2016, Vsevolod Stakhov
 * Copyright (c) 2024, Future Crew, LLC
 *                     Author: Gleb Popov <arrowd@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
#include <sys/wait.h>
#include <time.h>
#include <xstring.h>

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/add.h"

#if defined(UF_NOUNLINK)
#define NOCHANGESFLAGS	(UF_IMMUTABLE | UF_APPEND | UF_NOUNLINK | SF_IMMUTABLE | SF_APPEND | SF_NOUNLINK)
#else
#define NOCHANGESFLAGS	(UF_IMMUTABLE | UF_APPEND | SF_IMMUTABLE | SF_APPEND)
#endif

struct pkg_add_db {
	struct pkgdb *db;
	pkgs_t localpkgs;
	charv_t system_shlibs;
	bool local_scanned;
	bool ignore_compat32;
	bool pkgbase;
};

static int pkg_add_common(struct pkg_add_db *db, const char *path, unsigned flags, const char *reloc, struct pkg *remote, struct pkg *local, struct triggers *t);

struct external_merge_tmp_file {
	int fd;
	const char *template;
	char path[MAXPATHLEN];
	const char *content;
	size_t content_len;
};

static merge_status
merge_with_external_tool(const char *merge_tool, struct pkg_config_file *lcf,
    size_t lcf_len, struct pkg_config_file *rcf, char *localconf, char **mergedconf)
{
	pid_t wait_res;
	int status;
	FILE *inout[2];

	char *tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL)
		tmpdir = "/tmp";

	int output_fd;
	char output_path[MAXPATHLEN];
	off_t output_sz;

	strlcpy(output_path, tmpdir, sizeof(output_path));
	strlcat(output_path, "/OUTPUT.XXXXXXXXXX", sizeof(output_path));
	output_fd = mkstemp(output_path);
	if (output_fd == -1) {
		pkg_emit_error("Can't create %s", output_path);
		return MERGE_FAILED;
	}
	close(output_fd);

	struct external_merge_tmp_file tmp_files[] = {
		{-1, "/BASE.XXXXXXXXXX", {0}, lcf->content, lcf_len},
		{-1, "/LOCAL.XXXXXXXXXX", {0}, localconf, strlen(localconf)},
		{-1, "/REMOTE.XXXXXXXXXX", {0}, rcf->content, strlen(rcf->content)}
	};
	bool tmp_files_ok = true;
	for (int i = 0; i < NELEM(tmp_files); i++) {
		int copied = strlcpy(tmp_files[i].path, tmpdir, sizeof(tmp_files[i].path));
		if (copied >= sizeof(tmp_files[i].path)) {
			pkg_emit_error("Temporary path too long: %s", tmp_files[i].path);
			return MERGE_FAILED;
		}
		copied = strlcat(tmp_files[i].path, tmp_files[i].template, sizeof(tmp_files[i].path));
		if (copied >= sizeof(tmp_files[i].path)) {
			pkg_emit_error("Temporary path too long: %s", tmp_files[i].path);
			return MERGE_FAILED;
		}

		tmp_files[i].fd = mkstemp(tmp_files[i].path);
		if (tmp_files[i].fd == -1) {
			pkg_emit_error("Can't create %s", tmp_files[i].path);
			tmp_files_ok = false;
			break;
		}
		if (write(tmp_files[i].fd, tmp_files[i].content, tmp_files[i].content_len) == -1) {
			pkg_emit_error("Failed to write %s", tmp_files[i].path);
			tmp_files_ok = false;
			break;
		}
		close(tmp_files[i].fd);
		tmp_files[i].fd = -1;
	}
	if (!tmp_files_ok) {
		for (int i = 0; i < NELEM(tmp_files); i++) {
			if (tmp_files[i].fd != -1)
				close(tmp_files[i].fd);
			if (strlen(tmp_files[i].path))
				unlink(tmp_files[i].path);
		}
		return MERGE_FAILED;
	}

	char command[MAXPATHLEN];
	for (int i = 0; *merge_tool != '\0'; i++, merge_tool++) {
		if (*merge_tool != '%') {
			command[i] = *merge_tool;
			continue;
		}
		merge_tool++;
		int tmp_files_index;
		switch (*merge_tool) {
		case 'b':
			tmp_files_index = 0;
			break;
		case 'l':
			tmp_files_index = 1;
			break;
		case 'r':
			tmp_files_index = 2;
			break;
		case 'n':
			i += strlcpy(&command[i], RELATIVE_PATH(rcf->path), sizeof(command) - i) - 1;
			continue;
		case 'o':
			i += strlcpy(&command[i], output_path, sizeof(command) - i) - 1;
			continue;
		default:
			pkg_emit_error("Unknown format string in the MERGETOOL command");
			merge_tool--;
			continue;
		}
		i += strlcpy(&command[i], tmp_files[tmp_files_index].path, sizeof(command) - i) - 1;
	}

	pid_t pid = process_spawn_pipe(inout, command);
	wait_res = waitpid(pid, &status, 0);

	fclose(inout[0]);
	fclose(inout[1]);
	for (int i = 0; i < sizeof(tmp_files); i++) {
		unlink(tmp_files[i].path);
	}

	if (wait_res == -1 || WIFSIGNALED(status) || WEXITSTATUS(status)) {
		unlink(output_path);
		pkg_emit_error("External merge tool failed, retrying with builtin algorithm");
		return MERGE_FAILED;
	}

	file_to_bufferat(AT_FDCWD, output_path, mergedconf, &output_sz);
	unlink(output_path);

	return MERGE_SUCCESS;
}

static void
attempt_to_merge(int rootfd, struct pkg_config_file *rcf, struct pkg *local,
    bool merge, const char *merge_tool)
{
	const struct pkg_file *lf = NULL;
	struct stat st;
	xstring *newconf;
	struct pkg_config_file *lcf = NULL;
	size_t lcf_len;

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

	lcf_len = strlen(lcf->content);
	if (sz == lcf_len) {
		pkg_debug(2, "Ancient vanilla and deployed conf are the same size testing checksum");
		localsum = pkg_checksum_data(localconf, sz,
		    PKG_HASH_TYPE_SHA256_HEX);
		if (localsum && STREQ(localsum, lf->sum)) {
			pkg_debug(2, "Checksum are the same %jd", (intmax_t)strlen(localconf));
			free(localsum);
			goto ret;
		}
		free(localsum);
		pkg_debug(2, "Checksum are different %jd", (intmax_t)strlen(localconf));
	}
	rcf->status = MERGE_FAILED;
	if (!merge) {
		goto ret;
	}

	pkg_debug(1, "Attempting to merge %s", rcf->path);
	if (merge_tool) {
		char* mergedconf = NULL;
		rcf->status = merge_with_external_tool(merge_tool, lcf, lcf_len, rcf, localconf, &mergedconf);
		rcf->newcontent = mergedconf;
		if (rcf->status == MERGE_SUCCESS)
			goto ret;
	}
	newconf = xstring_new();
	if (merge_3way(lcf->content, localconf, rcf->content, newconf) != 0) {
		xstring_free(newconf);
		pkg_emit_error("Impossible to merge configuration file: %s", rcf->path);
	} else {
		char *conf = xstring_get(newconf);
		rcf->newcontent = conf;
		rcf->status = MERGE_SUCCESS;
	}
ret:
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
	if (pwent.pw_name != NULL && STREQ(user, pwent.pw_name))
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
	if (grent.gr_name != NULL && STREQ(group, grent.gr_name))
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
	struct stat st;
	struct timespec times[2];

	times[0] = *ats;
	times[1] = *mts;
	if (utimensat(fd, RELATIVE_PATH(path), times,
	    AT_SYMLINK_NOFOLLOW) == -1 && errno != EOPNOTSUPP){
		pkg_fatal_errno("Fail to set time on %s", path);
	}

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
get_tempdir(struct pkg_add_context *context, const char *path, tempdirs_t *tempdirs)
{
	struct tempdir *tmpdir = NULL;

	vec_foreach(*tempdirs, i) {
		tmpdir = tempdirs->d[i];
		if (strncmp(tmpdir->name, path, tmpdir->len) == 0 && path[tmpdir->len] == '/') {
			reopen_tempdir(context->rootfd, tmpdir);
			return (tmpdir);
		}
	}

	tmpdir = open_tempdir(context, path);
	if (tmpdir != NULL)
		vec_push(tempdirs, tmpdir);

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
create_dir(struct pkg_add_context *context, struct pkg_dir *d, tempdirs_t *tempdirs)
{
	struct stat st;
	struct tempdir *tmpdir = NULL;
	int fd;
	const char *path;

	tmpdir = get_tempdir(context, d->path, tempdirs);
	if (tmpdir == NULL) {
		fd = context->rootfd;
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
do_extract_dir(struct pkg_add_context* context, struct archive *a __unused, struct archive_entry *ae,
    const char *path, struct pkg *local __unused, tempdirs_t *tempdirs)
{
	struct pkg_dir *d;
	const struct stat *aest;
	unsigned long clear;

	d = pkg_get_dir(context->pkg, path);
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

	if (create_dir(context, d, tempdirs) == EPKG_FATAL) {
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
create_symlinks(struct pkg_add_context *context, struct pkg_file *f, const char *target, tempdirs_t *tempdirs)
{
	struct tempdir *tmpdir = NULL;
	int fd;
	const char *path;
	bool tried_mkdir = false;

	tmpdir = get_tempdir(context, f->path, tempdirs);
	if (tmpdir == NULL && errno == 0)
		hidden_tempfile(f->temppath, sizeof(f->temppath), f->path);
	if (tmpdir == NULL) {
		fd = context->rootfd;
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
do_extract_symlink(struct pkg_add_context *context, struct archive *a __unused, struct archive_entry *ae,
    const char *path, struct pkg *local __unused, tempdirs_t *tempdirs)
{
	struct pkg_file *f;
	const struct stat *aest;
	unsigned long clear;

	f = pkg_get_file(context->pkg, path);
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

	if (create_symlinks(context, f, archive_entry_symlink(ae), tempdirs) == EPKG_FATAL)
		return (EPKG_FATAL);

	metalog_add(PKG_METALOG_LINK, RELATIVE_PATH(path),
	    archive_entry_uname(ae), archive_entry_gname(ae),
	    aest->st_mode & ~S_IFLNK, f->fflags, archive_entry_symlink(ae));

	return (EPKG_OK);
}

static int
create_hardlink(struct pkg_add_context *context, struct pkg_file *f, const char *path, tempdirs_t *tempdirs)
{
	bool tried_mkdir = false;
	struct pkg_file *fh;
	int fd, fdh;
	const char *pathfrom, *pathto;
	struct tempdir *tmpdir = NULL;
	struct tempdir *tmphdir = NULL;

	tmpdir = get_tempdir(context, f->path, tempdirs);
	if (tmpdir == NULL && errno == 0)
		hidden_tempfile(f->temppath, sizeof(f->temppath), f->path);
	if (tmpdir != NULL) {
		fd = tmpdir->fd;
	} else {
		fd = context->rootfd;
	}
	fh = pkg_get_file(context->pkg, path);
	if (fh == NULL) {
		close_tempdir(tmpdir);
		pkg_emit_error("Can't find the file %s is supposed to be"
		    " hardlinked to %s", f->path, path);
		return (EPKG_FATAL);
	}
	if (fh->temppath[0] == '\0') {
		vec_foreach(*tempdirs, i) {
			if (strncmp(tempdirs->d[i]->name, fh->path, tempdirs->d[i]->len) == 0 &&
			    fh->path[tempdirs->d[i]->len] == '/' ) {
				tmphdir = tempdirs->d[i];
				reopen_tempdir(context->rootfd, tmphdir);
				break;
			}
		}
	}
	if (tmpdir == NULL) {
		pathto = f->temppath;
		fd = context->rootfd;
	} else {
		pathto = f->path + tmpdir->len;
		fd = tmpdir->fd;
	}

	if (tmphdir == NULL) {
		pathfrom = fh->temppath;
		fdh = context->rootfd;
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
do_extract_hardlink(struct pkg_add_context *context, struct archive *a __unused, struct archive_entry *ae,
    const char *path, struct pkg *local __unused, tempdirs_t *tempdirs)
{
	struct pkg_file *f;
	const struct stat *aest;
	const char *lp;

	f = pkg_get_file(context->pkg, path);
	if (f == NULL) {
		pkg_emit_error("Hardlink %s not specified in the manifest", path);
		return (EPKG_FATAL);
	}
	lp = archive_entry_hardlink(ae);
	aest = archive_entry_stat(ae);

	if (create_hardlink(context, f, lp, tempdirs) == EPKG_FATAL)
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
create_regfile(struct pkg_add_context *context, struct pkg_file *f, struct archive *a,
    struct archive_entry *ae, int fromfd, struct pkg *local, tempdirs_t *tempdirs)
{
	int fd = -1;
	size_t len;
	char buf[32768];
	char *path;
	struct tempdir *tmpdir = NULL;

	tmpdir = get_tempdir(context, f->path, tempdirs);
	if (tmpdir == NULL && errno == 0)
		hidden_tempfile(f->temppath, sizeof(f->temppath), f->path);

	if (tmpdir != NULL) {
		fd = open_tempfile(tmpdir->fd, f->path + tmpdir->len, f->perm);
	} else {
		fd = open_tempfile(context->rootfd, f->temppath,  f->perm);
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
		f->config = pkghash_get_value(context->pkg->config_files_hash, f->path);
		if (f->config) {
			const char *cfdata;
			bool merge = pkg_object_bool(pkg_config_get("AUTOMERGE"));
			const char *merge_tool = pkg_object_string(pkg_config_get("MERGETOOL"));

			pkg_debug(1, "Populating config_file %s", f->path);
			len = archive_entry_size(ae);
			f->config->content = xmalloc(len + 1);
			archive_read_data(a, f->config->content, len);
			f->config->content[len] = '\0';
			cfdata = f->config->content;
			attempt_to_merge(context->rootfd, f->config, local, merge, merge_tool);
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
		fd = context->rootfd;
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
do_extract_regfile(struct pkg_add_context *context, struct archive *a, struct archive_entry *ae,
    const char *path, struct pkg *local, tempdirs_t *tempdirs)
{
	struct pkg_file *f;
	const struct stat *aest;
	unsigned long clear;

	f = pkg_get_file(context->pkg, path);
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

	if (create_regfile(context, f, a, ae, -1, local, tempdirs) == EPKG_FATAL)
		return (EPKG_FATAL);

	metalog_add(PKG_METALOG_FILE, RELATIVE_PATH(path),
	    archive_entry_uname(ae), archive_entry_gname(ae),
	    aest->st_mode & ~S_IFREG, f->fflags, NULL);

	return (EPKG_OK);
}

static int
do_extract(struct archive *a, struct archive_entry *ae,
    int nfiles, struct pkg *local, tempdirs_t *tempdirs,
    struct pkg_add_context *context)
{
	int	retcode = EPKG_OK;
	int	ret = 0, cur_file = 0;
	char	path[MAXPATHLEN];
	int (*extract_cb)(struct pkg_add_context *context, struct archive *a,
	    struct archive_entry *ae, const char *path, struct pkg *local,
	    tempdirs_t *tempdirs);

#ifndef HAVE_ARC4RANDOM
	srand(time(NULL));
#endif

	if (nfiles == 0)
		return (EPKG_OK);

	pkg_emit_extract_begin(context->pkg);
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

		if (extract_cb(context, a, ae, path, local, tempdirs) != EPKG_OK) {
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
	pkg_emit_extract_finished(context->pkg);

	return (retcode);
}

static void
backup_file_if_needed(struct pkg *p, struct pkg_file *f)
{
	char path[MAXPATHLEN];
	struct stat st;
	char *sum;
	pkg_checksum_type_t t;

	if (fstatat(p->rootfd, RELATIVE_PATH(f->path), &st,
	    AT_SYMLINK_NOFOLLOW) == -1)
		return;

	if (S_ISLNK(st.st_mode))
		return;

	if (S_ISREG(st.st_mode)) {
		t = pkg_checksum_file_get_type(f->sum, -1);
		sum = pkg_checksum_generate_fileat(p->rootfd,
		    RELATIVE_PATH(f->path), t);
		if (sum == NULL)
			return;

		if (STREQ(sum, f->sum)) {
			free(sum);
			return;
		}
		free(sum);
	}

	snprintf(path, sizeof(path), "%s.pkgsave", f->path);
	renameat(p->rootfd, RELATIVE_PATH(f->path),
	    p->rootfd, RELATIVE_PATH(path));
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
		vec_foreach(*tempdirs, i) {
			struct tempdir *t = tempdirs->d[i];
			if (renameat(pkg->rootfd, RELATIVE_PATH(t->temp),
			    pkg->rootfd, RELATIVE_PATH(t->name)) != 0) {
				pkg_fatal_errno("Fail to rename %s -> %s",
				    t->temp, t->name);
			}
			free(t);
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
			backup_file_if_needed(pkg, f);
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
				backup_file_if_needed(pkg, f);
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
		vec_free(tempdirs);

	return (EPKG_OK);
}

static bool
should_append_pkg(pkgs_t *localpkgs, struct pkg *p)
{
	/* only keep the highest version is we fine one */
	struct pkg **lp = pkgs_insert_sorted(localpkgs, p);
	if (lp != NULL) {
		if (pkg_version_cmp((*lp)->version, p->version) == -1) {
			pkg_free(*lp);
			*lp = p;
			return (true);
		}
		return (false);
	}
	return (true);
}

struct localhashes {
	kvlist_t shlibs_provides;
	kvlist_t provides;
};

static FILE *
open_cache_read(void)
{
	FILE *fp;
	int cfd, fd;

	cfd = pkg_get_cachedirfd();
	if (cfd == -1)
		return (NULL);
	if ((fd = openat(cfd, "pkg_add_cache", O_RDONLY)) == -1)
		return (NULL);
	if ((fp = fdopen(fd, "r")) == NULL)
		close(fd);
	return (fp);
}

static FILE *
open_cache_write(void)
{
	FILE *fp;
	int cfd, fd;

	cfd = pkg_get_cachedirfd();
	if (cfd == -1) {
		if (errno == ENOENT) {
			if (pkg_mkdirs(ctx.cachedir) != EPKG_OK)
				return (NULL);
		} else {
			return (NULL);
		}
		cfd = pkg_get_cachedirfd();
		if (cfd == -1)
			return (NULL);
	}
	if ((fd = openat(cfd, "pkg_add_cache", O_WRONLY|O_TRUNC)) == -1)
		return (NULL);
	if ((fp = fdopen(fd, "w")) == NULL)
		close(fd);
	return (fp);
}

static void
scan_local_pkgs(struct pkg_add_db *db, bool fromstdin, struct localhashes *l, const char *bd, const char *ext)
{
	if (!fromstdin) {
		if (!db->local_scanned) {
			bool package_building = (getenv("PACKAGE_BUILDING") != NULL);
			bool cache_exist = false;
			if (package_building) {
				FILE * fp;
				if ((fp = open_cache_read()) != NULL) {
					cache_exist = true;
					char *line = NULL;
					size_t linecap = 0;
					ssize_t linelen;
					while ((linelen = getline(&line, &linecap, fp)) > 0) {
						struct pkg * p = NULL;
						pkg_new(&p, PKG_FILE);
						pkg_parse_manifest(p, line, linelen);
						vec_push(&db->localpkgs, p);
					}
					free(line);
				}
			}
			if (db->localpkgs.len == 0) {
				glob_t g;
				char *pattern;
				xasprintf(&pattern, "%s/*%s" , bd, ext);
				db->local_scanned = true;
				if (glob(pattern, 0, NULL, &g) == 0) {
					for (int i = 0; i <g.gl_pathc; i++) {
						struct pkg *p = NULL;
						if (pkg_open(&p, g.gl_pathv[i],
									PKG_OPEN_MANIFEST_COMPACT) == EPKG_OK) {
							if (should_append_pkg(&db->localpkgs, p)) {
								p->repopath = xstrdup(g.gl_pathv[i]);
							}
						}
					}
				}
				globfree(&g);
				free(pattern);
			}
			if (package_building && !cache_exist) {
				FILE *fp = open_cache_write();
				if (fp != NULL) {
					vec_foreach(db->localpkgs, i) {
						pkg_emit_manifest_file(db->localpkgs.d[i], fp, PKG_MANIFEST_EMIT_COMPACT);
						fputs("\n", fp);
					}
					fclose(fp);
				}
			}
		}
		vec_foreach(db->localpkgs, i) {
			struct pkg *p = db->localpkgs.d[i];
			vec_foreach(p->shlibs_provided, j) {
				struct pkg_kv *kv = pkg_kv_new(p->shlibs_provided.d[j], p->repopath);
				if (pkg_kv_insert_sorted(&l->shlibs_provides, kv) != NULL)
					pkg_kv_free(kv);
			}
			vec_foreach(p->provides, j) {
				struct pkg_kv *kv = pkg_kv_new(p->provides.d[j], p->repopath);
				if (pkg_kv_insert_sorted(&l->provides, kv) != NULL)
					pkg_kv_free(kv);
			}
		}
	}
}

static const char *
_localpkgs_get(pkgs_t *pkgs, const char *name)
{
	struct pkg **lp = pkgs_search(pkgs, name);
	if (lp != NULL)
		return ((*lp)->repopath);
	return (NULL);
}

static int
pkg_add_check_pkg_archive(struct pkg_add_db *db, struct pkg *pkg,
	const char *path, int flags, const char *location)
{
	const char	*arch;
	int	ret, retcode;
	struct pkg_dep	*dep = NULL;
	char	bd[MAXPATHLEN];
	const char	*ext = NULL;
	struct pkg	*pkg_inst = NULL;
	bool	fromstdin;
	struct localhashes l;
	bool scanned = false;

	memset(&l, 0, sizeof(l));

	arch = pkg->abi != NULL ? pkg->abi : pkg->altabi;

	if (!is_valid_abi(arch, true) && (flags & PKG_ADD_FORCE) == 0) {
		return (EPKG_FATAL);
	}

	if (!is_valid_os_version(pkg) && (flags & PKG_ADD_FORCE) == 0) {
		return (EPKG_FATAL);
	}

	/* XX check */
	ret = pkg_try_installed(db->db, pkg->name, &pkg_inst, PKG_LOAD_BASIC);
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
	fromstdin = STREQ(path, "-");
	strlcpy(bd, path, sizeof(bd));
	if (!fromstdin) {
		/* In-place truncate bd to the directory components. */
		char *basedir = strrchr(bd, '/');
		if (NULL == basedir) {
			bd[0]='.';
			bd[1]='\0';
		} else {
			*basedir = '\0';
		}
		if ((ext = strrchr(path, '.')) == NULL) {
			pkg_emit_error("%s has no extension", path);
			return (EPKG_FATAL);
		}
	}

	retcode = EPKG_FATAL;
	pkg_emit_add_deps_begin(pkg);

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		const char *founddep = NULL;

		if (pkg_is_installed(db->db, dep->name) == EPKG_OK)
			continue;

		if (fromstdin) {
			pkg_emit_missing_dep(pkg, dep);
			if ((flags & PKG_ADD_FORCE_MISSING) == 0)
				goto cleanup;
			continue;
		}

		if (!scanned) {
			scan_local_pkgs(db, fromstdin, &l, bd, ext);
			scanned = true;
		}
		if ((founddep = _localpkgs_get(&db->localpkgs, dep->name)) == NULL) {
			pkg_emit_missing_dep(pkg, dep);
			if ((flags & PKG_ADD_FORCE_MISSING) == 0)
				goto cleanup;
			continue;
		}

		if ((flags & PKG_ADD_UPGRADE) == 0 &&
				access(founddep, F_OK) == 0) {
			pkg_debug(2, "Installing %s because of direct dependency: %s", founddep, dep->name);
			ret = pkg_add_common(db, founddep, PKG_ADD_AUTOMATIC, location, NULL, NULL, NULL);

			if (ret != EPKG_OK)
				goto cleanup;
		} else {
			pkg_emit_missing_dep(pkg, dep);
			if ((flags & PKG_ADD_FORCE_MISSING) == 0)
				goto cleanup;
		}
	}

	vec_foreach(pkg->shlibs_required, i) {
		char *s = pkg->shlibs_required.d[i];
		pkg_debug(2, "%s requires %s", pkg->name, s);
		if (!db->pkgbase && db->system_shlibs.len == 0) {
			int ret;
			ret = scan_system_shlibs(&db->system_shlibs, ctx.pkg_rootdir);
			if (ret == EPKG_NOCOMPAT32)
				db->ignore_compat32 = true;
		}
		if (charv_search(&db->system_shlibs, s) != NULL)
			continue;
		const struct pkg_kv *founddep = NULL;
		if (pkgdb_is_shlib_provided(db->db, s))
			continue;

		if (fromstdin) {
			pkg_emit_error("Missing shlib dependency: %s", s);
			if ((flags & PKG_ADD_FORCE_MISSING) == 0)
				goto cleanup;
			continue;
		}
		if (!scanned) {
			scan_local_pkgs(db, fromstdin, &l, bd, ext);
			scanned = true;
		}
		if ((founddep = pkg_kv_search(&l.shlibs_provides, s)) == NULL) {
			pkg_emit_error("Missing shlib dependency: %s", s);
			if ((flags & PKG_ADD_FORCE_MISSING) == 0)
				goto cleanup;
			continue;
		}
		if ((flags & PKG_ADD_UPGRADE) == 0 &&
				access(founddep->value, F_OK) == 0) {
			pkg_debug(2, "Installing %s because of shlibs_required: %s", founddep->value, s);
			ret = pkg_add_common(db, founddep->value, PKG_ADD_AUTOMATIC, location, NULL, NULL, NULL);

			if (ret != EPKG_OK)
				goto cleanup;
		} else {
			pkg_emit_missing_dep(pkg, dep);
			if ((flags & PKG_ADD_FORCE_MISSING) == 0)
				goto cleanup;
		}
	}

	vec_foreach(pkg->requires, i) {
		char *s = pkg->requires.d[i];
		const struct pkg_kv *founddep = NULL;
		if (pkgdb_is_provided(db->db, s))
			continue;

		if (fromstdin) {
			pkg_emit_error("Missing require dependency: %s", s);
			if ((flags & PKG_ADD_FORCE_MISSING) == 0)
				goto cleanup;
			continue;
		}
		if (!scanned) {
			scan_local_pkgs(db, fromstdin, &l, bd, ext);
			scanned = true;
		}
		if ((founddep = pkg_kv_search(&l.provides, s)) == NULL) {
			pkg_emit_error("Missing require dependency: %s", s);
			if ((flags & PKG_ADD_FORCE_MISSING) == 0)
				goto cleanup;
			continue;
		}
		if ((flags & PKG_ADD_UPGRADE) == 0 &&
				access(founddep->value, F_OK) == 0) {
			pkg_debug(2, "Installing %s because of requires: %s", founddep->value, s);
			ret = pkg_add_common(db, founddep->value, PKG_ADD_AUTOMATIC, location, NULL, NULL, NULL);

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
	vec_free_and_free(&l.shlibs_provides, pkg_kv_free);
	vec_free_and_free(&l.provides, pkg_kv_free);
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
		bool noexec = ((flags & PKG_ADD_NOEXEC) == PKG_ADD_NOEXEC);
		ret = pkg_lua_script_run(old, PKG_LUA_PRE_DEINSTALL, (old != NULL));
		if (ret != EPKG_OK && ctx.developer_mode) {
			return (ret);
		} else {
			ret = pkg_script_run(old, PKG_SCRIPT_PRE_DEINSTALL, (old != NULL),
			    noexec);
			if (ret != EPKG_OK && (ctx.developer_mode || noexec)) {
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
					    charv_search(&old->shlibs_provided, libname+1) != NULL) {
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
pkg_add_common(struct pkg_add_db *db, const char *path, unsigned flags,
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
	bool			 extract = true, openxact = false;
	int			 retcode = EPKG_OK;
	int			 ret;
	int			 nfiles;
	tempdirs_t		 tempdirs = vec_init();
	struct pkg_add_context	 context;

	memset(&context, 0, sizeof(context));

	assert(path != NULL);

	/*
	 * Open the package archive file, read all the meta files and set the
	 * current archive_entry to the first non-meta file.
	 * If there is no non-meta files, EPKG_END is returned.
	 */
	ret = pkg_open2(&pkg, &a, &ae, path, 0, -1);
	context.pkg = pkg;
	context.localpkg = local;
	if (ret == EPKG_END)
		extract = false;
	else if (ret != EPKG_OK) {
		retcode = ret;
		goto cleanup;
	}
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
	context.rootfd = pkg->rootfd;
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
			if (!pkgdb_file_exists(db->db, f->path)) {
				f->previous = PKG_FILE_SAVE;
			}
		}
	}

	/*
	 * Register the package before installing it in case there are problems
	 * that could be caught here.
	 */
	retcode = pkgdb_register_pkg(db->db, pkg, flags & PKG_ADD_FORCE, NULL);
	if (retcode != EPKG_OK)
		goto cleanup;
	openxact = true;

	/*
	 * Execute pre-install scripts
	 */
	if ((flags & PKG_ADD_NOSCRIPT) == 0) {
		if ((retcode = pkg_lua_script_run(pkg, PKG_LUA_PRE_INSTALL, (local != NULL))) != EPKG_OK)
			goto cleanup;
		if ((retcode = pkg_script_run(pkg, PKG_SCRIPT_PRE_INSTALL, (local != NULL),
			    ((flags & PKG_ADD_NOEXEC) == PKG_ADD_NOEXEC))) != EPKG_OK)
			goto cleanup;
	}


	/* add the user and group if necessary */

	nfiles = pkghash_count(pkg->filehash) + pkghash_count(pkg->dirhash);
	/*
	 * Extract the files on disk.
	 */
	if (extract) {
		pkg_register_cleanup_callback(pkg_rollback_cb, pkg);
		retcode = do_extract(a, ae, nfiles, local, &tempdirs, &context);
		pkg_unregister_cleanup_callback(pkg_rollback_cb, pkg);
		if (retcode != EPKG_OK) {
			/* If the add failed, clean up (silently) */
			pkg_rollback_pkg(pkg);
			pkg_delete_dirs(db->db, pkg, NULL);
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
		if (pkg_add_cleanup_old(db->db, local, pkg, t, flags) != EPKG_OK) {
			retcode = EPKG_FATAL;
			goto cleanup;
		}
	}


	/* Update configuration file content with db with newer versions */
	pkgdb_update_config_file_content(pkg, db->db->sqlite);

	retcode = pkg_extract_finalize(pkg, &tempdirs);

	pkgdb_register_finale(db->db, retcode, NULL);
	openxact = false;

	/*
	 * Execute post install scripts
	 */

	if (retcode != EPKG_OK)
		goto cleanup;
	if ((flags & PKG_ADD_NOSCRIPT) == 0) {
		bool noexec = ((flags & PKG_ADD_NOEXEC) == PKG_ADD_NOEXEC);
		pkg_lua_script_run(pkg, PKG_LUA_POST_INSTALL, (local != NULL));
		retcode = pkg_script_run(pkg, PKG_SCRIPT_POST_INSTALL, (local != NULL),
		    noexec);
		if (retcode != EPKG_OK && noexec)
			goto cleanup;
		retcode = EPKG_OK;
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

	vec_foreach(pkg->message, i) {
		msg = pkg->message.d[i];
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
	if (openxact)
		pkgdb_register_finale(db->db, retcode, NULL);
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
	struct pkg_add_db padb;
	int ret;

	memset(&padb, 0, sizeof(padb));
	padb.db = db;
	padb.local_scanned = false;
	padb.ignore_compat32 = false;
	padb.pkgbase = pkgdb_file_exists(db, "/usr/bin/uname");

	ret = pkg_add_common(&padb, path, flags, location, NULL, NULL, NULL);
	vec_free_and_free(&padb.localpkgs, pkg_free);
	vec_free_and_free(&padb.system_shlibs, free);
	return (ret);
}

int
pkg_add_from_remote(struct pkgdb *db, const char *path, unsigned flags,
    const char *location, struct pkg *rp, struct triggers *t)
{
	struct pkg_add_db padb;

	memset(&padb, 0, sizeof(padb));
	padb.db = db;
	padb.ignore_compat32 = false;
	padb.pkgbase = pkgdb_file_exists(db, "/usr/bin/uname");

	return pkg_add_common(&padb, path, flags, location, rp, NULL, t);
}

int
pkg_add_upgrade(struct pkgdb *db, const char *path, unsigned flags,
    const char *location,
    struct pkg *rp, struct pkg *lp, struct triggers *t)
{
	struct pkg_add_db padb;

	memset(&padb, 0, sizeof(padb));
	padb.db = db;
	padb.ignore_compat32 = false;
	padb.pkgbase = pkgdb_file_exists(db, "/usr/bin/uname");

	if (pkgdb_ensure_loaded(db, lp,
	    PKG_LOAD_FILES|PKG_LOAD_SCRIPTS|PKG_LOAD_DIRS|PKG_LOAD_LUA_SCRIPTS) != EPKG_OK)
		return (EPKG_FATAL);

	return pkg_add_common(&padb, path, flags, location, rp, lp, t);
}

static int
pkg_group_dump(int fd, struct pkg *pkg)
{
	ucl_object_t *o, *seq;
	struct pkg_dep	*dep = NULL;

	if (pkg->type != PKG_GROUP_REMOTE)
		return (EPKG_FATAL);
	o = ucl_object_typed_new(UCL_OBJECT);
	ucl_object_insert_key(o, ucl_object_fromstring(pkg->name), "name", 0, false);
	ucl_object_insert_key(o, ucl_object_fromstring(pkg->comment), "comment", 0, false);
	seq = ucl_object_typed_new(UCL_ARRAY);
	while (pkg_deps(pkg, &dep) == EPKG_OK)
		ucl_array_append(seq, ucl_object_fromstring(dep->name));
	ucl_object_insert_key(o, seq, "depends", 0, false);
	ucl_object_emit_fd(o, UCL_EMIT_CONFIG, fd);
	return (EPKG_OK);
}

int
pkg_add_group(struct pkg *pkg)
{
	char temp[MAXPATHLEN];
	int dfd = pkg_get_dbdirfd();
	mkdirat(dfd, "groups", 0755);
	int gfd = openat(dfd, "groups", O_DIRECTORY|O_CLOEXEC);
	hidden_tempfile(temp, MAXPATHLEN, pkg->name);
	int fd = openat(gfd, temp, O_CREAT|O_EXCL|O_WRONLY, 0644);
	if (fd == -1) {
		pkg_emit_errno("impossible to create group file %s", pkg->name);
		return (EPKG_FATAL);
	}
	pkg_group_dump(fd, pkg);
	close(fd);
	if (renameat(gfd, temp, gfd, pkg->name) == -1) {
		unlinkat(gfd, temp, 0);
		pkg_emit_errno("impossible to create group file %s", pkg->name);
		return (EPKG_FATAL);
	}
	return (EPKG_OK);
}

int
pkg_add_fromdir(struct pkg *pkg, const char *src, struct pkgdb *db __unused)
{
	struct stat st;
	struct pkg_dir *d = NULL;
	struct pkg_file *f = NULL;
	char target[MAXPATHLEN];
	struct passwd *pw, pwent;
	struct group *gr, grent;
	int err, fd, fromfd;
	int retcode;
	hardlinks_t hardlinks = vec_init();
	const char *path;
	char buffer[1024];
	size_t link_len;
	bool install_as_user;
	tempdirs_t tempdirs = vec_init();
	struct pkg_add_context context;

	memset(&context, 0, sizeof(context));

	install_as_user = (getenv("INSTALL_AS_USER") != NULL);

	fromfd = open(src, O_DIRECTORY);
	if (fromfd == -1) {
		pkg_fatal_errno("Unable to open source directory '%s'", src);
	}
	pkg_open_root_fd(pkg);
	context.pkg = pkg;
	context.rootfd = pkg->rootfd;

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

		if (create_dir(&context, d, &tempdirs) == EPKG_FATAL) {
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
			vec_free_and_free(&hardlinks, free);
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
				vec_free_and_free(&hardlinks, free);
				close(fromfd);
				pkg_fatal_errno("Impossible to read symlinks "
				    "'%s'", f->path);
			}
			target[link_len] = '\0';
			if (create_symlinks(&context, f, target, &tempdirs) == EPKG_FATAL) {
				retcode = EPKG_FATAL;
				goto cleanup;
			}
		} else if (S_ISREG(st.st_mode)) {
			if ((fd = openat(fromfd, RELATIVE_PATH(f->path),
			    O_RDONLY)) == -1) {
				vec_free_and_free(&hardlinks, free);
				close(fromfd);
				pkg_fatal_errno("Impossible to open source file"
				    " '%s'", RELATIVE_PATH(f->path));
			}
			path = NULL;
			vec_foreach(hardlinks, i) {
				struct hardlink *hit = hardlinks.d[i];
				if (hit->ino == st.st_ino &&
				    hit->dev == st.st_dev) {
					path = hit->path;
					break;
				}
			}
			if (path != NULL) {
				if (create_hardlink(&context, f, path, &tempdirs) == EPKG_FATAL) {
					close(fd);
					retcode = EPKG_FATAL;
					goto cleanup;
				}
			} else {
				if (create_regfile(&context, f, NULL, NULL, fd, NULL, &tempdirs) == EPKG_FATAL) {
					close(fd);
					retcode = EPKG_FATAL;
					goto cleanup;
				}
				struct hardlink *h = xcalloc(1, sizeof(*h));
				h->ino = st.st_ino;
				h->dev = st.st_dev;
				h->path = f->path;
				vec_push(&hardlinks, h);
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
	vec_free_and_free(&hardlinks, free);
	close(fromfd);
	return (retcode);
}

/*static bool
belong_to_self(struct pkg_add_context *context, const char *path)
{
	struct pkgdb_it *it = NULL;
	struct pkg *p = NULL;
	if (context->db != NULL && (it = pkgdb_query_which(context->db, path, false)) != NULL) {
		if (pkgdb_it_next(it, &p, PKG_LOAD_BASIC) != EPKG_OK) {
			pkgdb_it_free(it);
			fprintf(stderr, "mais non\n");
			return (false);
		}
		pkgdb_it_free(it);
		if (STREQ(p->uid, context->pkg->uid)) {
			pkg_free(p);
			fprintf(stderr, "mais oui\n");
			return (true);
		}
		pkg_free(p);
	}
	fprintf(stderr, "mais nope\n");
	return (false);
}*/

struct tempdir *
open_tempdir(struct pkg_add_context *context, const char *path)
{
	struct stat st;
	char walk[MAXPATHLEN];
	char *dir;
	size_t cnt = 0, len;
	struct tempdir *t;

	strlcpy(walk, path, sizeof(walk));
	while ((dir = strrchr(walk, '/')) != NULL) {
		*dir = '\0';
		cnt++;
		/* accept symlinks pointing to directories */
		len = strlen(walk);
		if (len == 0 && cnt == 1)
			break;
		if (len > 0) {
			int flag = AT_SYMLINK_NOFOLLOW;
			if (context->localpkg == NULL)
				flag = 0;
			if (fstatat(context->rootfd, RELATIVE_PATH(walk), &st, flag) == -1)
				continue;
			if (S_ISLNK(st.st_mode) && context->localpkg != NULL && pkghash_get(context->localpkg->filehash, walk) == NULL) {
				if (fstatat(context->rootfd, RELATIVE_PATH(walk), &st, 0) == -1)
					continue;
			}
			if (S_ISDIR(st.st_mode) && cnt == 1)
				break;
			if (!S_ISDIR(st.st_mode))
				continue;
		}
		*dir = '/';
		t = xcalloc(1, sizeof(*t));
		hidden_tempfile(t->temp, sizeof(t->temp), walk);
		if (mkdirat(context->rootfd, RELATIVE_PATH(t->temp), 0755) == -1) {
			pkg_errno("Fail to create temporary directory: %s", t->temp);
			free(t);
			return (NULL);
		}

		strlcpy(t->name, walk, sizeof(t->name));
		t->len = strlen(t->name);
		t->fd = openat(context->rootfd, RELATIVE_PATH(t->temp), O_DIRECTORY);
		if (t->fd == -1) {
			pkg_errno("Fail to open directory %s", t->temp);
			free(t);
			return (NULL);
		}
		return (t);
	}
	errno = 0;
	return (NULL);
}
