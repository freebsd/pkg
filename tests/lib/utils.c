/*-
 * Copyright (c) 2022 Baptiste Daroussin <bapt@FreeBSD.org>
 *~
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *~
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
#include <atf-c/macros.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <private/utils.h>
#include <private/add.h>
#include <private/pkg.h>
#include <private/pkg_abi.h>

ATF_TC_WITHOUT_HEAD(hidden_tempfile);
ATF_TC_WITHOUT_HEAD(random_suffix);
ATF_TC_WITHOUT_HEAD(json_escape);
ATF_TC_WITHOUT_HEAD(open_tempdir);
ATF_TC_WITHOUT_HEAD(get_http_auth);
ATF_TC_WITHOUT_HEAD(str_ends_with);
ATF_TC_WITHOUT_HEAD(match_paths);
ATF_TC_WITHOUT_HEAD(arch_to_legacy);
ATF_TC_WITHOUT_HEAD(safe_relative_path);
ATF_TC_WITHOUT_HEAD(abi_from_string);
ATF_TC_WITHOUT_HEAD(resolve_beneath);
ATF_TC_WITHOUT_HEAD(tokenize_count);

ATF_TC_BODY(hidden_tempfile, tc) {
	const char *filename = "plop";
	const char *longfile = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA.php.gif";
	const char *pathfn = "/tmp/plop";
	const char *pathlongfn = "/tmp/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA.php.gif";
	char buf[MAXPATHLEN];

	hidden_tempfile(buf, MAXPATHLEN, filename);
	ATF_REQUIRE_EQ_MSG(strncmp(buf, ".pkgtemp.plop.", 14), 0, "bad filename '%s'", buf);
	hidden_tempfile(buf, MAXPATHLEN, longfile);
	ATF_REQUIRE_EQ_MSG(strncmp(buf, ".AAA", 4), 0, "bad filename '%s'", buf);

	hidden_tempfile(buf, MAXPATHLEN, pathfn);
	ATF_REQUIRE_EQ_MSG(strncmp(buf, "/tmp/.pkgtemp.plop.", 19), 0, "bad filename '%s'", buf);

	hidden_tempfile(buf, MAXPATHLEN, pathlongfn);
	ATF_REQUIRE_EQ_MSG(strncmp(buf, "/tmp/.AAA", 9), 0, "bad filename '%s'", buf);


}
ATF_TC_BODY(random_suffix, tc) {
	char buf[14];

	buf[0] = '\0';
	append_random_suffix(buf, sizeof(buf), 12);
	ATF_REQUIRE_EQ_MSG(strlen(buf), 13, "suffix not long enough %lu", strlen(buf));
	snprintf(buf, sizeof(buf), "filename");
	append_random_suffix(buf, sizeof(buf), 12);
	ATF_REQUIRE_EQ_MSG(strlen(buf), 13, "suffix not long enough %lu", strlen(buf));
}

ATF_TC_BODY(json_escape, tc) {
	char *m = json_escape("entry1\"\"\\ ");
	ATF_REQUIRE_STREQ_MSG(m, "entry1\\\"\\\"\\\\ ", "Invalid escaping");
	free(m);
}

ATF_TC_BODY(open_tempdir, tc) {
	struct pkg_add_context ctx = { 0 };
	struct tempdir *t;
	ctx.rootfd = open(getenv("TMPDIR"), O_DIRECTORY);
	ATF_REQUIRE_MSG(ctx.rootfd  != -1, "impossible to open TMPDIR");
	t = open_tempdir(&ctx, "/plop");
	ATF_REQUIRE(t == NULL);
	mkdirat(ctx.rootfd, "usr", 0755);
	t = open_tempdir(&ctx, "/usr/local/directory");
	ATF_REQUIRE(t != NULL);
	ATF_REQUIRE_STREQ(t->name, "/usr/local");
	ATF_REQUIRE_EQ(t->len, strlen("/usr/local"));
	ATF_REQUIRE(strncmp(t->temp, "/usr/.pkgtemp.", 14) == 0);
	ATF_REQUIRE(t->fd != -1);
	close(t->fd);
	free(t);
	t = open_tempdir(&ctx, "/nousr/local/directory");
	ATF_REQUIRE(t != NULL);
	ATF_REQUIRE_STREQ(t->name, "/nousr");
	ATF_REQUIRE_EQ(t->len, strlen("/nousr"));
	ATF_REQUIRE(strncmp(t->temp, "/.pkgtemp.", 10) == 0);
	ATF_REQUIRE(t->fd != -1);
	close(t->fd);
	free(t);
	mkdirat(ctx.rootfd, "dir", 0755);
	/* a file in the path */
	close(openat(ctx.rootfd, "dir/file1", O_CREAT|O_WRONLY, 0644));
	t = open_tempdir(&ctx, "/dir/file1/test");
	ATF_REQUIRE(t != NULL);
	ATF_REQUIRE_STREQ(t->name, "/dir/file1");
	ATF_REQUIRE_EQ(t->len, strlen("/dir/file1"));
	ATF_REQUIRE(strncmp(t->temp, "/dir/.pkgtemp.", 14) == 0);
	ATF_REQUIRE(t->fd != -1);
	close(t->fd);
	free(t);
}

