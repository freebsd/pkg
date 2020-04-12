/*-
 * Copyright (c) 2011-2016 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2013 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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
#include <stdio.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ucl.h>
#include <utlist.h>
#include <ctype.h>
#include <fnmatch.h>
#include <paths.h>
#include <float.h>
#include <math.h>

#include <bsd_compat.h>

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkg.h"
#include "xmalloc.h"

extern struct pkg_ctx ctx;

int
mkdirs(const char *_path)
{
	char path[MAXPATHLEN];
	char *p;

	strlcpy(path, _path, sizeof(path));
	p = path;
	if (*p == '/')
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

	if (fstatat(dfd, path, &st, 0) == -1) {
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
		*buffer = NULL;
		*sz = -1;
	}
	return (retcode);
}

int
file_to_buffer(const char *path, char **buffer, off_t *sz)
{
	int fd = -1;
	struct stat st;
	int retcode = EPKG_OK;

	assert(path != NULL && path[0] != '\0');
	assert(buffer != NULL);
	assert(sz != NULL);

	if ((fd = open(path, O_RDONLY)) == -1) {
		pkg_emit_errno("open", path);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (fstat(fd, &st) == -1) {
		pkg_emit_errno("fstat", path);
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
		*buffer = NULL;
		*sz = -1;
	}
	return (retcode);
}

int
format_exec_cmd(char **dest, const char *in, const char *prefix,
    const char *plist_file, char *line, int argc, char **argv)
{
	UT_string *buf;
	char path[MAXPATHLEN];
	char *cp;
	size_t sz;

	utstring_new(buf);

	while (in[0] != '\0') {
		if (in[0] != '%') {
			utstring_printf(buf, "%c", in[0]);
			in++;
			continue;
		}
		in++;
		switch(in[0]) {
		case 'D':
			utstring_printf(buf, "%s", prefix);
			break;
		case 'F':
			if (plist_file == NULL || plist_file[0] == '\0') {
				pkg_emit_error("No files defined %%F couldn't "
				    "be expanded, ignoring %s", in);
				utstring_free(buf);
				return (EPKG_FATAL);
			}
			utstring_printf(buf, "%s", plist_file);
			break;
		case 'f':
			if (plist_file == NULL || plist_file[0] == '\0') {
				pkg_emit_error("No files defined %%f couldn't "
				    "be expanded, ignoring %s", in);
				utstring_free(buf);
				return (EPKG_FATAL);
			}
			if (prefix[strlen(prefix) - 1] == '/')
				snprintf(path, sizeof(path), "%s%s",
				    prefix, plist_file);
			else
				snprintf(path, sizeof(path), "%s/%s",
				    prefix, plist_file);
			cp = strrchr(path, '/');
			cp ++;
			utstring_printf(buf, "%s", cp);
			break;
		case 'B':
			if (plist_file == NULL || plist_file[0] == '\0') {
				pkg_emit_error("No files defined %%B couldn't "
				    "be expanded, ignoring %s", in);
				utstring_free(buf);
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
			utstring_printf(buf, "%s", path);
			break;
		case '%':
			utstring_printf(buf, "%c", '%');
			break;
		case '@':
			if (line != NULL) {
				utstring_printf(buf, "%s", line);
				break;
			}

			/*
			 * no break here because if line is not
			 * given (default exec) %@ does not
			 * exists
			 */
			/* FALLTHRU */
		case '#':
			utstring_printf(buf, "%c", argc);
			break;
		default:
			if ((sz = strspn(in, "0123456789")) > 0) {
				int pos = strtol(in, NULL, 10);
				if (pos > argc) {
					pkg_emit_error("Requesting argument "
					    "%%%d while only %d arguments are"
					    " available", pos, argc);
					utstring_free(buf);
					return (EPKG_FATAL);
				}
				utstring_printf(buf, "%s", argv[pos -1]);
				in += sz -1;
				break;
			}
			utstring_printf(buf, "%c%c", '%', in[0]);
			break;
		}

		in++;
	}

	*dest = xstrdup(utstring_body(buf));
	utstring_free(buf);
	
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
string_end_with(const char *path, const char *str)
{
	size_t n, s;
	const char *p = NULL;

	s = strlen(str);
	n = strlen(path);

	if (n < s)
		return (false);

	p = &path[n - s];

	if (strcmp(p, str) == 0)
		return (true);

	return (false);
}

