/*-
 * Copyright (c) 2019 Baptiste Daroussin <bapt@FreeBSD.org>
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

#ifdef HAVE_SYS_PROCCTL_H
#include <sys/procctl.h>
#endif

#ifdef HAVE_CAPSICUM
#include <sys/capsicum.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include <errno.h>
#include <poll.h>
#include <xstring.h>
#include <lauxlib.h>
#include <lualib.h>
#include <fcntl.h>
#include <err.h>
#include <stdio.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

extern char **environ;

static lua_CFunction
stack_dump(lua_State *L)
{
	int i;
	int top = lua_gettop(L);
	xstring *stack;

	stack = xstring_new();

	fprintf(stack->fp, "\nLua Stack\n---------\n");
	fprintf(stack->fp, "\tType   Data\n\t-----------\n" );

	for (i = 1; i <= top; i++) {  /* repeat for each level */
		int t = lua_type(L, i);
		fprintf(stack->fp, "%i", i);
		switch (t) {
		case LUA_TSTRING:  /* strings */
			fprintf(stack->fp, "\tString: `%s'\n", lua_tostring(L, i));
			break;
		case LUA_TBOOLEAN:  /* booleans */
			fprintf(stack->fp, "\tBoolean: %s", lua_toboolean(L, i) ? "\ttrue\n" : "\tfalse\n");
			break;
		case LUA_TNUMBER:  /* numbers */
			fprintf(stack->fp, "\tNumber: %g\n", lua_tonumber(L, i));
			break;
		default:  /* other values */
			fprintf(stack->fp, "\tOther: %s\n", lua_typename(L, t));
			break;
		}
	}
	fflush(stack->fp);
	pkg_emit_error("%s\n", stack->buf);
	xstring_free(stack);

	return (0);
}

static int
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