ATF_TC_BODY(get_http_auth, tc) {
	unsetenv("HTTP_AUTH");
	ATF_REQUIRE(get_http_auth() == NULL);
	setenv("HTTP_AUTH", "plop", 1);
	ATF_REQUIRE(get_http_auth() == NULL);

	setenv("HTTP_AUTH", "basic:any", 1);
	ATF_REQUIRE(get_http_auth() == NULL);

	setenv("HTTP_AUTH", "basic:any:user", 1);
	ATF_REQUIRE(get_http_auth() == NULL);

	setenv("HTTP_AUTH", "basic:any:user:passwd", 1);
	ATF_REQUIRE_STREQ(get_http_auth(), "user:passwd");
}

ATF_TC_BODY(str_ends_with, tc) {
	ATF_REQUIRE(str_ends_with(NULL, NULL));
	ATF_REQUIRE(!str_ends_with(NULL, "end"));
	ATF_REQUIRE(!str_ends_with("a", "end"));
	ATF_REQUIRE(str_ends_with("end", "end"));
	ATF_REQUIRE(str_ends_with("backend", "end"));
}

ATF_TC_BODY(match_paths, tc) {
	const char *paths[] = {
		"////",
		"/foo1",
		"/foo2/",
		"////foo3/bar",
		"/foo4//bar",
		"/foo5//////bar",
		"//foo6//bar/",
		"/foo7//////bar/",
		"////foo8//bar///",
		NULL,
	};

	ucl_object_t *list = ucl_object_typed_new(UCL_ARRAY);
	for (size_t i = 0; paths[i] != NULL; i++) {
		ucl_array_append(list, ucl_object_fromstring_common(paths[i], 0, 0));
	}

	ATF_REQUIRE(pkg_match_paths_list(list, "/target.so"));
	ATF_REQUIRE(pkg_match_paths_list(list, "/foo1/whatever"));
	ATF_REQUIRE(pkg_match_paths_list(list, "/foo2/thing.txt"));
	ATF_REQUIRE(pkg_match_paths_list(list, "/foo3/bar/baz.so.1.1.1"));
	ATF_REQUIRE(pkg_match_paths_list(list, "////foo4//bar/thingy"));
	ATF_REQUIRE(pkg_match_paths_list(list, "/foo5//////bar/whatisit"));
	ATF_REQUIRE(pkg_match_paths_list(list, "/foo6//bar/afile"));
	ATF_REQUIRE(pkg_match_paths_list(list, "/foo7//////bar/foooo"));
	ATF_REQUIRE(pkg_match_paths_list(list, "/foo8//bar///other"));

	ATF_REQUIRE(!pkg_match_paths_list(list, "/notinpath/target.so"));
	ATF_REQUIRE(!pkg_match_paths_list(list, "//////notinpath////other.so.1"));
	ATF_REQUIRE(!pkg_match_paths_list(list, "/a/b/c/d/e/f/g"));
}

