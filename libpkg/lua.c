/*-
 * SPDX-License-Identifier: LicenseRef-scancode-bsd-unchanged
 *
 * Copyright (c) 2019-2025 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include "pkg_config.h"

#if __has_include(<sys/capsicum.h>)
#define HAVE_CAPSICUM 1
#include <sys/capsicum.h>
#endif

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdbool.h>
#include <unistd.h>
#include <pkg/sb.h>

#include "private/pkg.h"
#include "private/event.h"
#include "private/lua.h"

#ifndef DEFFILEMODE
#define DEFFILEMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)
#endif

extern char **environ;

int
stack_dump(lua_State *L)
{
	int i;
	int top = lua_gettop(L);
	sb_t stack = sb_init();
	char *stackstr;

	sb_cat(&stack, "\nLua Stack\n---------\n"
	    "\tType   Data\n\t-----------\n");

	for (i = 1; i <= top; i++) {  /* repeat for each level */
		int t = lua_type(L, i);
		sb_printf(&stack, "%i", i);
		switch (t) {
		case LUA_TSTRING:  /* strings */
			sb_printf(&stack, "\tString: `%s'\n", lua_tostring(L, i));
			break;
		case LUA_TBOOLEAN:  /* booleans */
			sb_printf(&stack, "\tBoolean: %s", lua_toboolean(L, i) ? "\ttrue\n" : "\tfalse\n");
			break;
		case LUA_TNUMBER:  /* numbers */
			sb_printf(&stack, "\tNumber: %g\n", lua_tonumber(L, i));
			break;
		default:  /* other values */
			sb_printf(&stack, "\tOther: %s\n", lua_typename(L, t));
			break;
		}
	}
	stackstr = sb_get(&stack);
	pkg_emit_error("%s\n", stackstr);
	free(stackstr);

	return (0);
}

int
lua_print_msg(lua_State *L)
{
	int n = lua_gettop(L);
	luaL_argcheck(L, n == 1, n > 1 ? 2 : n,
	    "pkg.print_msg takes exactly one argument");
	const char* str = luaL_checkstring(L, 1);
	lua_getglobal(L, "msgfd");
	int fd = lua_tointeger(L, -1);

	dprintf(fd, "%s\n", str);

	return (0);
}


static const char**
luaL_checkarraystrings(lua_State *L, int arg) {
	const char **ret;
	lua_Integer n, i;
	int t;
	int abs_arg = lua_absindex(L, arg);
	luaL_checktype(L, abs_arg, LUA_TTABLE);
	n = lua_rawlen(L, abs_arg);
	ret = lua_newuserdata(L, (n+1)*sizeof(char*));
	for (i=0; i<n; i++) {
		t = lua_rawgeti(L, abs_arg, i+1);
		if (t == LUA_TNIL)
			break;
		luaL_argcheck(L, t == LUA_TSTRING, arg, "expected array of strings");
		ret[i] = lua_tostring(L, -1);
		lua_pop(L, 1);
	}
	ret[i] = NULL;
	return ret;
}

