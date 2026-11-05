/*-
 * SPDX-License-Identifier: LicenseRef-scancode-bsd-unchanged
 *
 * Copyright (c) 2011-2020 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2013 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2023 Serenity Cyber Security, LLC
 *                    Author: Gleb Popov <arrowd@FreeBSD.org>
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

#include <pkg_config.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/param.h>
#if __has_include(<sys/procctl.h>)
#include <sys/procctl.h>
#endif
#include <stdio.h>

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <ucl.h>
#include <ctype.h>
#include <fnmatch.h>
#include <paths.h>
#include <float.h>
#include <math.h>
#include <regex.h>
#include <pwd.h>
#include <grp.h>

#include <bsd_compat.h>

#include "pkg.h"
#include "pkg/vec.h"
#include "private/event.h"
#include "private/pkg_abi.h"
#include "private/utils.h"
#include "private/pkg.h"
#include "xmalloc.h"

extern struct pkg_ctx ctx;

bool
match_ucl_lists(const char *buf, const ucl_object_t *globs, const ucl_object_t *regexes)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it;

	if (globs == NULL && regexes == NULL)
		return (false);

	if (globs != NULL) {
		it = NULL;
		while ((cur = ucl_iterate_object(globs, &it, true))) {
			if (fnmatch(ucl_object_tostring(cur), buf, 0) == 0)
				return (true);
		}
	}

	if (regexes != NULL) {
		it = NULL;
		while ((cur = ucl_iterate_object(regexes, &it, true))) {
			regex_t re;
			regcomp(&re, ucl_object_tostring(cur),
			   REG_EXTENDED|REG_NOSUB);
			if (regexec(&re, buf, 0, NULL, 0) == 0) {
				regfree(&re);
				return (true);
			}
			regfree(&re);
		}
	}

	return (false);
}

/* Check if two absolute directory paths are equal, collapsing consecutive
 * path separators and ignoring trailing path separators. */
static bool
dir_paths_equal(const char *a, const char *b)
{
	assert(a != NULL);
	assert(b != NULL);
	assert(*a == '/');
	assert(*b == '/');

	while (*a == *b) {
		if (*a == '\0') {
			return (true);
		}

		/* Skip over consecutive path separators */
		if (*a == '/') {
			while (*a == '/') a++;
			while (*b == '/') b++;
		} else {
			a++;
			b++;
		}
	}

	/* There may be trailing path separators on one path but not the other */
	while (*a == '/') a++;
	while (*b == '/') b++;

	return (*a == *b);
}

/*
 * Given a ucl list of directory paths, check if the file is in one of the
 * directories in the list (subdirectories not included).
 *
 * Asserts that file is an absolute path that does not end in /. */
bool
pkg_match_paths_list(const ucl_object_t *paths, const char *file)
{
	assert(file != NULL);
	assert(file[0] == '/');

	char *copy = xstrdup(file);
	char *final_slash = strrchr(copy, '/');
	assert(final_slash != NULL);
	assert(*(final_slash + 1) != '\0');
	if (final_slash == copy) {
		*(final_slash + 1) = '\0';
	} else {
		*final_slash = '\0';
	}
	const char *dirname = copy;

	bool found = false;
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	while ((cur = ucl_object_iterate(paths, &it, true))) {
		if (dir_paths_equal(dirname, ucl_object_tostring(cur))) {
			found = true;
			break;
		}
	}

	free(copy);

	return (found);
}