ATF_TC_BODY(arch_to_legacy, tc) {
	char dest[32];
	char longabi[4096];

	ATF_REQUIRE_EQ(pkg_arch_to_legacy("FreeBSD:14:amd64", dest,
	    sizeof(dest)), 0);
	ATF_REQUIRE_STREQ(dest, "freebsd:14:x86:64");
	ATF_REQUIRE_EQ(pkg_arch_to_legacy("FreeBSD:14:whatever", dest,
	    sizeof(dest)), 0);
	ATF_REQUIRE_STREQ(dest, "freebsd:14:whatever");
	ATF_REQUIRE_EQ(pkg_arch_to_legacy("FreeBSD", dest, sizeof(dest)), 0);
	ATF_REQUIRE_STREQ(dest, "freebsd");

	/* The result must fit exactly, one byte less is an overflow */
	ATF_REQUIRE_EQ(pkg_arch_to_legacy("FreeBSD:14:amd64", dest,
	    strlen("freebsd:14:x86:64") + 1), 0);
	ATF_REQUIRE_STREQ(dest, "freebsd:14:x86:64");
	ATF_REQUIRE_EQ(pkg_arch_to_legacy("FreeBSD:14:amd64", dest,
	    strlen("freebsd:14:x86:64")), EPKG_FATAL);
	ATF_REQUIRE_STREQ(dest, "");

	/* Overlong OS, version and arch components must be rejected */
	memset(longabi, 'A', sizeof(longabi) - 1);
	longabi[sizeof(longabi) - 1] = '\0';
	ATF_REQUIRE_EQ(pkg_arch_to_legacy(longabi, dest, sizeof(dest)),
	    EPKG_FATAL);
	ATF_REQUIRE_STREQ(dest, "");
	snprintf(longabi, sizeof(longabi), "FreeBSD:%0*d:amd64", 100, 0);
	ATF_REQUIRE_EQ(pkg_arch_to_legacy(longabi, dest, sizeof(dest)),
	    EPKG_FATAL);
	ATF_REQUIRE_STREQ(dest, "");
	snprintf(longabi, sizeof(longabi), "FreeBSD:14:%0*d", 100, 0);
	ATF_REQUIRE_EQ(pkg_arch_to_legacy(longabi, dest, sizeof(dest)),
	    EPKG_FATAL);
	ATF_REQUIRE_STREQ(dest, "");
}

ATF_TC_BODY(safe_relative_path, tc) {
	ATF_REQUIRE(pkg_is_safe_relative_path("All/foo-1.0.pkg"));
	ATF_REQUIRE(pkg_is_safe_relative_path("foo-1.0.pkg"));
	ATF_REQUIRE(pkg_is_safe_relative_path("All/..foo/.hidden/a..b"));
	ATF_REQUIRE(pkg_is_safe_relative_path("All/./foo.pkg"));

	ATF_REQUIRE(!pkg_is_safe_relative_path(NULL));
	ATF_REQUIRE(!pkg_is_safe_relative_path(""));
	ATF_REQUIRE(!pkg_is_safe_relative_path("/etc/rc.local"));
	ATF_REQUIRE(!pkg_is_safe_relative_path(".."));
	ATF_REQUIRE(!pkg_is_safe_relative_path("../foo.pkg"));
	ATF_REQUIRE(!pkg_is_safe_relative_path("All/../../etc/rc.local"));
	ATF_REQUIRE(!pkg_is_safe_relative_path("All/x.pkg/.."));
	ATF_REQUIRE(!pkg_is_safe_relative_path("All//../x.pkg"));
}

ATF_TC_BODY(abi_from_string, tc) {
	struct pkg_abi abi;

	ATF_REQUIRE(pkg_abi_from_string(&abi, "FreeBSD:14:amd64"));
	ATF_REQUIRE_EQ(abi.os, PKG_OS_FREEBSD);
	ATF_REQUIRE_EQ(abi.major, 14);
	ATF_REQUIRE_EQ(abi.minor, 0);
	ATF_REQUIRE(!pkg_abi_from_string(&abi, "FreeBSD:abc:amd64"));

	/* OSes with a minor version must parse both parts */
	ATF_REQUIRE(pkg_abi_from_string(&abi, "dragonfly:6.4:x86:64"));
	ATF_REQUIRE_EQ(abi.os, PKG_OS_DRAGONFLY);
	ATF_REQUIRE_EQ(abi.major, 6);
	ATF_REQUIRE_EQ(abi.minor, 4);
	ATF_REQUIRE(!pkg_abi_from_string(&abi, "dragonfly:abc.4:x86:64"));
	ATF_REQUIRE(!pkg_abi_from_string(&abi, "dragonfly:6.abc:x86:64"));
	ATF_REQUIRE(!pkg_abi_from_string(&abi, "dragonfly:6:x86:64"));
}

