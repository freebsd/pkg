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
#include <fcntl.h>
#include <pkg.h>
#include <private/pkg.h>
#include <stdlib.h>
#include <xmalloc.h>

ATF_TC_WITHOUT_HEAD(badcommand);
ATF_TC_WITHOUT_HEAD(badrestrict);
ATF_TC_WITHOUT_HEAD(getfile);
ATF_TC_WITHOUT_HEAD(notrestricted);
ATF_TC_WITHOUT_HEAD(restricted);

ATF_TC_BODY(badcommand, tc)
{
	char strout[] =
		"ok: pkg " PKGVERSION "\n"
		"ko: unknown command 'plop'\n";
	int rootfd = open(getcwd(NULL, 0), O_DIRECTORY);
	int stdin_pipe[2];
	ATF_REQUIRE(pipe(stdin_pipe) >= 0);
	pid_t p = atf_utils_fork();
	if (p == 0) {
		close(STDIN_FILENO);
		dup2(stdin_pipe[0], STDIN_FILENO);
		close(stdin_pipe[1]);
		exit(pkg_sshserve(rootfd));
	}
	close(stdin_pipe[0]);
	dprintf(stdin_pipe[1], "plop\n");
	dprintf(stdin_pipe[1], "quit\n");
	atf_utils_wait(p, 0, strout, "");
}

ATF_TC_BODY(getfile, tc)
{
	extern ucl_object_t *config;
	struct stat st;
	char strout[] =
		"ok: pkg " PKGVERSION "\n"
		"ko: bad command get, expecting 'get file age'\n"
		"ko: bad command get, expecting 'get file age'\n"
		"ok: 12\n"
		"testcontent\n"
		"ok: 0\n"
		"ko: bad number plop: invalid\n"
		"ko: file not found\n"
		"ko: not a file\n"
		"ko: file not found\n";
	int rootfd = open(getcwd(NULL, 0), O_DIRECTORY);
	int stdin_pipe[2];
	ATF_REQUIRE(pipe(stdin_pipe) >= 0);
	FILE *f = fopen("testfile", "w+");
	ATF_CHECK(f != NULL);
	fputs("testcontent\n", f);
	fclose(f);
	ATF_CHECK(stat("testfile", &st) >= 0);
	pid_t p = atf_utils_fork();
	if (p == 0) {
		close(STDIN_FILENO);
		dup2(stdin_pipe[0], STDIN_FILENO);
		close(stdin_pipe[1]);
		config = ucl_object_typed_new(UCL_OBJECT);
		ucl_object_insert_key(config, ucl_object_fromstring_common(getcwd(NULL, 0), 0, UCL_STRING_TRIM),
			"SSH_RESTRICT_DIR", 16, false);
		exit(pkg_sshserve(rootfd));
	}
	close(stdin_pipe[0]);
	dprintf(stdin_pipe[1], "get \n");
	/* get a file without stating the age, should fail */
	dprintf(stdin_pipe[1], "get /testfile\n");
	dprintf(stdin_pipe[1], "get /testfile 0\n");
	/* get a file already in cache */
	dprintf(stdin_pipe[1], "get /testfile %ld\n", st.st_mtime);
	/* get a file with a bad age specified */
	dprintf(stdin_pipe[1], "get /testfile plop\n");
	dprintf(stdin_pipe[1], "get /nonexistent 0\n");
	mkdir("test", 0755);
	dprintf(stdin_pipe[1], "get test 0\n");
	dprintf(stdin_pipe[1], "get %s/Makefile.autosetup 0\n", atf_tc_get_config_var(tc, "srcdir"));
	dprintf(stdin_pipe[1], "quit\n");
	atf_utils_wait(p, 0, strout, "");
}

ATF_TC_BODY(badrestrict, tc)
{
	extern ucl_object_t *config;
	char strout[] =
		"ok: pkg " PKGVERSION "\n"
		"ko: chdir failed (/nonexistent)\n";
	int rootfd = open(getcwd(NULL, 0), O_DIRECTORY);
	int stdin_pipe[2];
	ATF_REQUIRE(pipe(stdin_pipe) >= 0);
	pid_t p = atf_utils_fork();
	if (p == 0) {
		close(STDIN_FILENO);
		dup2(stdin_pipe[0], STDIN_FILENO);
		close(stdin_pipe[1]);
		config = ucl_object_typed_new(UCL_OBJECT);
		ucl_object_insert_key(config, ucl_object_fromstring_common("/nonexistent", 0, UCL_STRING_TRIM),
			"SSH_RESTRICT_DIR", 16, false);
		exit(pkg_sshserve(rootfd));
	}
	close(stdin_pipe[0]);
	dprintf(stdin_pipe[1], "get /testfile 0\n");
	dprintf(stdin_pipe[1], "quit\n");
	atf_utils_wait(p, 0, strout, "");
}

ATF_TC_BODY(notrestricted, tc)
{
	struct stat st;
	char strout[] =
		"ok: pkg " PKGVERSION "\n"
		"ok: 12\n"
		"testcontent\n";
	 mkdir("test", 0755);
	int rootfd = open("test", O_DIRECTORY);
	int stdin_pipe[2];
	ATF_REQUIRE(pipe(stdin_pipe) >= 0);
	FILE *f = fopen("testfile", "w+");
	ATF_CHECK(f != NULL);
	fputs("testcontent\n", f);
	fclose(f);
	ATF_CHECK(stat("testfile", &st) >= 0);
	pid_t p = atf_utils_fork();
	if (p == 0) {
		close(STDIN_FILENO);
		dup2(stdin_pipe[0], STDIN_FILENO);
		close(stdin_pipe[1]);
		exit(pkg_sshserve(rootfd));
	}
	close(stdin_pipe[0]);
	dprintf(stdin_pipe[1], "get ../testfile 0\n");
	dprintf(stdin_pipe[1], "quit\n");
	atf_utils_wait(p, 0, strout, "");
}

ATF_TC_BODY(restricted, tc)
{
	extern ucl_object_t *config;
	struct stat st;
	char strout[] =
		"ok: pkg " PKGVERSION "\n"
		"ko: file not found\n";
	 mkdir("test", 0755);
	int rootfd = open("test", O_DIRECTORY);
	int stdin_pipe[2];
	ATF_REQUIRE(pipe(stdin_pipe) >= 0);
	FILE *f = fopen("testfile", "w+");
	ATF_CHECK(f != NULL);
	fputs("testcontent\n", f);
	fclose(f);
	ATF_CHECK(stat("testfile", &st) >= 0);
	pid_t p = atf_utils_fork();
	if (p == 0) {
		close(STDIN_FILENO);
		dup2(stdin_pipe[0], STDIN_FILENO);
		close(stdin_pipe[1]);
		config = ucl_object_typed_new(UCL_OBJECT);
		char *restriteddir;
		xasprintf(&restriteddir, "%s/test", getcwd(NULL, 0));
		ucl_object_insert_key(config, ucl_object_fromstring_common(restriteddir, 0, UCL_STRING_TRIM),
			"SSH_RESTRICT_DIR", 16, false);
		exit(pkg_sshserve(rootfd));
	}
	close(stdin_pipe[0]);
	dprintf(stdin_pipe[1], "get ../testfile 0\n");
	dprintf(stdin_pipe[1], "quit\n");
	atf_utils_wait(p, 0, strout, "");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, badcommand);
	ATF_TP_ADD_TC(tp, getfile);
	ATF_TP_ADD_TC(tp, badrestrict);
	ATF_TP_ADD_TC(tp, notrestricted);
	ATF_TP_ADD_TC(tp, restricted);

	return (atf_no_error());
}