int
pkg_mkdirs(const char *_path)
{
	char path[MAXPATHLEN];
	char *p;
	int dirfd;

	dirfd = open(_path, O_RDONLY|O_DIRECTORY);
	if (dirfd >= 0) {
		close(dirfd);
		return EPKG_OK;
	}

	strlcpy(path, _path, sizeof(path));
	p = path;
	while (*p == '/')
		p++;

	for (;;) {
		if ((p = strchr(p, '/')) != NULL)
			*p = '\0';

		if (mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO) < 0)
			if (errno != EEXIST && errno != EISDIR) {
				pkg_emit_errno("mkdir", path);
				return (EPKG_FATAL);
			}

		/* that was the last element of the path */
		if (p == NULL)
			break;

		*p = '/';
		p++;
	}

	return (EPKG_OK);
}
int
file_to_bufferat(int dfd, const char *path, char **buffer, off_t *sz)
{
	int fd = -1;
	struct stat st;
	int retcode = EPKG_OK;

	assert(path != NULL && path[0] != '\0');
	assert(buffer != NULL);
	assert(sz != NULL);

	if ((fd = openat(dfd, path, O_RDONLY)) == -1) {
		pkg_emit_errno("openat", path);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (fstat(fd, &st) == -1) {
		pkg_emit_errno("fstatat", path);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	*buffer = xmalloc(st.st_size + 1);

	if (read(fd, *buffer, st.st_size) == -1) {
		pkg_emit_errno("read", path);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

cleanup:
	if (fd >= 0)
		close(fd);

	if (retcode == EPKG_OK) {
		(*buffer)[st.st_size] = '\0';
		*sz = st.st_size;
	} else {
		free(*buffer);
		*buffer = NULL;
		*sz = -1;
	}
	return (retcode);
}

int
file_to_buffer(const char *path, char **buffer, off_t *sz)
{
	return file_to_bufferat(AT_FDCWD, path, buffer, sz);
}

int
format_exec_cmd(char **dest, const char *in, const char *prefix,
    const char *plist_file, const char *line, int argc, char **argv, bool lua)
{
	sb_t buf = sb_init();
	char path[MAXPATHLEN];
	char *cp;
	const char *ptr;
	size_t sz;

	cp = NULL;

	if (line != NULL && argv != NULL) {
		if (lua) {
			sb_printf(&buf, "-- args: %s\n", line);
		} else {
			sb_printf(&buf, "# args: %s\n", line);
		}
	}

	while (in[0] != '\0') {
		if (in[0] != '%') {
			sb_cat_c(&buf, in[0]);
			in++;
			continue;
		}
		in++;
		switch(in[0]) {
		case 'D':
			sb_printf(&buf, "%s", prefix);
			break;
		case 'F':
			if (plist_file == NULL || plist_file[0] == '\0') {
				pkg_emit_error("No files defined %%F couldn't "
				    "be expanded, ignoring %s", in);
				sb_fini(&buf);
				return (EPKG_FATAL);
			}
			sb_printf(&buf, "%s", plist_file);
			break;
		case 'f':
			if (plist_file == NULL || plist_file[0] == '\0') {
				pkg_emit_error("No files defined %%f couldn't "
				    "be expanded, ignoring %s", in);
				sb_fini(&buf);
				return (EPKG_FATAL);
			}
			ptr = strrchr(plist_file, '/');
			if (ptr != NULL)
				ptr++;
			else
				ptr = plist_file;
			sb_printf(&buf, "%s", ptr);
			break;
		case 'B':
			if (plist_file == NULL || plist_file[0] == '\0') {
				pkg_emit_error("No files defined %%B couldn't "
				    "be expanded, ignoring %s", in);
				sb_fini(&buf);
				return (EPKG_FATAL);
			}
			if (prefix[strlen(prefix) - 1] == '/')
				snprintf(path, sizeof(path), "%s%s", prefix,
				    plist_file);
			else
				snprintf(path, sizeof(path), "%s/%s", prefix,
				    plist_file);
			cp = strrchr(path, '/');
			cp[0] = '\0';
			sb_printf(&buf, "%s", path);
			break;
		case '%':
			sb_cat_c(&buf, '%');
			break;
		case '@':
			if (line != NULL) {
				sb_printf(&buf, "%s", line);
				break;
			}

			/*
			 * no break here because if line is not
			 * given (default exec) %@ does not
			 * exists
			 */
			/* FALLTHRU */
		case '#':
			sb_printf(&buf, "%d", argc);
			break;
		default:
			if ((sz = strspn(in, "0123456789")) > 0) {
				int pos = strtol(in, NULL, 10);
				if (pos > argc) {
					pkg_emit_error("Requesting argument "
					    "%%%d while only %d arguments are"
					    " available", pos, argc);
					sb_fini(&buf);
					return (EPKG_FATAL);
				}
				sb_printf(&buf, "%s", argv[pos -1]);
				in += sz -1;
				break;
			}
			sb_printf(&buf, "%c%c", '%', in[0]);
			break;
		}

		in++;
	}

	*dest = sb_get(&buf);

	return (EPKG_OK);
}

int
is_dir(const char *path)
{
	struct stat st;

	return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

int
is_link(const char *path)
{
	struct stat st;

	return (lstat(path, &st) == 0 && S_ISLNK(st.st_mode));
}

bool
check_for_hardlink(hardlinks_t *hl, struct stat *st)
{
	struct hardlink *h;

	vec_foreach(*hl, i) {
		h = hl->d[i];
		if (h->ino == st->st_ino &&
		    h->dev == st->st_dev)
			return (true);
	}
	h = xcalloc(1, sizeof(*h));
	h->ino = st->st_ino;
	h->dev = st->st_dev;
	vec_push(hl, h);

	return (false);
}

/*
 * ABI validation:
 * - lowest match (case insensitive)
 * - glob matching
 *
 * A package which is valid for installation on any FreeBSD now simply has
 * to define itself as: abi: "FreeBSD" or "FreeBSD:*"
 * A package which is valid for installation on any FreeBSD 15 regardless of
 * the arch
 * abi: "FreeBSD:15" or "FreeBSD:15:*"
 *
 */

bool
is_valid_abi(const char *testabi, bool emit_error)
{
	const char *abi = pkg_object_string(pkg_config_get("ABI"));

	if (strncasecmp(testabi, abi, strlen(testabi)) != 0 &&
	    fnmatch(testabi, abi, FNM_CASEFOLD) == FNM_NOMATCH) {
		if (emit_error)
			pkg_emit_error("wrong architecture: %s instead of %s",
			    testabi, abi);
		return (false);
	}

	return (true);
}

bool
is_valid_os_version(struct pkg *pkg)
{
	if (ctx.abi.os != PKG_OS_FREEBSD)
		return (true);
	const char *fbsd_version;
	const char *errstr = NULL;
	char query_buf[512];
	/* -1: not checked, 0: not allowed, 1: allowed */
	static int osver_mismatch_allowed = -1;
	bool ret;

	if (pkg_object_bool(pkg_config_get("IGNORE_OSVERSION")))
		return (true);
	if ((fbsd_version = pkg_kv_get(&pkg->annotations, "FreeBSD_version")) != NULL) {
		int pkg_osversion = strtonum(fbsd_version, 1, INT_MAX, &errstr);
		if (errstr != NULL) {
			pkg_emit_error("Invalid FreeBSD version %s for package %s",
			    fbsd_version, pkg->name);
			return (false);
		}
		int abi_osversion = pkg_abi_get_freebsd_osversion(&ctx.abi);
		if (pkg_osversion > abi_osversion) {
			if (pkg_osversion - abi_osversion < 100000) {
				/* Negligible difference, ask user to enforce */
				if (osver_mismatch_allowed == -1) {
					snprintf(query_buf, sizeof(query_buf),
							"Newer FreeBSD version for package %s:\n"
							"To ignore this error set IGNORE_OSVERSION=yes\n"
							"- package: %d\n"
							"- running userland: %d\n"
							"Ignore the mismatch and continue? ", pkg->name,
							pkg_osversion, abi_osversion);
					ret = pkg_emit_query_yesno(false, query_buf);
					osver_mismatch_allowed = ret;
				}

				return (osver_mismatch_allowed);
			}
			else {
				pkg_emit_error("Newer FreeBSD version for package %s:\n"
					"To ignore this error set IGNORE_OSVERSION=yes\n"
					"- package: %d\n"
					"- running kernel: %d\n",
					pkg->name,
					pkg_osversion, abi_osversion);
				return (false);
			}
		}
	}
	return (true);
}

void
set_nonblocking(int fd)
{
	int flags;

	if ((flags = fcntl(fd, F_GETFL)) == -1)
		return;
	if (!(flags & O_NONBLOCK)) {
		flags |= O_NONBLOCK;
		fcntl(fd, F_SETFL, flags);
	}
}

void
set_blocking(int fd)
{
	int flags;

	if ((flags = fcntl(fd, F_GETFL)) == -1)
		return;
	if (flags & O_NONBLOCK) {
		flags &= ~O_NONBLOCK;
		fcntl(fd, F_SETFL, flags);
	}
}

/* Spawn a process from pfunc, returning it's pid. The fds array passed will
 * be filled with two descriptors: fds[0] will read from the child process,
 * and fds[1] will write to it.
 * Similarly, the child process will receive a reading/writing fd set (in
 * that same order) as arguments.
*/
extern char **environ;

#if defined(__linux__) || defined(__illumos__) || defined(__sun)

static void
pkg_closefrom_proc(int lowfd)
{
	DIR *dirp;
	struct dirent *dent;
	char *endp;
	long fd;

	dirp = opendir("/proc/self/fd");
	if (dirp == NULL)
		return;
	while ((dent = readdir(dirp)) != NULL) {
		fd = strtol(dent->d_name, &endp, 10);
		if (dent->d_name != endp && *endp == '\0' &&
		    fd >= 0 && fd < INT_MAX && fd >= lowfd && fd != dirfd(dirp))
			(void) close((int) fd);
	}
	(void) closedir(dirp);
}

#endif	/* __linux__ || __illumos__ || __sun */

void
pkg_closefrom(int lowfd)
{
#if defined(__linux__) || defined(__illumos__) || defined(__sun)
	pkg_closefrom_proc(lowfd);
#elif defined(HAVE_CLOSEFROM)
	closefrom(lowfd);
#else
	long maxfd = sysconf(_SC_OPEN_MAX);
	long fd;

	if (maxfd < 0)
		maxfd = 256;
	for (fd = lowfd; fd < maxfd; fd++)
		(void) close((int) fd);
#endif
}

pid_t
process_spawn_pipe(FILE *inout[2], const char *command)
{
	pid_t pid;
	int pipes[4];
	char *argv[4];

	/* Parent read/child write pipe */
	if (pipe(&pipes[0]) == -1)
		return (-1);

	/* Child read/parent write pipe */
	if (pipe(&pipes[2]) == -1) {
		close(pipes[0]);
		close(pipes[1]);
		return (-1);
	}

	argv[0] = __DECONST(char *, "sh");
	argv[1] = __DECONST(char *, "-c");
	argv[2] = __DECONST(char *, command);
	argv[3] = NULL;

	pid = fork();
	if (pid > 0) {
		/* Parent process */
		inout[0] = fdopen(pipes[0], "r");
		inout[1] = fdopen(pipes[3], "w");

		close(pipes[1]);
		close(pipes[2]);

		return (pid);

	} else if (pid == 0) {
		close(pipes[0]);
		close(pipes[3]);

		if (pipes[1] != STDOUT_FILENO) {
			dup2(pipes[1], STDOUT_FILENO);
			close(pipes[1]);
		}
		if (pipes[2] != STDIN_FILENO) {
			dup2(pipes[2], STDIN_FILENO);
			close(pipes[2]);
		}
		pkg_closefrom(STDERR_FILENO + 1);

		execve(_PATH_BSHELL, argv, environ);

		_exit(127);
	}

	return (-1); /* ? */
}

static int
ucl_buf_append_character(unsigned char c, size_t len, void *data)
{
	sb_t *buf = data;
	size_t i;

	for (i = 0; i < len; i++)
		sb_printf(buf, "%c", c);

	return (0);
}

static int
ucl_buf_append_len(const unsigned char *str, size_t len, void *data)
{
	sb_t *buf = data;

	sb_printf(buf, "%.*s", (int)len, str);

	return (0);
}

static int
ucl_buf_append_int(int64_t val, void *data)
{
	sb_t *buf = data;

	sb_printf(buf, "%"PRId64, val);

	return (0);
}

static int
ucl_buf_append_double(double val, void *data)
{
	sb_t *buf = data;
	const double delta = 0.0000001;

	if (val == (double)(int)val) {
		sb_printf(buf, "%.1lf", val);
	} else if (fabs(val - (double)(int)val) < delta) {
		sb_printf(buf, "%.*lg", DBL_DIG, val);
	} else {
		sb_printf(buf, "%lf", val);
	}

	return (0);
}

bool
ucl_object_emit_file(const ucl_object_t *obj, enum ucl_emitter emit_type,
    FILE *out)
{
	struct ucl_emitter_functions *f = ucl_object_emit_file_funcs(out);
	bool ret = false;

	if (obj == NULL)
		return (false);

	ret = ucl_object_emit_full(obj, emit_type, f, NULL);
	ucl_object_emit_funcs_free(f);

	return (ret);
}

bool
ucl_object_emit_fd(const ucl_object_t *obj, enum ucl_emitter emit_type, int fd)
{
	struct ucl_emitter_functions *f = ucl_object_emit_fd_funcs(fd);
	bool ret = false;

	if (obj == NULL)
		return (false);
	ret = ucl_object_emit_full(obj, emit_type, f, NULL);
	ucl_object_emit_funcs_free(f);

	return (ret);
}


bool
ucl_object_emit_buf(const ucl_object_t *obj, enum ucl_emitter emit_type,
                     sb_t **buf)
{
	bool ret = false;
	struct ucl_emitter_functions func = {
		.ucl_emitter_append_character = ucl_buf_append_character,
		.ucl_emitter_append_len = ucl_buf_append_len,
		.ucl_emitter_append_int = ucl_buf_append_int,
		.ucl_emitter_append_double = ucl_buf_append_double
	};

	sb_reset(*buf);

	func.ud = *buf;

	ret = ucl_object_emit_full(obj, emit_type, &func, NULL);

	return (ret);
}

/* A bit like strsep(), except it accounts for "double" and 'single'
   quotes.  Unlike strsep(), returns the next arg string, trimmed of
   whitespace or enclosing quotes, and updates **args to point at the
   character after that.  Sets *args to NULL when it has been
   completely consumed.  Quoted strings run from the first encountered
   quotemark to the next one of the same type or the terminating NULL.
   Quoted strings can contain the /other/ type of quote mark, which
   loses any special significance.  There isn't an escape
   character. */

enum parse_states {
	START,
	ORDINARY_TEXT,
	OPEN_SINGLE_QUOTES,
	IN_SINGLE_QUOTES,
	OPEN_DOUBLE_QUOTES,
	IN_DOUBLE_QUOTES,
};

char *
pkg_utils_tokenize(char **args)
{
	char			*p, *p_start;
	enum parse_states	 parse_state = START;

	assert(*args != NULL);

	for (p = p_start = *args; *p != '\0'; p++) {
		switch (parse_state) {
		case START:
			if (!isspace(*p)) {
				if (*p == '"')
					parse_state = OPEN_DOUBLE_QUOTES;
				else if (*p == '\'')
					parse_state = OPEN_SINGLE_QUOTES;
				else {
					parse_state = ORDINARY_TEXT;
					p_start = p;
				}
			} else
				p_start = p;
			break;
		case ORDINARY_TEXT:
			if (isspace(*p))
				goto finish;
			break;
		case OPEN_SINGLE_QUOTES:
			p_start = p;
			if (*p == '\'')
				goto finish;

			parse_state = IN_SINGLE_QUOTES;
			break;
		case IN_SINGLE_QUOTES:
			if (*p == '\'')
				goto finish;
			break;
		case OPEN_DOUBLE_QUOTES:
			p_start = p;
			if (*p == '"')
				goto finish;
			parse_state = IN_DOUBLE_QUOTES;
			break;
		case IN_DOUBLE_QUOTES:
			if (*p == '"')
				goto finish;
			break;
		}
	}

finish:
	if (*p == '\0')
		*args = NULL;	/* All done */
	else {
		*p = '\0';
		p++;
		if (*p == '\0' || parse_state == START)
			*args = NULL; /* whitespace or nothing left */
		else
			*args = p;
	}
	return (p_start);
}

/*
 * Callers use this to size the array filled by pkg_utils_tokenize(), which
 * returns at most one token more than this count.  A closing quote also ends
 * a token ("a""b" gives two), so quotes are counted along with spaces.
 */
int
pkg_utils_count_spaces(const char *args)
{
	int		spaces;
	const char	*p;

	for (spaces = 0, p = args; *p != '\0'; p++)
		if (isspace(*p) || *p == '"' || *p == '\'')
			spaces++;

	return (spaces);
}

/* unlike realpath(3), this routine does not expand symbolic links */
char *
pkg_absolutepath(const char *src, char *dest, size_t dest_size, bool fromroot) {
	size_t dest_len, src_len, cur_len;
	const char *cur, *next;

	src_len = strlen(src);
	memset(dest, '\0', dest_size);

	if (src_len != 0 && src[0] != '/') {
		if (fromroot)
			*dest = '/';
		/* relative path, we use cwd */
		else if (getcwd(dest, dest_size) == NULL)
			return (NULL);
	}
	dest_len = strlen(dest);

	for (cur = next = src; next != NULL; cur = (next == NULL) ? NULL : next + 1) {
		next = strchr(cur, '/');
		if (next != NULL)
			cur_len = next - cur;
		else
			cur_len = strlen(cur);

		/* check for special cases "", "." and ".." */
		if (cur_len == 0)
			continue;
		else if (cur_len == 1 && cur[0] == '.')
			continue;
		else if (cur_len == 2 && cur[0] == '.' && cur[1] == '.') {
			const char *slash = strrchr(dest, '/');
			if (slash != NULL) {
				dest_len = slash - dest;
				dest[dest_len] = '\0';
			}
			continue;
		}

		if (dest_len + 1 + cur_len >= dest_size)
			return (NULL);
		dest[dest_len++] = '/';
		(void)memcpy(dest + dest_len, cur, cur_len);
		dest_len += cur_len;
		dest[dest_len] = '\0';
	}

	if (dest_len == 0) {
		if (strlcpy(dest, "/", dest_size) >= dest_size)
			return (NULL);
	}

	return (dest);
}

bool
mkdirat_p(int fd, const char *path)
{
	const char *next;
	char pathdone[MAXPATHLEN], walkbuf[MAXPATHLEN], *walk;

	pathdone[0] = '\0';
	strlcpy(walkbuf, path, sizeof(walkbuf));
	walk = walkbuf;
	while ((next = strsep(&walk, "/")) != NULL) {
		if (*next == '\0')
			continue;
		strlcat(pathdone, next, sizeof(pathdone));
		if (mkdirat(fd, pathdone, 0755) == -1) {
			if (errno == EEXIST) {
				strlcat(pathdone, "/", sizeof(pathdone));
				continue;
			}
			pkg_errno("Failed to create /%s", pathdone);
			return (false);
		}
		strlcat(pathdone, "/", sizeof(pathdone));
	}
	return (true);
}

/*
 * Join 'name' to 'dir' into 'dest'.
 */
static bool
path_join(char *dest, size_t destlen, const char *dir, const char *name)
{
	int n;

	if (dir[0] == '\0')
		n = snprintf(dest, destlen, "%s", name);
	else
		n = snprintf(dest, destlen, "%s/%s", dir, name);

	return (n >= 0 && (size_t)n < destlen);
}

/*
 * Drop the last component of a relative path, staying at the root when
 * there is none left.
 */
/*
 * Store in 'dest' the resolved 'rel' path, keeping the form of the path the
 * caller gave: absolute when it was absolute, relative otherwise.
 */
static bool
store_resolved(char *dest, size_t destlen, const char *rel, bool absolute)
{
	int n;

	if (absolute)
		n = snprintf(dest, destlen, "/%s", rel);
	else
		n = snprintf(dest, destlen, "%s", rel);

	return (n >= 0 && (size_t)n < destlen);
}

static void
path_pop(char *path)
{
	char *slash = strrchr(path, '/');

	if (slash == NULL)
		path[0] = '\0';
	else
		*slash = '\0';
}

/*
 * Number of directory levels below the root a relative path ends up at,
 * resolving "." and ".." lexically.  -1 when it climbs above the root.
 */
static int
relative_depth(const char *path, int depth)
{
	const char *p, *next;
	size_t clen;

	for (p = path; *p != '\0'; p = next + 1) {
		next = strchr(p, '/');
		if (next == NULL)
			next = p + strlen(p);
		clen = next - p;
		if (clen == 2 && p[0] == '.' && p[1] == '.') {
			if (--depth < 0)
				return (-1);
		} else if (clen > 0 && !(clen == 1 && p[0] == '.')) {
			depth++;
		}
		if (*next == '\0')
			break;
	}
	return (depth);
}

/*
 * Resolve the parents of 'path' as if the directory 'rootfd' were the root
 * of the file system, so that resolving the result never leaves it: an
 * absolute symlink target, or one climbing above the root with "..", is
 * re-rooted at 'rootfd' instead of being resolved from the process root.
 * A target which already names a path inside of the root, as pkg itself
 * writes them, is used as is.
 *
 * Only the parents are resolved; the last component is left alone, the
 * caller protects it with O_NOFOLLOW or AT_SYMLINK_NOFOLLOW.
 *
 * 'dest' receives a path relative to 'rootfd'.  Nothing is rewritten when
 * no root directory is in use.
 */
int
pkg_resolve_beneath(int rootfd, const char *path, char *dest, size_t destlen)
{
	char		cur[MAXPATHLEN], todo[MAXPATHLEN], next[MAXPATHLEN];
	char		comp[NAME_MAX + 1], leaf[MAXPATHLEN];
	char		candidate[MAXPATHLEN], target[MAXPATHLEN];
	char		canon[MAXPATHLEN];
	const char	*root, *p, *end, *rest, *trel;
	struct stat	st;
	ssize_t		len;
	size_t		rootlen, clen;
	int		hops = 0;
	bool		absolute, expanded, rewritten;

	root = ctx.pkg_rootdir;
	if (rootfd < 0 || root == NULL || STREQ(root, "/")) {
		if (strlcpy(dest, path, destlen) >= destlen)
			return (EPKG_FATAL);
		return (EPKG_OK);
	}

	absolute = (path[0] == '/');

	rootlen = strlen(root);
	while (rootlen > 0 && root[rootlen - 1] == '/')
		rootlen--;

	if (strlcpy(todo, RELATIVE_PATH(path), sizeof(todo)) >= sizeof(todo))
		return (EPKG_FATAL);

	/* the last component is never resolved */
	end = strrchr(todo, '/');
	if (end == NULL)
		return (store_resolved(dest, destlen, todo, absolute) ?
		    EPKG_OK : EPKG_FATAL);
	if (strlcpy(leaf, end + 1, sizeof(leaf)) >= sizeof(leaf))
		return (EPKG_FATAL);
	todo[end - todo] = '\0';

	rewritten = false;
	cur[0] = '\0';
	for (;;) {
		expanded = false;
		for (p = todo; *p != '\0'; p = rest) {
			end = strchr(p, '/');
			clen = end != NULL ? (size_t)(end - p) : strlen(p);
			rest = end != NULL ? end + 1 : p + clen;

			if (clen == 0 || (clen == 1 && p[0] == '.'))
				continue;
			if (clen == 2 && p[0] == '.' && p[1] == '.') {
				if (cur[0] == '\0')
					rewritten = true;
				path_pop(cur);
				continue;
			}
			if (clen >= sizeof(comp))
				return (EPKG_FATAL);
			memcpy(comp, p, clen);
			comp[clen] = '\0';
			if (!path_join(candidate, sizeof(candidate), cur, comp))
				return (EPKG_FATAL);

			if (fstatat(rootfd, candidate, &st,
			    AT_SYMLINK_NOFOLLOW) == -1) {
				/*
				 * The rest does not exist yet: there is
				 * nothing left to resolve.
				 */
				if (*rest == '\0')
					strlcpy(cur, candidate, sizeof(cur));
				else if (!path_join(cur, sizeof(cur),
				    candidate, rest))
					return (EPKG_FATAL);
				todo[0] = '\0';
				break;
			}
			if (!S_ISLNK(st.st_mode)) {
				strlcpy(cur, candidate, sizeof(cur));
				continue;
			}
			if (++hops > 40) {
				errno = ELOOP;
				return (EPKG_FATAL);
			}
			len = readlinkat(rootfd, candidate, target,
			    sizeof(target) - 1);
			if (len <= 0)
				return (EPKG_FATAL);
			target[len] = '\0';
			if (target[0] == '/') {
				const char *tp = target;

				cur[0] = '\0';
				/*
				 * The root directory is canonical while a
				 * target naming a path inside of it may be
				 * spelled through a symlink: the two forms
				 * do not compare equal, look at both.
				 */
				if (root[0] == '/' && rootlen > 0) {
					if (realpath(target, canon) != NULL &&
					    strncmp(canon, root, rootlen) == 0 &&
					    (canon[rootlen] == '\0' ||
					    canon[rootlen] == '/'))
						tp = canon + rootlen;
					else if (strncmp(target, root, rootlen) == 0 &&
					    (target[rootlen] == '\0' ||
					    target[rootlen] == '/'))
						tp = target + rootlen;
				}
				trel = RELATIVE_PATH(tp);
				rewritten = true;
			} else {
				/*
				 * Relative to the directory holding the
				 * link.  It is expanded to keep inspecting
				 * the components it leads to, but it only
				 * rewrites the path when it climbs above
				 * the root: the kernel resolves the others
				 * correctly.
				 */
				trel = target;
				if (relative_depth(target,
				    relative_depth(cur, 0)) < 0)
					rewritten = true;
			}
			if (*rest == '\0') {
				if (strlcpy(next, trel, sizeof(next)) >=
				    sizeof(next))
					return (EPKG_FATAL);
			} else if (!path_join(next, sizeof(next), trel, rest))
				return (EPKG_FATAL);
			strlcpy(todo, next, sizeof(todo));
			expanded = true;
			break;
		}
		if (!expanded)
			break;
	}

	if (!rewritten) {
		if (strlcpy(dest, path, destlen) >= destlen)
			return (EPKG_FATAL);
		return (EPKG_OK);
	}
	if (!path_join(next, sizeof(next), cur, leaf))
		return (EPKG_FATAL);
	if (!store_resolved(dest, destlen, next, absolute))
		return (EPKG_FATAL);
	return (EPKG_OK);
}

/*
 * Recursively remove 'path' relative to 'basefd', never following
 * symlinks.  Errors are reported but do not stop the removal.
 */
void
pkg_rmtree_at(int basefd, const char *path)
{
	DIR *d;
	struct dirent *e;
	struct stat st;
	int dfd;

	path = RELATIVE_PATH(path);
	if (fstatat(basefd, path, &st, AT_SYMLINK_NOFOLLOW) != 0)
		return;
	if (S_ISDIR(st.st_mode)) {
		dfd = openat(basefd, path, O_DIRECTORY|O_CLOEXEC);
		if (dfd == -1) {
			pkg_emit_errno("openat", path);
			return;
		}
		d = fdopendir(dfd);
		if (d == NULL) {
			pkg_emit_errno("fdopendir", path);
			close(dfd);
			return;
		}
		while ((e = readdir(d)) != NULL) {
			if (STREQ(e->d_name, ".") || STREQ(e->d_name, ".."))
				continue;
			pkg_rmtree_at(dirfd(d), e->d_name);
		}
		closedir(d);
	}
	if (unlinkat(basefd, path, S_ISDIR(st.st_mode) ? AT_REMOVEDIR : 0) != 0)
		pkg_emit_errno("unlinkat", path);
}

bool
pkg_is_safe_relative_path(const char *path)
{
	const char *p;

	if (path == NULL || path[0] == '\0' || path[0] == '/')
		return (false);
	for (p = path; *p != '\0'; p++) {
		if (p[0] == '.' && p[1] == '.' &&
		    (p == path || p[-1] == '/') &&
		    (p[2] == '\0' || p[2] == '/'))
			return (false);
	}
	return (true);
}

int
pkg_namecmp(struct pkg *a, struct pkg *b)
{

	return (strcmp(a->name, b->name));
}

int
get_socketpair(int *pipe)
{
	int r;

#ifdef SOCK_SEQPACKET
	r = socketpair(AF_LOCAL, SOCK_SEQPACKET, 0, pipe);
	if (r == -1) {
		r = socketpair(AF_LOCAL, SOCK_DGRAM, 0, pipe);
	}
#else
	r = socketpair(AF_LOCAL, SOCK_DGRAM, 0, pipe);
#endif

	return (r);
}

/*
 * Modify the passed C-String by stripping off the last component delimited by '/'.
 * Return the string. Return a constant "." when passed NULL or the empty string.
 * FIXME: This routine corrupts memory when passed the empty string.
 * FIXME: This routine should propagate NULL.
 * TODO: Refactor at call sites.
 */
char *
get_dirname(char *d)
{
	char *walk;

	if (d == NULL)
		return (__DECONST(char *, "."));

	walk = strrchr(d, '/');
	if (walk == NULL) {
		d[0] = '.';
		d[1] = '\0';
	} else {
		*walk = '\0';
	}

	return (d);
}

char *
rtrimspace(char *buf)
{
	char *cp = buf + strlen(buf) -1;

	while (cp > buf && isspace(*cp)) {
		*cp = 0;
		cp --;
	}

	return (buf);
}

static ssize_t
_copy_file(int from, int to)
{
	char buf[BUFSIZ];
	ssize_t r, wresid, w = 0;
	char *bufp;

	r = read(from, buf, sizeof(buf));
	if (r < 0)
		return (r);
	for (bufp = buf, wresid = r; ; bufp += w, wresid -= w) {
		w = write(to, bufp, wresid);
		if (w <= 0)
			break;
		if (w >= (ssize_t)wresid)
			break;
	}
	return (w < 0 ? w : r);
}

bool
pkg_copy_file(int from, int to)
{
#ifdef HAVE_COPY_FILE_RANGE
	bool cfr = true;
#endif
	ssize_t r;

	do {
#ifdef HAVE_COPY_FILE_RANGE
		if (cfr) {
			r = copy_file_range(from, NULL, to, NULL, SSIZE_MAX, 0);
			if (r < 0 && (errno == EINVAL || errno == EXDEV)) {
				/* probably a non seekable FD */
				cfr = false;
			}
		}
		if (!cfr) {
#endif
			r = _copy_file(from, to);
#ifdef HAVE_COPY_FILE_RANGE
		}
#endif
	} while (r > 0);

	return (r >= 0);
}

static const unsigned char litchar[] =
"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/*
 * Uniform value in [0, max), used to build names from random characters.
 * The system generator is used where there is one, otherwise a generator
 * seeded once and serialised so that concurrent callers cannot share its
 * state.
 */
unsigned int
pkg_random_uniform(unsigned int max)
{
#ifdef HAVE_ARC4RANDOM
	return (arc4random_uniform(max));
#else
	static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
	static unsigned int seed;
	unsigned int r;

	if (max == 0)
		return (0);

	pthread_mutex_lock(&m);
	if (seed == 0)
		seed = (unsigned int)(time(NULL) ^ getpid());
	r = (unsigned int)rand_r(&seed) % max;
	pthread_mutex_unlock(&m);

	return (r);
#endif
}

void
append_random_suffix(char *buf, int buflen, int suflen)
{
	int nchars = strlen(buf);
	char *pos;

	/* 2 being the "." and the \0 */
	if (nchars + suflen > buflen - 2) {
		suflen = buflen - nchars - 2;
		if (suflen <= 0)
			return;
	}

	buf[nchars++] = '.';
	pos = buf + nchars;

	while(suflen --)
		*pos++ = litchar[pkg_random_uniform(sizeof(litchar) - 1)];

	*pos = '\0';
}

void
hidden_tempfile(char *buf, int buflen, const char *path)
{
	const char *fname;
	int suffixlen = 12;
	int nbuflen;
	const char *prefix = ".pkgtemp.";

	fname = strrchr(path, '/');
	if (fname != NULL)
		fname++;

	/*
	 * try to reduce the temporary name as much as possible to fit with very
	 * long file names if possible. by default
	 * .pkgtemp. fname . <suffix>
	 * otherwise
	 * . fname . <suffix>
	 * keep if suffix of at least 5 if possible
	 */
	if (fname != NULL) {
		if (strlen(fname) >= (NAME_MAX - 15))
			prefix = ".";
		snprintf(buf, buflen, "%.*s%s%s", (int)(fname - path), path, prefix, fname);
		nbuflen = buflen;
	} else {
		if (strlen(path) >= NAME_MAX - 15)
			prefix = ".";
		snprintf(buf, buflen, "%s%s", prefix, path);
		nbuflen = NAME_MAX;
	}


	append_random_suffix(buf, nbuflen, suffixlen);
}

char *
json_escape(const char *str)
{
	sb_t buf = sb_init();

	while (str != NULL && *str != '\0') {
		if (*str == '"' || *str == '\\')
			sb_cat_c(&buf, '\\');
		sb_cat_c(&buf, *str);
		str++;
	}

	return (sb_get(&buf));
}

const char *
get_http_auth(void)
{
	const char *str = getenv("HTTP_AUTH");
	if (str == NULL)
		return (NULL);
	if ((str = strchr(str, ':')) == NULL) {
		pkg_emit_error("malformed HTTP_AUTH");
		return (NULL);
	}
	if ((str = strchr(++str, ':')) == NULL) {
		pkg_emit_error("malformed HTTP_AUTH");
		return (NULL);
	}
	if (strchr(++str, ':') == NULL) {
		pkg_emit_error("malformed HTTP_AUTH");
		return (NULL);
	}
	return (str);
}

bool
charv_contains(charv_t *v, const char *el, bool casesensitive)
{
	vec_foreach(*v, i) {
		if (casesensitive) {
			if (STREQ(v->d[i], el))
				return (true);
		} else {
			if (STRIEQ(v->d[i], el)) {
				return (true);
			}
		}
	}
	return (false);
}

bool
c_charv_contains(c_charv_t *v, const char *el, bool casesensitive)
{
	vec_foreach(*v, i) {
		if (casesensitive) {
			if (STREQ(v->d[i], el))
				return (true);
		} else {
			if (STRIEQ(v->d[i], el)) {
				return (true);
			}
		}
	}
	return (false);
}

bool
str_ends_with(const char *str, const char *end)
{
	size_t el, sl;

	if (end == NULL)
		return (true);
	if (str == NULL)
		return (false);

	sl = strlen(str);
	el = strlen(end);
	if (sl < el)
		return (false);
	return (strncmp(str + (sl - el), end, el) == 0);
}

int
char_cmp(const void *a, const void *b) {
	return strcmp(*(char **)a, *(char **)b);
}

DEFINE_VEC_INSERT_SORTED_FUNC(charv_t, charv, char *, char_cmp);

const char *
charv_search(charv_t *v, const char *el)
{
	if (v->len == 0)
		return (NULL);
	const char **res = bsearch(&el, v->d, v->len, sizeof(char *), char_cmp);
	if (res == NULL)
		return (NULL);
	return *res;
}

static uid_t
lookup_uname(const char *uname)
{
	struct passwd pwent, *result = NULL;
	char *buf;
	size_t bufsize = 1024;
	int err = 0;
	uid_t uid = 0;

	for (;;) {
		buf = xcalloc(1, bufsize);
		err = getpwnam_r(uname, &pwent, buf, bufsize, &result);
		if (err != ERANGE)
			break;
		free(buf);
		bufsize *= 2;
	}
	if (err != 0)
		pkg_emit_error("getpwnam_r(%s): %s", uname, strerror(err));
	else if (result != NULL)
		uid = pwent.pw_uid;
	free(buf);

	return (uid);
}

uid_t
get_uid_from_uname(const char *uname, struct pkg_id_cache *cache)
{
	const char *testuname = uname ? uname : "";

	if (cache->uname[0] != '\0' && STREQ(testuname, cache->uname))
		return (cache->uid);

	cache->uname[0] = '\0';
	cache->uid = lookup_uname(testuname);
	/* a name too long for the cache is simply not cached */
	if (strlcpy(cache->uname, testuname, sizeof(cache->uname)) >=
	    sizeof(cache->uname))
		cache->uname[0] = '\0';

	return (cache->uid);
}

static gid_t
lookup_gname(const char *gname)
{
	struct group grent, *result = NULL;
	char *buf;
	size_t bufsize = 1024;
	int err = 0;
	gid_t gid = 0;

	for (;;) {
		buf = xcalloc(1, bufsize);
		err = getgrnam_r(gname, &grent, buf, bufsize, &result);
		if (err != ERANGE)
			break;
		free(buf);
		bufsize *= 2;
	}
	if (err != 0)
		pkg_emit_error("getgrnam_r(%s): %s", gname, strerror(err));
	else if (result != NULL)
		gid = grent.gr_gid;
	free(buf);

	return (gid);
}

gid_t
get_gid_from_gname(const char *gname, struct pkg_id_cache *cache)
{
	const char *testgname = gname ? gname : "";

	if (cache->gname[0] != '\0' && STREQ(testgname, cache->gname))
		return (cache->gid);

	cache->gname[0] = '\0';
	cache->gid = lookup_gname(testgname);
	/* a name too long for the cache is simply not cached */
	if (strlcpy(cache->gname, testgname, sizeof(cache->gname)) >=
	    sizeof(cache->gname))
		cache->gname[0] = '\0';

	return (cache->gid);
}

const char *
pkg_meta_attribute_tostring(enum pkg_meta_attribute attrib)
{
	switch (attrib) {
	case PKG_META_ATTR_TYPE: return "type";
	case PKG_META_ATTR_UNAME: return "uname";
	case PKG_META_ATTR_GNAME: return "gname";
	case PKG_META_ATTR_PERM: return "perm";
	case PKG_META_ATTR_FFLAGS: return "fflags";
	case PKG_META_ATTR_MTIME: return "mtime";
	case PKG_META_ATTR_SYMLINK: return "symlink";
	}

	return "???";
}

ucl_object_t *
ucl_parse_fd(int fd, const char *name)
{
	struct ucl_parser *p;
	ucl_object_t *obj;

	p = ucl_parser_new(0);
	if (!ucl_parser_add_fd(p, fd)) {
		pkg_emit_error("Error parsing '%s': %s",
		    name, ucl_parser_get_error(p));
		ucl_parser_free(p);
		return (NULL);
	}

	obj = ucl_parser_get_object(p);
	ucl_parser_free(p);

	return (obj);
}

ucl_object_t *
ucl_parse_buf(const char *buf, size_t len, const char *name)
{
	struct ucl_parser *p;
	ucl_object_t *obj;

	p = ucl_parser_new(UCL_PARSER_NO_FILEVARS);
	if (!ucl_parser_add_chunk(p, buf, len)) {
		pkg_emit_error("Error parsing '%s': %s",
		    name, ucl_parser_get_error(p));
		ucl_parser_free(p);
		return (NULL);
	}

	obj = ucl_parser_get_object(p);
	ucl_parser_free(p);

	return (obj);
}

void
pkg_reaper_acquire(struct pkg_reaper *r)
{
#ifdef PROC_REAP_KILL
	r->mypid = getpid();
	r->do_reap = procctl(P_PID, r->mypid, PROC_REAP_ACQUIRE, NULL) == 0;
#else
	r->mypid = getpid();
	r->do_reap = false;
#endif
}

void
pkg_reaper_release(struct pkg_reaper *r)
{
#ifdef PROC_REAP_KILL
	struct procctl_reaper_status info;
	struct procctl_reaper_kill killemall;

	if (!r->do_reap)
		return;

	memset(&info, 0, sizeof(info));
	memset(&killemall, 0, sizeof(killemall));

	procctl(P_PID, r->mypid, PROC_REAP_STATUS, &info);
	if (info.rs_children != 0) {
		killemall.rk_sig = SIGKILL;
		killemall.rk_flags = 0;
		if (procctl(P_PID, r->mypid, PROC_REAP_KILL, &killemall) != 0) {
			if (errno != ESRCH || killemall.rk_killed != 0) {
				pkg_errno("%s", "Failed to kill all processes");
			}
		}
	}
	procctl(P_PID, r->mypid, PROC_REAP_RELEASE, NULL);
#endif
}