ATF_TC_BODY(resolve_beneath, tc) {
	char dest[MAXPATHLEN];
	char rootpath[MAXPATHLEN];
	char target[MAXPATHLEN];
	char cwd[MAXPATHLEN];
	int rootfd;

	ATF_REQUIRE(mkdir("root", 0755) == 0);
	ATF_REQUIRE(mkdir("root/a", 0755) == 0);
	ATF_REQUIRE(mkdir("root/a/b", 0755) == 0);
	ATF_REQUIRE(symlink("/etc", "root/a/abs") == 0);
	ATF_REQUIRE(symlink("b", "root/a/rel") == 0);
	ATF_REQUIRE(symlink("../a/b", "root/a/sideways") == 0);
	ATF_REQUIRE(symlink("../../..", "root/a/up") == 0);
	ATF_REQUIRE(symlink("rel", "root/a/chain") == 0);
	ATF_REQUIRE(symlink("abs", "root/a/badchain") == 0);
	ATF_REQUIRE(symlink("loop2", "root/a/loop1") == 0);
	ATF_REQUIRE(symlink("loop1", "root/a/loop2") == 0);
	rootfd = open("root", O_DIRECTORY);
	ATF_REQUIRE(rootfd != -1);

	/* no root directory in use: the path is only made relative */
	ctx.pkg_rootdir = NULL;
	ATF_REQUIRE_EQ(EPKG_OK,
	    pkg_resolve_beneath(rootfd, "/a/abs/file", dest, sizeof(dest)));
	ATF_REQUIRE_STREQ("/a/abs/file", dest);

	ctx.pkg_rootdir = "root";

	/* plain directories and symlinks staying below the root */
	ATF_REQUIRE_EQ(EPKG_OK,
	    pkg_resolve_beneath(rootfd, "/a/b/file", dest, sizeof(dest)));
	ATF_REQUIRE_STREQ("/a/b/file", dest);
	ATF_REQUIRE_EQ(EPKG_OK,
	    pkg_resolve_beneath(rootfd, "/a/rel/file", dest, sizeof(dest)));
	ATF_REQUIRE_STREQ("/a/rel/file", dest);
	ATF_REQUIRE_EQ(EPKG_OK,
	    pkg_resolve_beneath(rootfd, "/a/sideways/x/file", dest, sizeof(dest)));
	ATF_REQUIRE_STREQ("/a/sideways/x/file", dest);
	ATF_REQUIRE_EQ(EPKG_OK,
	    pkg_resolve_beneath(rootfd, "/a/chain/file", dest, sizeof(dest)));
	ATF_REQUIRE_STREQ("/a/chain/file", dest);
	ATF_REQUIRE_EQ(EPKG_OK,
	    pkg_resolve_beneath(rootfd, "/a/missing/dir/file", dest,
	    sizeof(dest)));
	ATF_REQUIRE_STREQ("/a/missing/dir/file", dest);
	/* the last component is never resolved */
	ATF_REQUIRE_EQ(EPKG_OK,
	    pkg_resolve_beneath(rootfd, "/a/abs", dest, sizeof(dest)));
	ATF_REQUIRE_STREQ("/a/abs", dest);

	/* an absolute target is resolved from the root, never from "/" */
	ATF_REQUIRE_EQ(EPKG_OK,
	    pkg_resolve_beneath(rootfd, "/a/abs/file", dest, sizeof(dest)));
	ATF_REQUIRE_STREQ("/etc/file", dest);
	ATF_REQUIRE_EQ(EPKG_OK,
	    pkg_resolve_beneath(rootfd, "/a/badchain/file", dest, sizeof(dest)));
	ATF_REQUIRE_STREQ("/etc/file", dest);
	/* climbing above the root stays at the root */
	ATF_REQUIRE_EQ(EPKG_OK,
	    pkg_resolve_beneath(rootfd, "/a/up/file", dest, sizeof(dest)));
	ATF_REQUIRE_STREQ("/file", dest);
	ATF_REQUIRE_EQ(EPKG_OK,
	    pkg_resolve_beneath(rootfd, "/a/up/etc/periodic/file", dest,
	    sizeof(dest)));
	ATF_REQUIRE_STREQ("/etc/periodic/file", dest);
	ATF_REQUIRE_EQ(EPKG_OK,
	    pkg_resolve_beneath(rootfd, "/a/../../file", dest, sizeof(dest)));
	ATF_REQUIRE_STREQ("/file", dest);

	/* a symlink loop is reported, not resolved */
	ATF_REQUIRE_EQ(EPKG_FATAL,
	    pkg_resolve_beneath(rootfd, "/a/loop1/file", dest, sizeof(dest)));

	/*
	 * A target which already names a path inside of the root, as pkg
	 * itself writes them, is used as is instead of being re-rooted.
	 */
	ATF_REQUIRE(realpath("root", rootpath) != NULL);
	ATF_REQUIRE(snprintf(target, sizeof(target), "%s/a/b", rootpath) <
	    (int)sizeof(target));
	ATF_REQUIRE(symlink(target, "root/a/inside") == 0);
	ctx.pkg_rootdir = rootpath;
	ATF_REQUIRE_EQ(EPKG_OK, pkg_resolve_beneath(rootfd, "/a/inside/file",
	    dest, sizeof(dest)));
	ATF_REQUIRE_STREQ("/a/b/file", dest);

	/*
	 * The root is canonical but a target naming a path inside of it can
	 * be spelled through a symlink: it is the canonical form of the
	 * target which is compared.
	 */
	ATF_REQUIRE(getcwd(cwd, sizeof(cwd)) != NULL);
	ATF_REQUIRE(symlink(rootpath, "rootlink") == 0);
	ATF_REQUIRE(snprintf(target, sizeof(target), "%s/rootlink/a/b", cwd) <
	    (int)sizeof(target));
	ATF_REQUIRE(symlink(target, "root/a/noncanon") == 0);
	ATF_REQUIRE_EQ(EPKG_OK, pkg_resolve_beneath(rootfd, "/a/noncanon/file",
	    dest, sizeof(dest)));
	ATF_REQUIRE_STREQ("/a/b/file", dest);

	close(rootfd);
	ctx.pkg_rootdir = NULL;
}

