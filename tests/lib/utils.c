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

#include <atf-c.h>
#include <atf-c/macros.h>
#include <err.h>
#include <fcntl.h>
#include <private/utils.h>
#include <private/add.h>

ATF_TC_WITHOUT_HEAD(hidden_tempfile);
ATF_TC_WITHOUT_HEAD(random_suffix);
ATF_TC_WITHOUT_HEAD(json_escape);
ATF_TC_WITHOUT_HEAD(open_tempdir);
ATF_TC_WITHOUT_HEAD(get_http_auth);
ATF_TC_WITHOUT_HEAD(str_ends_with);
ATF_TC_WITHOUT_HEAD(match_paths);

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

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, hidden_tempfile);
	ATF_TP_ADD_TC(tp, random_suffix);
	ATF_TP_ADD_TC(tp, json_escape);
	ATF_TP_ADD_TC(tp, open_tempdir);
	ATF_TP_ADD_TC(tp, get_http_auth);
	ATF_TP_ADD_TC(tp, str_ends_with);
	ATF_TP_ADD_TC(tp, match_paths);

	return (atf_no_error());
}