bool
check_for_hardlink(hardlinks_t *hl, struct stat *st)
{
	int absent;

	kh_put_hardlinks(hl, st->st_ino, &absent);
	if (absent == 0)
		return (true);

	return (false);
}

bool
is_valid_abi(const char *arch, bool emit_error) {
	const char *myarch, *myarch_legacy;

	myarch = pkg_object_string(pkg_config_get("ABI"));
	myarch_legacy = pkg_object_string(pkg_config_get("ALTABI"));

	if (fnmatch(arch, myarch, FNM_CASEFOLD) == FNM_NOMATCH &&
	    fnmatch(arch, myarch_legacy, FNM_CASEFOLD) == FNM_NOMATCH &&
	    strncasecmp(arch, myarch, strlen(myarch)) != 0 &&
	    strncasecmp(arch, myarch_legacy, strlen(myarch_legacy)) != 0) {
		if (emit_error)
			pkg_emit_error("wrong architecture: %s instead of %s",
			    arch, myarch);
		return (false);
	}

	return (true);
}

bool
is_valid_os_version(struct pkg *pkg)
{
#ifdef __FreeBSD__
	const char *fbsd_version;
	const char *errstr = NULL;
	int fbsdver;
	char query_buf[512];
	/* -1: not checked, 0: not allowed, 1: allowed */
	static int osver_mismatch_allowed = -1;
	bool ret;

	if (pkg_object_bool(pkg_config_get("IGNORE_OSVERSION")))
		return (true);
	if ((fbsd_version = pkg_kv_get(&pkg->annotations, "FreeBSD_version")) != NULL) {
		fbsdver = strtonum(fbsd_version, 1, INT_MAX, &errstr);
		if (errstr != NULL) {
			pkg_emit_error("Invalid FreeBSD version %s for package %s",
			    fbsd_version, pkg->name);
			return (false);
		}
		if (fbsdver > ctx.osversion) {
			if (fbsdver - ctx.osversion < 100000) {
				/* Negligible difference, ask user to enforce */
				if (osver_mismatch_allowed == -1) {
					snprintf(query_buf, sizeof(query_buf),
							"Newer FreeBSD version for package %s:\n"
							"To ignore this error set IGNORE_OSVERSION=yes\n"
							"- package: %d\n"
							"- running kernel: %d\n"
							"Ignore the mismatch and continue? ", pkg->name,
							fbsdver, ctx.osversion);
					ret = pkg_emit_query_yesno(true, query_buf);
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
					fbsdver, ctx.osversion);
				return (false);
			}
		}
	}
	return (true);
#else
	return (true);
#endif

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
		closefrom(STDERR_FILENO + 1);

		execve(_PATH_BSHELL, argv, environ);

		exit(127);
	}

	return (-1); /* ? */
}

static int
ucl_file_append_character(unsigned char c, size_t len, void *data)
{
	size_t i;
	FILE *out = data;

	for (i = 0; i < len; i++)
		fprintf(out, "%c", c);

	return (0);
}

static int
ucl_file_append_len(const unsigned char *str, size_t len, void *data)
{
	FILE *out = data;

	fprintf(out, "%.*s", (int)len, str);

	return (0);
}

static int
ucl_file_append_int(int64_t val, void *data)
{
	FILE *out = data;

	fprintf(out, "%"PRId64, val);

	return (0);
}

static int
ucl_file_append_double(double val, void *data)
{
	FILE *out = data;
	const double delta = 0.0000001;

	if (val == (double)(int)val) {
		fprintf(out, "%.1lf", val);
	} else if (fabs(val - (double)(int)val) < delta) {
		fprintf(out, "%.*lg", DBL_DIG, val);
	} else {
		fprintf(out, "%lf", val);
	}

	return (0);
}

