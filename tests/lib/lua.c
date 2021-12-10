/*
 * Copyright (c) 2021 Baptiste Daroussin <bapt@FreeBSD.org
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

#include <atf-c.h>
#include <err.h>
#include <unistd.h>
#include <pkg.h>
#include <private/lua.h>
#include <fcntl.h>
#include <stdlib.h>

ATF_TC(readdir);

ATF_TC_HEAD(readdir, tc)
{
	atf_tc_set_md_var(tc, "descr", "Testing for lua functions");

}

ATF_TC_BODY(readdir, tc)
{
	int rootfd = open(getcwd(NULL, 0), O_DIRECTORY);
	lua_State *L = luaL_newstate();
	static const luaL_Reg test_lib[] = {
		{ "readdir", lua_readdir },
		{ NULL, NULL },
	};
	luaL_openlibs(L);
	lua_override_ios(L, false);
	luaL_newlib(L, test_lib);
	lua_setglobal(L, "test");
	lua_pushinteger(L, rootfd);
	lua_setglobal(L, "rootfd");

	pid_t p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "test.readdir(\".\", \"plop\")")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"test.readdir(\".\", \"plop\")\"]:1: bad argument #2 to 'readdir' (pkg.readdir takes exactly one argument)\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "if test.readdir(\".\") == nil then print(\"nil output\") end")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "", "");
	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "if test.readdir(\"nonexistent\") ~= nil then print(\"non nil output\") end")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "", "");
	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "if test.readdir(\"/\") ~= nil then print(\"nil output\") end")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "", "");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, readdir);

	return (atf_no_error());
}