ATF_TC_BODY(tokenize_count, tc) {
	const char *lines[] = { "a b c", "\"a\"\"b\"", "\"a\"b'c'", "'a''b''c'",
	    "  a  ", "\"\"\"\"" };

	for (size_t i = 0; i < NELEM(lines); i++) {
		char *buf = xstrdup(lines[i]), *walk = buf;
		int ntok = 0;

		while (walk != NULL) {
			pkg_utils_tokenize(&walk);
			ntok++;
		}
		ATF_REQUIRE_MSG(ntok <= pkg_utils_count_spaces(lines[i]) + 1,
		    "%s: %d tokens", lines[i], ntok);
		free(buf);
	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, hidden_tempfile);
	ATF_TP_ADD_TC(tp, random_suffix);
	ATF_TP_ADD_TC(tp, json_escape);
	ATF_TP_ADD_TC(tp, open_tempdir);
	ATF_TP_ADD_TC(tp, get_http_auth);
	ATF_TP_ADD_TC(tp, str_ends_with);
	ATF_TP_ADD_TC(tp, match_paths);
	ATF_TP_ADD_TC(tp, arch_to_legacy);
	ATF_TP_ADD_TC(tp, safe_relative_path);
	ATF_TP_ADD_TC(tp, abi_from_string);
	ATF_TP_ADD_TC(tp, resolve_beneath);
	ATF_TP_ADD_TC(tp, tokenize_count);

	return (atf_no_error());
}