static int
ucl_buf_append_character(unsigned char c, size_t len, void *data)
{
	UT_string *buf = data;
	size_t i;

	for (i = 0; i < len; i++)
		utstring_printf(buf, "%c", c);

	return (0);
}

static int
ucl_buf_append_len(const unsigned char *str, size_t len, void *data)
{
	UT_string *buf = data;

	utstring_bincpy(buf, str, len);

	return (0);
}

static int
ucl_buf_append_int(int64_t val, void *data)
{
	UT_string *buf = data;

	utstring_printf(buf, "%"PRId64, val);

	return (0);
}

static int
ucl_buf_append_double(double val, void *data)
{
	UT_string *buf = data;
	const double delta = 0.0000001;

	if (val == (double)(int)val) {
		utstring_printf(buf, "%.1lf", val);
	} else if (fabs(val - (double)(int)val) < delta) {
		utstring_printf(buf, "%.*lg", DBL_DIG, val);
	} else {
		utstring_printf(buf, "%lf", val);
	}

	return (0);
}

bool
ucl_object_emit_file(const ucl_object_t *obj, enum ucl_emitter emit_type,
    FILE *out)
{
	struct ucl_emitter_functions func = {
		.ucl_emitter_append_character = ucl_file_append_character,
		.ucl_emitter_append_len = ucl_file_append_len,
		.ucl_emitter_append_int = ucl_file_append_int,
		.ucl_emitter_append_double = ucl_file_append_double
	};

	if (obj == NULL)
		return (false);

	func.ud = out;

	return (ucl_object_emit_full(obj, emit_type, &func, NULL));
}

bool
ucl_object_emit_buf(const ucl_object_t *obj, enum ucl_emitter emit_type,
                     UT_string **buf)
{
	bool ret = false;
	struct ucl_emitter_functions func = {
		.ucl_emitter_append_character = ucl_buf_append_character,
		.ucl_emitter_append_len = ucl_buf_append_len,
		.ucl_emitter_append_int = ucl_buf_append_int,
		.ucl_emitter_append_double = ucl_buf_append_double
	};

	if (*buf == NULL)
		utstring_new(*buf);
	else
		utstring_clear(*buf);

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

int
pkg_utils_count_spaces(const char *args)
{
	int		spaces;
	const char	*p;

	for (spaces = 0, p = args; *p != '\0'; p++)
		if (isspace(*p))
			spaces++;

	return (spaces);
}

/* unlike realpath(3), this routine does not expand symbolic links */
char *
pkg_absolutepath(const char *src, char *dest, size_t dest_size, bool fromroot) {
	size_t dest_len, src_len, cur_len;
	const char *cur, *next;

	src_len = strlen(src);
	bzero(dest, dest_size);
	if (src_len != 0 && src[0] != '/') {
		if (fromroot)
			*dest = '/';
		/* relative path, we use cwd */
		else if (getcwd(dest, dest_size) == NULL)
			return (NULL);
	}
	dest_len = strlen(dest);

	for (cur = next = src; next != NULL; cur = next + 1) {
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
	char *walk, *walkorig, pathdone[MAXPATHLEN];

	walk = walkorig = xstrdup(path);
	pathdone[0] = '\0';

	while ((next = strsep(&walk, "/")) != NULL) {
		if (*next == '\0')
			continue;
		strlcat(pathdone, next, sizeof(pathdone));
		if (mkdirat(fd, pathdone, 0755) == -1) {
			if (errno == EEXIST) {
				strlcat(pathdone, "/", sizeof(pathdone));
				continue;
			}
			pkg_errno("Fail to create /%s", pathdone);
			free(walkorig);
			return (false);
		}
		strlcat(pathdone, "/", sizeof(pathdone));
	}
	free(walkorig);
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

#ifdef HAVE_DECL_SOCK_SEQPACKET
	r = socketpair(AF_LOCAL, SOCK_SEQPACKET, 0, pipe);
	if (r == -1) {
		r = socketpair(AF_LOCAL, SOCK_DGRAM, 0, pipe);
	}
#else
	r = socketpair(AF_LOCAL, SOCK_DGRAM, 0, pipe);
#endif

	return (r);
}