int
lua_exec(lua_State *L)
{
	int r, pstat;
	posix_spawn_file_actions_t action;
	int stdin_pipe[2] = {-1, -1};
	pid_t pid;
	const char **argv;
	int n = lua_gettop(L);
	luaL_argcheck(L, n == 1, n > 1 ? 2 : n,
	    "pkg.exec takes exactly one argument");

#ifdef HAVE_CAPSICUM
	unsigned int capmode;
	if (cap_getmode(&capmode) == 0 && capmode > 0) {
		return (luaL_error(L, "pkg.exec not available in sandbox"));
	}
#endif
	if (pipe(stdin_pipe) < 0)
		return (EPKG_FATAL);

	posix_spawn_file_actions_init(&action);
	posix_spawn_file_actions_adddup2(&action, stdin_pipe[0], STDIN_FILENO);
	posix_spawn_file_actions_addclose(&action, stdin_pipe[1]);

	argv = luaL_checkarraystrings(L, 1);
	if (0 != (r = posix_spawnp(&pid, argv[0], &action, NULL,
		(char*const*)argv, environ))) {
		lua_pushnil(L);
		lua_pushstring(L, strerror(r));
		lua_pushinteger(L, r);
		return 3;
	}
	while (waitpid(pid, &pstat, 0) == -1) {
		if (errno != EINTR) {
			lua_pushnil(L);
			lua_pushstring(L, strerror(r));
			lua_pushinteger(L, r);
			return 3;
		}
	}

	if (!WIFEXITED(pstat) || WEXITSTATUS(pstat) != 0) {
		lua_pushnil(L);
		lua_pushstring(L, "Abnormal termination");
		lua_pushinteger(L, r);
		return 3;
	}

	posix_spawn_file_actions_destroy(&action);

	if (stdin_pipe[0] != -1)
		close(stdin_pipe[0]);
	if (stdin_pipe[1] != -1)
		close(stdin_pipe[1]);
	lua_pushinteger(L, pid);
	return 1;
}

