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

#include <errno.h>
#include <utstring.h>
#include <lauxlib.h>
#include <lualib.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

extern char **environ;

static lua_CFunction
stack_dump(lua_State *L)
{
	int i;
	int top = lua_gettop(L);
	UT_string *stack;

	utstring_new(stack);

	utstring_printf(stack, "\nLua Stack\n---------\n");
	utstring_printf(stack, "\tType   Data\n\t-----------\n" );

	for (i = 1; i <= top; i++) {  /* repeat for each level */
		int t = lua_type(L, i);
		utstring_printf(stack, "%i", i);
		switch (t) {
		case LUA_TSTRING:  /* strings */
			utstring_printf(stack, "\tString: `%s'\n", lua_tostring(L, i));
			break;
		case LUA_TBOOLEAN:  /* booleans */
			utstring_printf(stack, "\tBoolean: %s", lua_toboolean(L, i) ? "\ttrue\n" : "\tfalse\n");
			break;
		case LUA_TNUMBER:  /* numbers */
			utstring_printf(stack, "\tNumber: %g\n", lua_tonumber(L, i));
			break;
		default:  /* other values */
			utstring_printf(stack, "\tOther: %s\n", lua_typename(L, t));
			break;
		}
	}
	pkg_emit_error("%s\n", utstring_body(stack));
	utstring_free(stack);

	return (0);
}

static int
lua_print_msg(lua_State *L)
{
	const char* str = luaL_checkstring(L, 1);

	pkg_emit_message(str);
	pkg_emit_message("\n");
	return (0);
}

static int
lua_prefix_path(lua_State *L)
{
	const char *str = luaL_checkstring(L, 1);
	lua_getglobal(L, "package");
	struct pkg *p = lua_touserdata(L, -1);

	char path[MAXPATHLEN];
	path[0] = '\0';

	if (ctx.pkg_rootdir != NULL && strcmp(ctx.pkg_rootdir, "/") != 0)
		strlcat(path, ctx.pkg_rootdir, MAXPATHLEN);
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
pkg_lua_script_run(struct pkg * const pkg, pkg_lua_script type)
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
		pid_t pid = fork();
		if (pid > 0) {
			static const luaL_Reg pkg_lib[] = {
				{ "print_msg", lua_print_msg },
				{ "prefixed_path", lua_prefix_path },
				{ NULL, NULL },
			};
			lua_State *L = luaL_newstate();
			luaL_openlibs( L );
			lua_atpanic(L, (lua_CFunction)stack_dump );
			lua_pushlightuserdata(L, pkg);
			lua_setglobal(L, "package");
			lua_pushliteral(L, "PREFIX");
			lua_pushstring(L, pkg->prefix);
			lua_setglobal(L, "pkg_prefix");
			if (ctx.pkg_rootdir == NULL)
				ctx.pkg_rootdir = "/";
			lua_pushstring(L, ctx.pkg_rootdir);
			lua_setglobal(L, "pkg_rootdir");
			lua_pushcfunction(L, lua_print_msg);
			luaL_newlib(L, pkg_lib);
			lua_setglobal(L, "pkg");

			pkg_debug(3, "Scripts: executing lua\n--- BEGIN ---\n%s\nScripts: --- END ---", lscript->script);
			if (luaL_dostring(L, lscript->script)) {
				pkg_emit_error("Failed to execute lua script: %s", lua_tostring(L, -1));
			}

			lua_close(L);
		} else if (pid < 0) {
			pkg_emit_errno("Cannot fork", "lua_script");
			ret = EPKG_FATAL;
			goto cleanup;
		}

		while (waitpid(pid, &pstat, 0) == -1) {
			if (errno != EINTR) {
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

