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
#include <err.h>
#include <fcntl.h>
#include <private/utils.h>

ATF_TC_WITHOUT_HEAD(hidden_tempfile);
ATF_TC_WITHOUT_HEAD(random_suffix);
ATF_TC_WITHOUT_HEAD(json_escape);
ATF_TC_WITHOUT_HEAD(open_tempdir);

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
}

ATF_TC_BODY(open_tempdir, tc) {
	struct tempdir *t;
	int rootfd = open(getenv("TMPDIR"), O_DIRECTORY);
	ATF_REQUIRE_MSG(rootfd  != -1, "impossible to open TMPDIR");
	t = open_tempdir(rootfd, "/plop");
	ATF_REQUIRE(t == NULL);
	mkdirat(rootfd, "usr", 0755);
	t = open_tempdir(rootfd, "/usr/local/directory");
	ATF_REQUIRE(t != NULL);
	ATF_REQUIRE_STREQ(t->name, "/usr/local");
	ATF_REQUIRE_EQ(t->len, strlen("/usr/local"));
	ATF_REQUIRE(strncmp(t->temp, "/usr/.pkgtemp.", 14) == 0);
	ATF_REQUIRE(t->fd != -1);
	close(t->fd);
	free(t);
	t = open_tempdir(rootfd, "/nousr/local/directory");
	ATF_REQUIRE(t != NULL);
	ATF_REQUIRE_STREQ(t->name, "/nousr");
	ATF_REQUIRE_EQ(t->len, strlen("/nousr"));
	ATF_REQUIRE(strncmp(t->temp, "/.pkgtemp.", 10) == 0);
	ATF_REQUIRE(t->fd != -1);
	close(t->fd);
	free(t);
	mkdirat(rootfd, "dir", 0755);
	/* a file in the path */
	close(openat(rootfd, "dir/file1", O_CREAT|O_WRONLY, 0644));
	t = open_tempdir(rootfd, "/dir/file1/test");
	ATF_REQUIRE(t != NULL);
	ATF_REQUIRE_STREQ(t->name, "/dir/file1");
	ATF_REQUIRE_EQ(t->len, strlen("/dir/file1"));
	ATF_REQUIRE(strncmp(t->temp, "/dir/.pkgtemp.", 14) == 0);
	ATF_REQUIRE(t->fd != -1);
	close(t->fd);
	free(t);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, hidden_tempfile);
	ATF_TP_ADD_TC(tp, random_suffix);
	ATF_TP_ADD_TC(tp, json_escape);
	ATF_TP_ADD_TC(tp, open_tempdir);

	return (atf_no_error());
}