static int
lua_pkg_copy(lua_State *L)
{
	int n = lua_gettop(L);
	luaL_argcheck(L, n == 2, n > 2 ? 3 : n,
	    "pkg.copy takes exactly two arguments");
	const char* src = luaL_checkstring(L, 1);
	const char* dst = luaL_checkstring(L, 2);
	char *buf1, *buf2;
	struct stat s1;
	int fd1, fd2;
	struct timespec ts[2];

	bool install_as_user = (getenv("INSTALL_AS_USER") != NULL);

	lua_getglobal(L, "package");
	struct pkg *pkg = lua_touserdata(L, -1);

	if (fstatat(pkg->rootfd, RELATIVE_PATH(src), &s1, 0) == -1) {
		lua_pushinteger(L, 2);
		return (1);
	}
	fd1 = openat(pkg->rootfd, RELATIVE_PATH(src), O_RDONLY, DEFFILEMODE);
	if (fd1 == -1) {
		lua_pushinteger(L, 2);
		return (1);
	}
	/* 
	 * We should be using O_WRONLY but a weird aarch64 pmap
	 * bug is preventing us doing that
	 * See https://bugs.freebsd.org/250271
	 */
	fd2 = openat(pkg->rootfd, RELATIVE_PATH(dst), O_RDWR | O_CREAT | O_TRUNC | O_EXCL, DEFFILEMODE);
	if (fd2 == -1) {
		lua_pushinteger(L, 2);
		return (1);
	}
	if (ftruncate(fd2, s1.st_size) != 0) {
		lua_pushinteger(L, -1);
		return (1);
	}
	buf1 = mmap(NULL, s1.st_size, PROT_READ, MAP_SHARED, fd1, 0);
	if (buf1 == NULL) {
		lua_pushinteger(L, -1);
		return (1);
	}
	/* 
	 * We should be using only PROT_WRITE but a weird aarch64 pmap
	 * bug is preventing us doing that
	 * https://bugs.freebsd.org/250271
	 */
	buf2 = mmap(NULL, s1.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
	if (buf2 == NULL) {
		lua_pushinteger(L, -1);
		return (1);
	}

	memcpy(buf2, buf1, s1.st_size);

	munmap(buf1, s1.st_size);
	munmap(buf2, s1.st_size);
	fsync(fd2);

	close(fd1);
	close(fd2);

#ifdef HAVE_STRUCT_STAT_ST_MTIM
	ts[0] = s1.st_atim;
	ts[1] = s1.st_mtim;
#else
#if defined(_DARWIN_C_SOURCE) || defined(__APPLE__)
	ts[0] = s1.st_atimespec;
	ts[1] = s1.st_mtimespec;
#else
	ts[0].tv_sec = s1.st_atime;
	ts[0].tv_nsec = 0;
	ts[1].tv_sec = s1.st_mtime;
	ts[1].tv_nsec = 0;
#endif
#endif

	if (set_attrsat(pkg->rootfd, RELATIVE_PATH(dst), s1.st_mode, s1.st_uid,
	  s1.st_gid, &ts[0], &ts[1]) != EPKG_OK) {
		lua_pushinteger(L, -1);
		return (1);
	}

#ifdef HAVE_CHFLAGSAT
	if (!install_as_user && s1.st_flags != 0) {
		if (chflagsat(pkg->rootfd, RELATIVE_PATH(dst),
		    s1.st_flags, AT_SYMLINK_NOFOLLOW) == -1) {
			pkg_fatal_errno("Fail to chflags %s", dst);
			lua_pushinteger(L, -1);
			return (1);
		}
	}
#endif
	return (0);
}

static int
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

	lua_getglobal(L, "package");
	struct pkg *pkg = lua_touserdata(L, -1);

	if (fstatat(pkg->rootfd, RELATIVE_PATH(file1), &s1, AT_SYMLINK_NOFOLLOW) == -1) {
		lua_pushinteger(L, 2);
		return (1);
	}
	if (fstatat(pkg->rootfd, RELATIVE_PATH(file2), &s2, AT_SYMLINK_NOFOLLOW) == -1) {
		lua_pushinteger(L, 2);
		return (1);
	}
	if (!S_ISREG(s1.st_mode) || !S_ISREG(s2.st_mode)) {
		lua_pushinteger(L, -1);
		return (1);
	}
	if (s1.st_size != s2.st_size) {
		lua_pushinteger(L, 1);
		return (1);
	}
	fd1 = openat(pkg->rootfd, RELATIVE_PATH(file1), O_RDONLY, DEFFILEMODE);
	if (fd1 == -1) {
		lua_pushinteger(L, 2);
		return (1);
	}
	fd2 = openat(pkg->rootfd, RELATIVE_PATH(file2), O_RDONLY, DEFFILEMODE);
	if (fd2 == -1) {
		lua_pushinteger(L, 2);
		return (1);
	}

	buf1 = mmap(NULL, s1.st_size, PROT_READ, MAP_SHARED, fd1, 0);
	if (buf1 == NULL) {
		lua_pushinteger(L, -1);
		return (1);
	}
	buf2 = mmap(NULL, s2.st_size, PROT_READ, MAP_SHARED, fd2, 0);
	if (buf2 == NULL) {
		lua_pushinteger(L, -1);
		return (1);
	}
	if (memcmp(buf1, buf2, s1.st_size) != 0)
		ret = 1;

	munmap(buf1, s1.st_size);
	munmap(buf2, s2.st_size);
	close(fd1);
	close(fd2);

	lua_pushinteger(L, ret);
	return (1);
}

static int
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
	lua_getglobal(L, "package");
	struct pkg *pkg = lua_touserdata(L, -1);
	int oflags;
	luaL_Stream *p = newfile(L);
	const char *md = mode;
	luaL_argcheck(L, checkflags(md, &oflags), 2, "invalid mode");
	int fd = openat(pkg->rootfd, RELATIVE_PATH(filename), oflags, DEFFILEMODE);
	if (fd == -1)
		return (luaL_fileresult(L, 0, filename));
	p->f = fdopen(fd, mode);
	return ((p->f == NULL) ? luaL_fileresult(L, 0, filename) : 1);
}

static int
lua_os_remove(lua_State *L) {
	const char *filename = RELATIVE_PATH(luaL_checkstring(L, 1));
	lua_getglobal(L, "package");
	struct pkg *pkg = lua_touserdata(L, -1);
	int flag = 0;
	struct stat st;

	if (fstatat(pkg->rootfd, filename, &st, AT_SYMLINK_NOFOLLOW) == -1)
		return (luaL_fileresult(L, 1, NULL));

	if (S_ISDIR(st.st_mode))
		flag = AT_REMOVEDIR;

	return (luaL_fileresult(L, unlinkat(pkg->rootfd, filename, flag) == 0, NULL));
}

