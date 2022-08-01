/*
 * Copyright (c) 2021 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <sys/stat.h>

#include <atf-c.h>
#include <err.h>
#include <unistd.h>
#include <pkg.h>
#include <private/lua.h>
#include <fcntl.h>
#include <stdlib.h>

ATF_TC_WITHOUT_HEAD(readdir);
ATF_TC_WITHOUT_HEAD(stat);
ATF_TC_WITHOUT_HEAD(print_msg);
ATF_TC_WITHOUT_HEAD(execute);
ATF_TC_WITHOUT_HEAD(override);
ATF_TC_WITHOUT_HEAD(fileops);
ATF_TC_WITHOUT_HEAD(prefix_path);

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
		if (luaL_dostring(L, "test.readdir()")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"test.readdir()\"]:1: bad argument #0 to 'readdir' (pkg.readdir takes exactly one argument)\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "res = test.readdir(\".\")\nif res ~= nil then print(#res) end")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "2\n", "");
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

	close(open("testfile", O_CREAT|O_TRUNC));
	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "if test.readdir(\"testfile\") ~= nil then print(\"nil output\") end")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "res = test.readdir(\".\")\n print(#res)")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "3\n", "");
}

ATF_TC_BODY(stat, tc)
{
	int rootfd = open(getcwd(NULL, 0), O_DIRECTORY);
	lua_State *L = luaL_newstate();
	static const luaL_Reg test_lib[] = {
		{ "stat", lua_stat },
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
		if (luaL_dostring(L, "test.stat(\".\", \"plop\")")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"test.stat(\".\", \"plop\")\"]:1: bad argument #2 to 'stat' (pkg.stat takes exactly one argument)\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "test.stat()")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"test.stat()\"]:1: bad argument #0 to 'stat' (pkg.stat takes exactly one argument)\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "st = test.stat(\".\")\nprint(st.type)")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "dir\n", "");

	close(open("testfile", O_CREAT|O_TRUNC));
	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "st = test.stat(\"testfile\")\nprint(st.type)")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "reg\n", "");

	symlink("testfile", "plop");
	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "st = test.stat(\"plop\")\nprint(st.type)")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "lnk\n", "");

	lua_pushinteger(L, -1);
	lua_setglobal(L, "rootfd");
	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "st = test.stat(\".\")\nprint(st)")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "nil\n", "");
}

ATF_TC_BODY(print_msg, tc)
{
	lua_State *L = luaL_newstate();
	static const luaL_Reg test_lib[] = {
		{ "print_msg", lua_print_msg },
		{ NULL, NULL },
	};
	int fd = open("testfile", O_CREAT|O_TRUNC);
	luaL_openlibs(L);
	lua_override_ios(L, false);
	luaL_newlib(L, test_lib);
	lua_setglobal(L, "test");
	lua_pushinteger(L, fd);
	lua_setglobal(L, "msgfd");

	pid_t p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "test.print_msg()")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"test.print_msg()\"]:1: bad argument #0 to 'print_msg' (pkg.print_msg takes exactly one argument)\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "test.print_msg(1, 2)")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"test.print_msg(1, 2)\"]:1: bad argument #2 to 'print_msg' (pkg.print_msg takes exactly one argument)\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "test.print_msg(\"bla\")")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "", "");
	close(fd);
	atf_utils_compare_file("testfile", "bla\n");
}

ATF_TC_BODY(execute, tc)
{
	lua_State *L = luaL_newstate();
	static const luaL_Reg test_lib[] = {
		{ "exec", lua_exec },
		{ NULL, NULL },
	};
	luaL_openlibs(L);
	lua_override_ios(L, false);
	luaL_newlib(L, test_lib);
	lua_setglobal(L, "test");

	pid_t p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "test.exec()")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"test.exec()\"]:1: bad argument #0 to 'exec' (pkg.exec takes exactly one argument)\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "test.exec(plop)")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"test.exec(plop)\"]:1: bad argument #1 to 'exec' (table expected, got nil)\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "test.exec(plop, meh)")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"test.exec(plop, meh)\"]:1: bad argument #2 to 'exec' (pkg.exec takes exactly one argument)\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "test.exec({\"/bin/echo\", \"1\"})")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "1\n", "");
}

ATF_TC_BODY(override, tc)
{
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	lua_override_ios(L, true);

	pid_t p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "os.execute(\"/usr/bin/true\")")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"os.execute(\"/usr/bin/true\")\"]:1: os.execute not available\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "os.exit(1)")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"os.exit(1)\"]:1: os.exit not available\n", "");

	int rootfd = open(getcwd(NULL, 0), O_DIRECTORY);
	lua_pushinteger(L, rootfd);
	lua_setglobal(L, "rootfd");
	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "io.close(io.open(\"/plop\", \"w+\"))")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "", "");
	atf_utils_file_exists("plop");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "os.rename(\"/plop\", \"/bob\")")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "", "");
	atf_utils_file_exists("bob");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "os.remove(\"/bob\")\nassert(io.open(\"/bob\", \"r\"))")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"os.remove(\"/bob\")...\"]:2: /bob: No such file or directory\n", "");
}

ATF_TC_BODY(fileops, tc)
{
	char b[1024];
	int rootfd = open(getcwd(NULL, 0), O_DIRECTORY);
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	lua_pushinteger(L, rootfd);
	lua_setglobal(L, "rootfd");
	lua_override_ios(L, true);
	static const luaL_Reg test_lib[] = {
		{ "copy", lua_pkg_copy },
		{ "cmp", lua_pkg_filecmp},
		{ "symlink", lua_pkg_symlink},
		{ NULL, NULL },
	};
	luaL_newlib(L, test_lib);
	lua_setglobal(L, "test");
	pid_t p;

	FILE *f1 = fopen("test1", "w+");
	FILE *f2 = fopen("test2", "w+");
	FILE *f3 = fopen("test3", "w+");

	fputs("test", f1);
	fputs("test2", f2);
	fputs("test", f3);
	fclose(f1);
	fclose(f2);
	fclose(f3);

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "test.cmp(1)")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"test.cmp(1)\"]:1: bad argument #1 to 'cmp' (pkg.filecmp takes exactly two arguments)\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "test.cmp(1, 2, 3)")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"test.cmp(1, 2, 3)\"]:1: bad argument #3 to 'cmp' (pkg.filecmp takes exactly two arguments)\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "return test.cmp(1, 2)")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 2, "", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "return test.cmp(\"test1\", 2)")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 2, "", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "return test.cmp(\"test1\", \"test2\")")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 1, "", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "return test.cmp(\"test1\", \"test3\")")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "return(test.copy(1, 2))")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 2, "", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "return(test.copy(\"test1\", \"nonexistent/2\"))")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 2, "", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "test.copy(\"test1\", \"test4\")\nreturn test.cmp(\"test1\", \"test4\")")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "test.symlink(\"a\", \"b\", \"meh\")\n")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"test.symlink(\"a\", \"b\", \"meh\")...\"]:1: bad argument #3 to 'symlink' (pkg.symlink takes exactly two arguments)\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "test.symlink(\"a\")\n")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"test.symlink(\"a\")...\"]:1: bad argument #1 to 'symlink' (pkg.symlink takes exactly two arguments)\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "test.symlink(\"a\", \"b\")\n")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "", "");
	struct stat st;
	if (lstat("b", &st) != 0)
		atf_tc_fail("File 'b' not created");
	if (!S_ISLNK(st.st_mode))
		atf_tc_fail("File 'b' is not a symlink");
	memset(b, 0, sizeof(b));
	readlink("b", b, sizeof(b));
	ATF_REQUIRE_STREQ(b, "a");

}

ATF_TC_BODY(prefix_path, tc)
{
	struct pkg *pkg = NULL;
	pkg_new(&pkg, PKG_INSTALLED);
	pkg_set(pkg, PKG_PREFIX, "/myprefix");
	lua_State *L = luaL_newstate();
	static const luaL_Reg test_lib[] = {
		{ "prefix_path", lua_prefix_path },
		{ NULL, NULL },
	};
	luaL_openlibs(L);
	lua_override_ios(L, false);
	luaL_newlib(L, test_lib);
	lua_setglobal(L, "test");
	lua_pushlightuserdata(L, pkg);
	lua_setglobal(L, "package");
	pid_t p;

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "print(test.prefix_path())")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"print(test.prefix_path())\"]:1: bad argument #0 to 'prefix_path' (pkg.prefix_path takes exactly one argument)\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "print(test.prefix_path(1, 2))")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "[string \"print(test.prefix_path(1, 2))\"]:1: bad argument #2 to 'prefix_path' (pkg.prefix_path takes exactly one argument)\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "print(test.prefix_path(1))")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "/myprefix/1\n", "");

	p = atf_utils_fork();
	if (p == 0) {
		if (luaL_dostring(L, "print(test.prefix_path(\"/1\"))")) {
			printf("%s\n", lua_tostring(L, -1));
		}
		exit(lua_tonumber(L, -1));
	}
	atf_utils_wait(p, 0, "/1\n", "");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, readdir);
	ATF_TP_ADD_TC(tp, stat);
	ATF_TP_ADD_TC(tp, print_msg);
	ATF_TP_ADD_TC(tp, execute);
	ATF_TP_ADD_TC(tp, override);
	ATF_TP_ADD_TC(tp, fileops);
	ATF_TP_ADD_TC(tp, prefix_path);

	return (atf_no_error());
}