int
lua_exec_capture(lua_State *L)
{
	int r, pstat;
	posix_spawn_file_actions_t action;
	int stdout_pipe[2] = {-1, -1};
	int stderr_pipe[2] = {-1, -1};
	pid_t pid;
	const char **argv;
	int n = lua_gettop(L);
	char buf[BUFSIZ];
	ssize_t nread;
	sb_t out = sb_init();
	sb_t err = sb_init();

	luaL_argcheck(L, n == 1, n > 1 ? 2 : n,
	    "pkg.exec_capture takes exactly one argument");

#ifdef HAVE_CAPSICUM
	unsigned int capmode;
	if (cap_getmode(&capmode) == 0 && capmode > 0) {
		return (luaL_error(L,
		    "pkg.exec_capture not available in sandbox"));
	}
#endif
	if (pipe(stdout_pipe) < 0)
		return (luaL_error(L, "pipe: %s", strerror(errno)));
	if (pipe(stderr_pipe) < 0) {
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		return (luaL_error(L, "pipe: %s", strerror(errno)));
	}

	posix_spawn_file_actions_init(&action);
	posix_spawn_file_actions_adddup2(&action, stdout_pipe[1],
	    STDOUT_FILENO);
	posix_spawn_file_actions_addclose(&action, stdout_pipe[0]);
	posix_spawn_file_actions_adddup2(&action, stderr_pipe[1],
	    STDERR_FILENO);
	posix_spawn_file_actions_addclose(&action, stderr_pipe[0]);

	argv = luaL_checkarraystrings(L, 1);
	if (0 != (r = posix_spawnp(&pid, argv[0], &action, NULL,
		(char*const*)argv, environ))) {
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		close(stderr_pipe[0]);
		close(stderr_pipe[1]);
		posix_spawn_file_actions_destroy(&action);
		lua_pushnil(L);
		lua_pushnil(L);
		lua_pushinteger(L, r);
		return 3;
	}
	posix_spawn_file_actions_destroy(&action);
	close(stdout_pipe[1]);
	close(stderr_pipe[1]);

	while ((nread = read(stdout_pipe[0], buf, sizeof(buf))) != 0) {
		if (nread < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		sb_cat_n(&out, buf, nread);
	}
	close(stdout_pipe[0]);

	while ((nread = read(stderr_pipe[0], buf, sizeof(buf))) != 0) {
		if (nread < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		sb_cat_n(&err, buf, nread);
	}
	close(stderr_pipe[0]);

	while (waitpid(pid, &pstat, 0) == -1) {
		if (errno != EINTR) {
			sb_fini(&out);
			sb_fini(&err);
			lua_pushnil(L);
			lua_pushnil(L);
			lua_pushinteger(L, -1);
			return 3;
		}
	}



	lua_pushstring(L, sb_str(&out));
	lua_pushstring(L, sb_str(&err));
	/* a command killed by a signal has no exit status, report failure */
	lua_pushinteger(L, WIFEXITED(pstat) ? WEXITSTATUS(pstat) : -1);
	sb_fini(&out);
	sb_fini(&err);
	return 3;
}

int
lua_pkg_copy(lua_State *L)
{
	int n = lua_gettop(L);
	luaL_argcheck(L, n == 2, n > 2 ? 3 : n,
	    "pkg.copy takes exactly two arguments");
	const char* src = luaL_checkstring(L, 1);
	const char* dst = luaL_checkstring(L, 2);
	struct stat s1;
	int fd1, fd2;
	struct timespec ts[2];

	bool install_as_user = (getenv("INSTALL_AS_USER") != NULL);

	lua_getglobal(L, "rootfd");
	int rootfd = lua_tointeger(L, -1);

	if (fstatat(rootfd, RELATIVE_PATH(src), &s1, 0) == -1) {
		lua_pushinteger(L, 2);
		return (1);
	}
	fd1 = openat(rootfd, RELATIVE_PATH(src), O_RDONLY, DEFFILEMODE);
	if (fd1 == -1) {
		lua_pushinteger(L, 2);
		return (1);
	}

	fd2 = openat(rootfd, RELATIVE_PATH(dst), O_RDWR | O_CREAT | O_TRUNC | O_EXCL, s1.st_mode);
	if (fd2 == -1) {
		lua_pushinteger(L, 2);
		return (1);
	}

	if (!pkg_copy_file(fd1, fd2)) {
		lua_pushinteger(L, 2);
		return (1);
	}
	if (fchown(fd2, s1.st_uid, s1.st_gid) == -1) {
		lua_pushinteger(L, 2);
		return (1);
	}

	fsync(fd2);
	close(fd1);
	close(fd2);

	ts[0] = s1.st_atim;
	ts[1] = s1.st_mtim;

	if (set_attrsat(rootfd, RELATIVE_PATH(dst), s1.st_mode, s1.st_uid,
	  s1.st_gid, ts, install_as_user) != EPKG_OK) {
		lua_pushinteger(L, -1);
		return (1);
	}

#ifdef HAVE_CHFLAGSAT
	if (!install_as_user && s1.st_flags != 0) {
		if (chflagsat(rootfd, RELATIVE_PATH(dst),
		    s1.st_flags, AT_SYMLINK_NOFOLLOW) == -1) {
			pkg_fatal_errno("Failed to chflags %s", dst);
			lua_pushinteger(L, -1);
			return (1);
		}
	}
#endif
	return (0);
}

int
lua_pkg_filecmp(lua_State *L)
{
	int n = lua_gettop(L);
	luaL_argcheck(L, n == 2, n > 2 ? 3 : n,
	    "pkg.filecmp takes exactly two arguments");
	const char* file1 = luaL_checkstring(L, 1);
	const char* file2 = luaL_checkstring(L, 2);
	char *buf1, *buf2;
	struct stat s1, s2;
	int fd1, fd2;
	int ret = 0;

	lua_getglobal(L, "rootfd");
	int rootfd = lua_tointeger(L, -1);

	if (fstatat(rootfd, RELATIVE_PATH(file1), &s1, 0) == -1) {
		lua_pushinteger(L, 2);
		return (1);
	}
	if (fstatat(rootfd, RELATIVE_PATH(file2), &s2, 0) == -1) {
		lua_pushinteger(L, 2);
		return (1);
	}
	if (s1.st_size != s2.st_size) {
		lua_pushinteger(L, 1);
		return (1);
	}
	fd1 = openat(rootfd, RELATIVE_PATH(file1), O_RDONLY, DEFFILEMODE);
	if (fd1 == -1) {
		lua_pushinteger(L, 2);
		return (1);
	}
	buf1 = mmap(NULL, s1.st_size, PROT_READ, MAP_SHARED, fd1, 0);
	close(fd1);
	if (buf1 == NULL) {
		lua_pushinteger(L, -1);
		return (1);
	}
	fd2 = openat(rootfd, RELATIVE_PATH(file2), O_RDONLY, DEFFILEMODE);
	if (fd2 == -1) {
		lua_pushinteger(L, 2);
		return (1);
	}

	buf2 = mmap(NULL, s2.st_size, PROT_READ, MAP_SHARED, fd2, 0);
	close(fd2);
	if (buf2 == NULL) {
		lua_pushinteger(L, -1);
		return (1);
	}
	if (memcmp(buf1, buf2, s1.st_size) != 0)
		ret = 1;

	munmap(buf1, s1.st_size);
	munmap(buf2, s2.st_size);

	lua_pushinteger(L, ret);
	return (1);
}

int
lua_pkg_symlink(lua_State *L)
{
	int n = lua_gettop(L);
	luaL_argcheck(L, n == 2, n > 2 ? 3 : n,
	    "pkg.symlink takes exactly two arguments");
	const char *from = luaL_checkstring(L, 1);
	const char *to = luaL_checkstring(L, 2);
	lua_getglobal(L, "rootfd");
	int rootfd = lua_tointeger(L, -1);
	if (symlinkat(from, rootfd, RELATIVE_PATH(to)) == -1)
		return (luaL_fileresult(L, 0, from));
	return (1);
}

int
lua_prefix_path(lua_State *L)
{
	int n = lua_gettop(L);
	luaL_argcheck(L, n == 1, n > 1 ? 2 : n,
	    "pkg.prefix_path takes exactly one argument");
	const char *str = luaL_checkstring(L, 1);
	lua_getglobal(L, "package");
	struct pkg *p = lua_touserdata(L, -1);

	char path[MAXPATHLEN];
	path[0] = '\0';

	if (*str == '/') {
		strlcat(path, str, MAXPATHLEN);
	} else {
		strlcat(path, p->prefix, MAXPATHLEN);
		strlcat(path, "/", MAXPATHLEN);
		strlcat(path, str, MAXPATHLEN);
	}

	lua_pushstring(L, path);
	return (1);
}

int
lua_stat(lua_State *L)
{
	int n = lua_gettop(L);
	luaL_argcheck(L, n == 1, n > 1 ? 2 : n,
	    "pkg.stat takes exactly one argument");
	const char *path = RELATIVE_PATH(luaL_checkstring(L, 1));
	lua_getglobal(L, "rootfd");
	int rootfd = lua_tointeger(L, -1);
	struct stat s;
	const char *type = "unknown";

	if (fstatat(rootfd, path, &s, AT_SYMLINK_NOFOLLOW) == -1) {
		return lua_pushnil(L), 1;
	}

	lua_settop(L, 2);
	if (!lua_istable(L, 2))
		lua_newtable(L);

	lua_pushinteger(L, s.st_size);
	lua_setfield(L, -2, "size");
	lua_pushinteger(L, s.st_uid);
	lua_setfield(L, -2, "uid");
	lua_pushinteger(L, s.st_gid);
	lua_setfield(L, -2, "gid");
	if (S_ISREG(s.st_mode))
		type = "reg";
	else if (S_ISDIR(s.st_mode))
		type = "dir";
	else if (S_ISCHR(s.st_mode))
		type = "chr";
	else if (S_ISLNK(s.st_mode))
		type = "lnk";
	else if (S_ISSOCK(s.st_mode))
		type = "sock";
	else if (S_ISBLK(s.st_mode))
		type = "blk";
	else if (S_ISFIFO(s.st_mode))
		type = "fifo";
	lua_pushstring(L, type);
	lua_setfield(L, -2, "type");

	return (1);
}

#ifdef __FreeBSD__
static int
lua_sysctl_kind(const char *name, u_int *kind)
{
	int mib[CTL_MAXNAME];
	int qoid[CTL_MAXNAME + 2];
	u_char buf[BUFSIZ];
	size_t len = sizeof(mib) / sizeof(mib[0]);
	size_t buflen = sizeof(buf);

	if (sysctlnametomib(name, mib, &len) == -1)
		return (-1);
	qoid[0] = CTL_SYSCTL;
	qoid[1] = CTL_SYSCTL_OIDFMT;
	memcpy(qoid + 2, mib, len * sizeof(int));
	if (sysctl(qoid, len + 2, buf, &buflen, NULL, 0) == -1)
		return (-1);
	memcpy(kind, buf, sizeof(*kind));
	return (0);
}

static int64_t
lua_sysctl_signed(const void *p, size_t size)
{
	int8_t s8;
	int16_t s16;
	int32_t s32;
	int64_t s64;

	switch (size) {
	case 1:
		memcpy(&s8, p, sizeof(s8));
		return (s8);
	case 2:
		memcpy(&s16, p, sizeof(s16));
		return (s16);
	case 4:
		memcpy(&s32, p, sizeof(s32));
		return (s32);
	case 8:
		memcpy(&s64, p, sizeof(s64));
		return (s64);
	}
	return (0);
}

static uint64_t
lua_sysctl_unsigned(const void *p, size_t size)
{
	uint8_t u8;
	uint16_t u16;
	uint32_t u32;
	uint64_t u64;

	switch (size) {
	case 1:
		memcpy(&u8, p, sizeof(u8));
		return (u8);
	case 2:
		memcpy(&u16, p, sizeof(u16));
		return (u16);
	case 4:
		memcpy(&u32, p, sizeof(u32));
		return (u32);
	case 8:
		memcpy(&u64, p, sizeof(u64));
		return (u64);
	}
	return (0);
}
#endif

#ifdef __FreeBSD__
int
lua_sysctl(lua_State *L)
{
	int n = lua_gettop(L);
	luaL_argcheck(L, n == 1, n > 1 ? 2 : n,
	    "pkg.sysctl takes exactly one argument");
	const char *name = luaL_checkstring(L, 1);
	u_int kind;
	size_t len = 0;
	void *val;
	size_t size;
	bool is_signed, numeric = true;

	if (lua_sysctl_kind(name, &kind) == -1)
		goto fail;
	if ((kind & CTLTYPE) == CTLTYPE_NODE) {
		lua_pushnil(L);
		lua_pushstring(L, "node is not a value");
		return (2);
	}
	if (sysctlbyname(name, NULL, &len, NULL, 0) == -1)
		goto fail;
	if (len == 0) {
		if ((kind & CTLTYPE) == CTLTYPE_STRING ||
		    (kind & CTLTYPE) == CTLTYPE_OPAQUE)
			lua_pushstring(L, "");
		else
			lua_pushnil(L);
		return (1);
	}
	val = xmalloc(len);
	if (sysctlbyname(name, val, &len, NULL, 0) == -1) {
		free(val);
		goto fail;
	}
	switch (kind & CTLTYPE) {
	case CTLTYPE_INT:
		size = sizeof(int);
		is_signed = true;
		break;
	case CTLTYPE_UINT:
		size = sizeof(u_int);
		is_signed = false;
		break;
	case CTLTYPE_LONG:
		size = sizeof(long);
		is_signed = true;
		break;
	case CTLTYPE_ULONG:
		size = sizeof(u_long);
		is_signed = false;
		break;
	case CTLTYPE_S64:
		size = sizeof(int64_t);
		is_signed = true;
		break;
	case CTLTYPE_U64:
		size = sizeof(uint64_t);
		is_signed = false;
		break;
	case CTLTYPE_S8:
		size = sizeof(int8_t);
		is_signed = true;
		break;
	case CTLTYPE_S16:
		size = sizeof(int16_t);
		is_signed = true;
		break;
	case CTLTYPE_S32:
		size = sizeof(int32_t);
		is_signed = true;
		break;
	case CTLTYPE_U8:
		size = sizeof(uint8_t);
		is_signed = false;
		break;
	case CTLTYPE_U16:
		size = sizeof(uint16_t);
		is_signed = false;
		break;
	case CTLTYPE_U32:
		size = sizeof(uint32_t);
		is_signed = false;
		break;
	default:
		numeric = false;
		break;
	}
	if (numeric) {
		size_t cnt = len / size, i;
		if (cnt == 1) {
			lua_pushinteger(L, is_signed ?
			    lua_sysctl_signed(val, size) :
			    (lua_Integer)lua_sysctl_unsigned(val, size));
		} else {
			lua_createtable(L, cnt, 0);
			for (i = 0; i < cnt; i++) {
				void *p = (char *)val + i * size;
				lua_pushinteger(L, is_signed ?
				    lua_sysctl_signed(p, size) :
				    (lua_Integer)lua_sysctl_unsigned(p, size));
				lua_rawseti(L, -2, i + 1);
			}
		}
	} else if ((kind & CTLTYPE) == CTLTYPE_STRING) {
		lua_pushlstring(L, val, strnlen(val, len));
	} else if ((kind & CTLTYPE) == CTLTYPE_OPAQUE) {
		lua_pushlstring(L, val, len);
	} else {
		free(val);
		lua_pushnil(L);
		lua_pushfstring(L, "unsupported sysctl type %d",
		    kind & CTLTYPE);
		return (2);
	}
	free(val);
	return (1);
fail:
	lua_pushnil(L);
	lua_pushstring(L, strerror(errno));
	return (2);
}
#endif

/* stolen from lua.c */
void
lua_args_table(lua_State *L, char **argv, int argc)
{
	lua_createtable(L, argc, 1);
	for (int i = 0; i < argc; i++) {
		lua_pushstring(L, argv[i]);
		/* lua starts counting by 1 */
		lua_rawseti(L, -2, i + 1);
	}
	lua_setglobal(L, "arg");
}


/*
 * this is a copy of lua code to be able to override open
 * merge of newprefile and newfile
 */

static int
my_iofclose(lua_State *L)
{
	luaL_Stream *p = ((luaL_Stream *)luaL_checkudata(L, 1, LUA_FILEHANDLE));
	int res = fclose(p->f);
	return (luaL_fileresult(L, (res == 0), NULL));
}

static luaL_Stream *
newfile(lua_State *L) {
	luaL_Stream *p = (luaL_Stream *)lua_newuserdata(L, sizeof(luaL_Stream));
	p->f = NULL;
	p->closef = &my_iofclose;
	luaL_setmetatable(L, LUA_FILEHANDLE);
	return (p);
}

static int
lua_io_open(lua_State *L)
{
	const char *filename = luaL_checkstring(L, 1);
	const char *mode = luaL_optstring(L, 2, "r");
	lua_getglobal(L, "rootfd");
	int rootfd = lua_tointeger(L, -1);
	int oflags;
	luaL_Stream *p = newfile(L);
	const char *md = mode;
	luaL_argcheck(L, checkflags(md, &oflags), 2, "invalid mode");
	int fd = openat(rootfd, RELATIVE_PATH(filename), oflags, DEFFILEMODE);
	if (fd == -1)
		return (luaL_fileresult(L, 0, filename));
	p->f = fdopen(fd, mode);
	return ((p->f == NULL) ? luaL_fileresult(L, 0, filename) : 1);
}

static int
lua_os_remove(lua_State *L) {
	const char *filename = RELATIVE_PATH(luaL_checkstring(L, 1));
	lua_getglobal(L, "rootfd");
	int rootfd = lua_tointeger(L, -1);
	int flag = 0;
	struct stat st;

	if (fstatat(rootfd, filename, &st, AT_SYMLINK_NOFOLLOW) == -1)
		return (luaL_fileresult(L, 1, NULL));

	if (S_ISDIR(st.st_mode))
		flag = AT_REMOVEDIR;

	return (luaL_fileresult(L, unlinkat(rootfd, filename, flag) == 0, NULL));
}

static int
lua_os_rename(lua_State *L)
{
	const char *fromname = RELATIVE_PATH(luaL_checkstring(L, 1));
	const char *toname = RELATIVE_PATH(luaL_checkstring(L, 2));
	lua_getglobal(L, "rootfd");
	int rootfd = lua_tointeger(L, -1);
	return luaL_fileresult(L, renameat(rootfd, fromname, rootfd, toname) == 0, NULL);
}

static int
lua_os_execute(lua_State *L)
{
	return (luaL_error(L, "os.execute not available"));
}

static int
lua_os_exit(lua_State *L)
{
	return (luaL_error(L, "os.exit not available"));
}

void
lua_override_ios(lua_State *L, bool sandboxed)
{
	lua_getglobal(L, "io");
	lua_pushcfunction(L, lua_io_open);
	lua_setfield(L, -2, "open");

	lua_getglobal(L, "os");
	lua_pushcfunction(L, lua_os_remove);
	lua_setfield(L, -2, "remove");
	lua_pushcfunction(L, lua_os_rename);
	lua_setfield(L, -2, "rename");
	if (sandboxed) {
		lua_pushcfunction(L, lua_os_execute);
		lua_setfield(L, -2, "execute");
	}
	lua_pushcfunction(L, lua_os_exit);
	lua_setfield(L, -2, "exit");
}

int
lua_readdir(lua_State *L)
{
	int n = lua_gettop(L);
	luaL_argcheck(L, n == 1, n > 1 ? 2 : n,
	    "pkg.readdir takes exactly one argument");
	const char *path = luaL_checkstring(L, 1);
	int fd = -1;

	if (*path == '/') {
		lua_getglobal(L, "rootfd");
		int rootfd = lua_tointeger(L, -1);
		if (strlen(path) > 1) {
			fd = openat(rootfd, path +1, O_DIRECTORY);
		} else {
			fd = dup(rootfd);
		}
	} else {
		fd = open(path, O_DIRECTORY);
	}
	if (fd == -1)
		return (luaL_fileresult(L, 0, path));

	DIR *dir = fdopendir(fd);
	if (!dir)
		return (luaL_fileresult(L, 0, path));
	lua_newtable(L);
	struct dirent *e;
	int i = 0;
	while ((e = readdir(dir))) {
		char *name = e->d_name;
		if (STREQ(name, ".") || STREQ(name, ".."))
			continue;
		lua_pushinteger(L, ++i);
		lua_pushstring(L, name);
		lua_settable(L, -3);
	}
	return 1;
}

int
lua_metalog_copy(lua_State *L)
{
	int n = lua_gettop(L);
	luaL_argcheck(L, n == 2, n > 2 ? 3 : n,
	    "pkg.metalog_copy takes exactly two arguments");
	const char *src = luaL_checkstring(L, 1);
	const char *dst = luaL_checkstring(L, 2);
	lua_getglobal(L, "package");
	struct pkg *p = lua_touserdata(L, -1);
	struct pkg_file *f = pkg_get_file(p, src);
	if (f == NULL) {
		lua_pushnil(L);
		lua_pushstring(L, "Unknown source file");
		return (2);
	}
	/* TODO: what about symlinks ? */
	metalog_add(PKG_METALOG_FILE, RELATIVE_PATH(dst),
	    f->uname, f->gname, f->perm & ~S_IFREG, f->fflags, f->symlink_target);
	return (1);
}