static int
lua_os_rename(lua_State *L)
{
	const char *fromname = RELATIVE_PATH(luaL_checkstring(L, 1));
	const char *toname = RELATIVE_PATH(luaL_checkstring(L, 2));
	lua_getglobal(L, "package");
	struct pkg *pkg = lua_touserdata(L, -1);
	return luaL_fileresult(L, renameat(pkg->rootfd, fromname, pkg->rootfd, toname) == 0, NULL);
}

static int
lua_stat(lua_State *L)
{
	int n = lua_gettop(L);
	luaL_argcheck(L, n == 1, n > 1 ? 2 : n,
	    "pkg.stat takes exactly one argument");
	const char *path = RELATIVE_PATH(luaL_checkstring(L, 1));
	lua_getglobal(L, "package");
	struct pkg *pkg = lua_touserdata(L, -1);
	struct stat s;
	const char *type = "unknown";

	if (fstatat(pkg->rootfd, path, &s, AT_SYMLINK_NOFOLLOW) == -1) {
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

static int
lua_os_execute(lua_State *L)
{
	return (luaL_error(L, "os.execute not available"));
}

static void
lua_override_ios(lua_State *L)
{
	lua_getglobal(L, "io");
	lua_pushcfunction(L, lua_io_open);
	lua_setfield(L, -2, "open");

	lua_getglobal(L, "os");
	lua_pushcfunction(L, lua_os_remove);
	lua_setfield(L, -2, "remove");
	lua_pushcfunction(L, lua_os_rename);
	lua_setfield(L, -2, "rename");
	lua_pushcfunction(L, lua_os_execute);
	lua_setfield(L, -2, "execute");
}

int
pkg_lua_script_run(struct pkg * const pkg, pkg_lua_script type, bool upgrade)
{
	int ret = EPKG_OK;
	struct pkg_lua_script *lscript;
	int pstat;
#ifdef PROC_REAP_KILL
	bool do_reap;
	pid_t mypid;
	struct procctl_reaper_status info;
	struct procctl_reaper_kill killemall;
#endif
	struct pollfd pfd;
	int cur_pipe[2];
	bool should_waitpid;
	char *line = NULL;
	FILE *f;
	ssize_t linecap = 0;

	if (pkg->lua_scripts[type] == NULL)
		return (EPKG_OK);

	if (!pkg_object_bool(pkg_config_get("RUN_SCRIPTS"))) {
		return (EPKG_OK);
	}

#ifdef PROC_REAP_KILL
	mypid = getpid();
	do_reap = procctl(P_PID, mypid, PROC_REAP_ACQUIRE, NULL) == 0;
#endif

	LL_FOREACH(pkg->lua_scripts[type], lscript) {
		if (get_socketpair(cur_pipe) == -1) {
			pkg_emit_errno("pkg_lua_script_script", "socketpair");
			goto cleanup;
		}
		pid_t pid = fork();
		if (pid == 0) {
			static const luaL_Reg pkg_lib[] = {
				{ "print_msg", lua_print_msg },
				{ "prefixed_path", lua_prefix_path },
				{ "filecmp", lua_pkg_filecmp },
				{ "copy", lua_pkg_copy },
				{ "stat", lua_stat },
				{ NULL, NULL },
			};
			close(cur_pipe[0]);
			lua_State *L = luaL_newstate();
			luaL_openlibs( L );
			lua_atpanic(L, (lua_CFunction)stack_dump );
			lua_pushinteger(L, cur_pipe[1]);
			lua_setglobal(L, "msgfd");
			lua_pushlightuserdata(L, pkg);
			lua_setglobal(L, "package");
			lua_pushliteral(L, "PREFIX");
			lua_pushstring(L, pkg->prefix);
			lua_setglobal(L, "pkg_prefix");
			if (ctx.pkg_rootdir == NULL)
				ctx.pkg_rootdir = "/";
			lua_pushstring(L, ctx.pkg_rootdir);
			lua_setglobal(L, "pkg_rootdir");
			lua_pushboolean(L, (upgrade));
			lua_setglobal(L, "pkg_upgrade");
			lua_pushcfunction(L, lua_print_msg);
			luaL_newlib(L, pkg_lib);
			lua_setglobal(L, "pkg");
			lua_override_ios(L);
#ifdef HAVE_CAPSICUM
			if (cap_enter() < 0 && errno != ENOSYS) {
				err(1, "cap_enter failed");
			}
#endif

			pkg_debug(3, "Scripts: executing lua\n--- BEGIN ---\n%s\nScripts: --- END ---", lscript->script);
			if (luaL_dostring(L, lscript->script)) {
				pkg_emit_error("Failed to execute lua script: %s", lua_tostring(L, -1));
				lua_close(L);
				exit(1);
			}

			lua_close(L);
			exit(0);
		} else if (pid < 0) {
			pkg_emit_errno("Cannot fork", "lua_script");
			ret = EPKG_FATAL;
			goto cleanup;
		}

		close(cur_pipe[1]);
		memset(&pfd, 0, sizeof(pfd));
		pfd.fd = cur_pipe[0];
		pfd.events = POLLIN | POLLERR | POLLHUP;

		f = fdopen(pfd.fd, "r");
		should_waitpid = true;
		for (;;) {
			errno = 0;
			int pres = poll(&pfd, 1, 1000);
			if (pres == -1) {
				if (errno == EINTR) {
					continue;
				} else {
					pkg_emit_error("poll() failed: %s",
					    strerror(errno));
					ret = EPKG_FATAL;
					goto cleanup;
				}
			}
			if (pres == 0) {
				pid_t p;
				assert(should_waitpid);
				while ((p = waitpid(pid, &pstat, WNOHANG)) == -1) {
					if (errno != EINTR) {
						pkg_emit_error("waitpid() "
						    "failed: %s", strerror(errno));
						ret = EPKG_FATAL;
						goto cleanup;
					}
				}
				if (p > 0) {
					should_waitpid = false;
					break;
				}
				continue;
			}
			if (pfd.revents & (POLLERR|POLLHUP))
				break;
			if (getline(&line, &linecap, f) > 0)
				pkg_emit_message(line);
			if (feof(f))
				break;
		}
		/* Gather any remaining output */
		while (!feof(f) && !ferror(f) && getline(&line, &linecap, f) > 0) {
			pkg_emit_message(line);
		}
		fclose(f);

		while (should_waitpid && waitpid(pid, &pstat, 0) == -1) {
			if (errno != EINTR) {
				pkg_emit_error("waitpid() failed: %s",
				    strerror(errno));
				ret = EPKG_FATAL;
				goto cleanup;
			}
		}
		if (WEXITSTATUS(pstat) != 0) {
			pkg_emit_error("lua script failed");
			ret = EPKG_FATAL;
			goto cleanup;
		}
	}


cleanup:
#ifdef PROC_REAP_KILL
	/*
	 * If the prior PROCCTL_REAP_ACQUIRE call failed, the kernel
	 * probably doesn't support this, so don't try.
	 */
	if (!do_reap)
		return (ret);

	procctl(P_PID, mypid, PROC_REAP_STATUS, &info);
	if (info.rs_children != 0) {
		killemall.rk_sig = SIGKILL;
		killemall.rk_flags = 0;
		if (procctl(P_PID, mypid, PROC_REAP_KILL, &killemall) != 0) {
			pkg_errno("%s", "Fail to kill all processes");
		}
	}
	procctl(P_PID, mypid, PROC_REAP_RELEASE, NULL);
#endif
	free(line);

	return (ret);
}

ucl_object_t *
pkg_lua_script_to_ucl(struct pkg_lua_script *scripts)
{
	struct pkg_lua_script *script;
	ucl_object_t *array;
	ucl_object_t *obj;

	array = ucl_object_typed_new(UCL_ARRAY);
	LL_FOREACH(scripts, script) {
		obj = ucl_object_typed_new(UCL_OBJECT);

		ucl_array_append(array, ucl_object_fromstring_common(script->script,
				strlen(script->script), UCL_STRING_RAW|UCL_STRING_TRIM));
	}

	return (array);
}

int
pkg_lua_script_from_ucl(struct pkg *pkg, const ucl_object_t *obj, pkg_lua_script type)
{
	struct pkg_lua_script *lscript;
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;

	while ((cur = ucl_iterate_object(obj, &it, true))) {
		if (ucl_object_type(cur) != UCL_STRING) {
			pkg_emit_error("lua scripts be strings");
			return (EPKG_FATAL);
		}
		lscript = xcalloc(1, sizeof(*lscript));
		lscript->script = xstrdup(ucl_object_tostring(cur));
		DL_APPEND(pkg->lua_scripts[type], lscript);
	}
	return (EPKG_OK);
}

