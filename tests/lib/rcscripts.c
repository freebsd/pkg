/*-
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/stat.h>

#include <atf-c.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <private/pkg.h>
#include <xmalloc.h>
#include <pkghash.h>

ATF_TC_WITHOUT_HEAD(deferred_rc_init_free);
ATF_TC_WITHOUT_HEAD(deferred_rc_free_null);
ATF_TC_WITHOUT_HEAD(deferred_rc_stop_entries);
ATF_TC_WITHOUT_HEAD(deferred_rc_start_entries);
ATF_TC_WITHOUT_HEAD(deferred_rc_dedup);
ATF_TC_WITHOUT_HEAD(deferred_rc_tmpdir_cleanup);
ATF_TC_WITHOUT_HEAD(deferred_rc_free_reuse);
ATF_TC_WITHOUT_HEAD(deferred_rc_stop_all_null_oldpath);
ATF_TC_WITHOUT_HEAD(deferred_rc_tmpdir_multiple_scripts);
ATF_TC_WITHOUT_HEAD(deferred_rc_seen_sets_independent);
ATF_TC_WITHOUT_HEAD(deferred_rc_mixed_stop_start);

ATF_TC_BODY(deferred_rc_init_free, tc)
{
	struct deferred_rc rc;

	pkg_deferred_rc_init(&rc);
	ATF_REQUIRE_EQ(rc.tmpdir, NULL);
	ATF_REQUIRE_EQ(rc.to_stop.len, 0);
	ATF_REQUIRE_EQ(rc.to_start.len, 0);
	ATF_REQUIRE_EQ(rc.seen_stop, NULL);
	ATF_REQUIRE_EQ(rc.seen_start, NULL);

	pkg_deferred_rc_free(&rc);
	ATF_REQUIRE_EQ(rc.tmpdir, NULL);
	ATF_REQUIRE_EQ(rc.to_stop.len, 0);
	ATF_REQUIRE_EQ(rc.to_start.len, 0);
}

ATF_TC_BODY(deferred_rc_free_null, tc)
{
	pkg_deferred_rc_free(NULL);
}

ATF_TC_BODY(deferred_rc_stop_entries, tc)
{
	struct deferred_rc rc;
	struct deferred_rc_stop s;

	pkg_deferred_rc_init(&rc);

	s.name = xstrdup("sshd");
	s.oldpath = xstrdup("/tmp/fakepath/sshd");
	vec_push(&rc.to_stop, s);

	s.name = xstrdup("nginx");
	s.oldpath = NULL;
	vec_push(&rc.to_stop, s);

	ATF_REQUIRE_EQ(rc.to_stop.len, 2);
	ATF_REQUIRE_STREQ(rc.to_stop.d[0].name, "sshd");
	ATF_REQUIRE(rc.to_stop.d[0].oldpath != NULL);
	ATF_REQUIRE_STREQ(rc.to_stop.d[1].name, "nginx");
	ATF_REQUIRE_EQ(rc.to_stop.d[1].oldpath, NULL);
	ATF_REQUIRE_EQ(rc.to_start.len, 0);

	/* Clean up without unlink since paths are fake */
	free(rc.to_stop.d[0].oldpath);
	rc.to_stop.d[0].oldpath = NULL;
	pkg_deferred_rc_free(&rc);
}

ATF_TC_BODY(deferred_rc_start_entries, tc)
{
	struct deferred_rc rc;

	pkg_deferred_rc_init(&rc);

	vec_push(&rc.to_start, xstrdup("postfix"));
	vec_push(&rc.to_start, xstrdup("dovecot"));

	ATF_REQUIRE_EQ(rc.to_start.len, 2);
	ATF_REQUIRE_STREQ(rc.to_start.d[0], "postfix");
	ATF_REQUIRE_STREQ(rc.to_start.d[1], "dovecot");
	ATF_REQUIRE_EQ(rc.to_stop.len, 0);

	pkg_deferred_rc_free(&rc);
}

ATF_TC_BODY(deferred_rc_dedup, tc)
{
	struct deferred_rc rc;
	struct deferred_rc_stop s;

	pkg_deferred_rc_init(&rc);

	/* Simulate what pkg_deferred_rc_add does for dedup */
	pkghash_safe_add(rc.seen_stop, "svc", NULL, NULL);
	s.name = xstrdup("svc");
	s.oldpath = NULL;
	vec_push(&rc.to_stop, s);

	/* Second add should be detected via seen_stop */
	ATF_REQUIRE(pkghash_get(rc.seen_stop, "svc") != NULL);
	/* Don't add again — this is what pkg_deferred_rc_add checks */
	ATF_REQUIRE_EQ(rc.to_stop.len, 1);

	/* Same for start */
	pkghash_safe_add(rc.seen_start, "svc2", NULL, NULL);
	vec_push(&rc.to_start, xstrdup("svc2"));

	ATF_REQUIRE(pkghash_get(rc.seen_start, "svc2") != NULL);
	ATF_REQUIRE_EQ(rc.to_start.len, 1);

	/* Cross-lookup: seen_start used to detect upgrades in execute */
	pkghash_safe_add(rc.seen_start, "svc", NULL, NULL);
	ATF_REQUIRE(pkghash_get(rc.seen_start, "svc") != NULL);

	pkg_deferred_rc_free(&rc);
}

ATF_TC_BODY(deferred_rc_tmpdir_cleanup, tc)
{
	struct deferred_rc rc;
	struct deferred_rc_stop s;
	char tdir[] = "/tmp/pkg-test-rc.XXXXXX";
	char script_path[PATH_MAX];
	char *saved_tmpdir;
	int fd;

	ATF_REQUIRE(mkdtemp(tdir) != NULL);

	pkg_deferred_rc_init(&rc);
	rc.tmpdir = xstrdup(tdir);

	snprintf(script_path, sizeof(script_path), "%s/fakesvc", tdir);
	fd = open(script_path, O_WRONLY | O_CREAT, 0755);
	ATF_REQUIRE(fd != -1);
	write(fd, "#!/bin/sh\n", 10);
	close(fd);

	s.name = xstrdup("fakesvc");
	s.oldpath = xstrdup(script_path);
	vec_push(&rc.to_stop, s);

	saved_tmpdir = xstrdup(rc.tmpdir);
	ATF_REQUIRE(access(script_path, F_OK) == 0);

	pkg_deferred_rc_free(&rc);

	ATF_REQUIRE_EQ_MSG(access(script_path, F_OK), -1,
	    "saved rc script should have been removed");
	ATF_REQUIRE_EQ_MSG(access(saved_tmpdir, F_OK), -1,
	    "tmpdir should have been removed");

	free(saved_tmpdir);
}

ATF_TC_BODY(deferred_rc_free_reuse, tc)
{
	struct deferred_rc rc;
	struct deferred_rc_stop s;

	pkg_deferred_rc_init(&rc);

	s.name = xstrdup("sshd");
	s.oldpath = NULL;
	vec_push(&rc.to_stop, s);
	vec_push(&rc.to_start, xstrdup("nginx"));

	pkghash_safe_add(rc.seen_stop, "sshd", NULL, NULL);
	pkghash_safe_add(rc.seen_start, "nginx", NULL, NULL);

	pkg_deferred_rc_free(&rc);

	/* Re-init and reuse the same struct */
	pkg_deferred_rc_init(&rc);
	ATF_REQUIRE_EQ(rc.tmpdir, NULL);
	ATF_REQUIRE_EQ(rc.to_stop.len, 0);
	ATF_REQUIRE_EQ(rc.to_start.len, 0);
	ATF_REQUIRE_EQ(rc.seen_stop, NULL);
	ATF_REQUIRE_EQ(rc.seen_start, NULL);

	s.name = xstrdup("postfix");
	s.oldpath = NULL;
	vec_push(&rc.to_stop, s);
	ATF_REQUIRE_EQ(rc.to_stop.len, 1);
	ATF_REQUIRE_STREQ(rc.to_stop.d[0].name, "postfix");

	pkg_deferred_rc_free(&rc);
}

ATF_TC_BODY(deferred_rc_stop_all_null_oldpath, tc)
{
	struct deferred_rc rc;
	struct deferred_rc_stop s;

	pkg_deferred_rc_init(&rc);

	s.name = xstrdup("sshd");
	s.oldpath = NULL;
	vec_push(&rc.to_stop, s);

	s.name = xstrdup("nginx");
	s.oldpath = NULL;
	vec_push(&rc.to_stop, s);

	s.name = xstrdup("postfix");
	s.oldpath = NULL;
	vec_push(&rc.to_stop, s);

	ATF_REQUIRE_EQ(rc.to_stop.len, 3);
	ATF_REQUIRE_EQ(rc.to_stop.d[0].oldpath, NULL);
	ATF_REQUIRE_EQ(rc.to_stop.d[1].oldpath, NULL);
	ATF_REQUIRE_EQ(rc.to_stop.d[2].oldpath, NULL);

	/* free should handle all-NULL oldpaths without issue */
	pkg_deferred_rc_free(&rc);
}

ATF_TC_BODY(deferred_rc_tmpdir_multiple_scripts, tc)
{
	struct deferred_rc rc;
	struct deferred_rc_stop s;
	char tdir[] = "/tmp/pkg-test-rc.XXXXXX";
	char path1[PATH_MAX], path2[PATH_MAX], path3[PATH_MAX];
	int fd;

	ATF_REQUIRE(mkdtemp(tdir) != NULL);

	pkg_deferred_rc_init(&rc);
	rc.tmpdir = xstrdup(tdir);

	/* Create three fake scripts in the tmpdir */
	snprintf(path1, sizeof(path1), "%s/svc_a", tdir);
	fd = open(path1, O_WRONLY | O_CREAT, 0755);
	ATF_REQUIRE(fd != -1);
	write(fd, "#!/bin/sh\n", 10);
	close(fd);

	snprintf(path2, sizeof(path2), "%s/svc_b", tdir);
	fd = open(path2, O_WRONLY | O_CREAT, 0755);
	ATF_REQUIRE(fd != -1);
	write(fd, "#!/bin/sh\n", 10);
	close(fd);

	snprintf(path3, sizeof(path3), "%s/svc_c", tdir);
	fd = open(path3, O_WRONLY | O_CREAT, 0755);
	ATF_REQUIRE(fd != -1);
	write(fd, "#!/bin/sh\n", 10);
	close(fd);

	s.name = xstrdup("svc_a");
	s.oldpath = xstrdup(path1);
	vec_push(&rc.to_stop, s);

	s.name = xstrdup("svc_b");
	s.oldpath = xstrdup(path2);
	vec_push(&rc.to_stop, s);

	s.name = xstrdup("svc_c");
	s.oldpath = xstrdup(path3);
	vec_push(&rc.to_stop, s);

	ATF_REQUIRE_EQ(rc.to_stop.len, 3);

	char *saved_tmpdir = xstrdup(rc.tmpdir);
	pkg_deferred_rc_free(&rc);

	/* All three scripts and the tmpdir should be gone */
	ATF_REQUIRE_EQ_MSG(access(path1, F_OK), -1,
	    "svc_a script should have been removed");
	ATF_REQUIRE_EQ_MSG(access(path2, F_OK), -1,
	    "svc_b script should have been removed");
	ATF_REQUIRE_EQ_MSG(access(path3, F_OK), -1,
	    "svc_c script should have been removed");
	ATF_REQUIRE_EQ_MSG(access(saved_tmpdir, F_OK), -1,
	    "tmpdir should have been removed");

	free(saved_tmpdir);
}

ATF_TC_BODY(deferred_rc_seen_sets_independent, tc)
{
	struct deferred_rc rc;

	pkg_deferred_rc_init(&rc);

	/* Add to seen_stop only */
	pkghash_safe_add(rc.seen_stop, "only_stop", NULL, NULL);
	/* Add to seen_start only */
	pkghash_safe_add(rc.seen_start, "only_start", NULL, NULL);

	/* Each should be in its own set but not the other */
	ATF_REQUIRE(pkghash_get(rc.seen_stop, "only_stop") != NULL);
	ATF_REQUIRE(pkghash_get(rc.seen_start, "only_stop") == NULL);

	ATF_REQUIRE(pkghash_get(rc.seen_start, "only_start") != NULL);
	ATF_REQUIRE(pkghash_get(rc.seen_stop, "only_start") == NULL);

	/* A name not in either set */
	ATF_REQUIRE(pkghash_get(rc.seen_stop, "unknown") == NULL);
	ATF_REQUIRE(pkghash_get(rc.seen_start, "unknown") == NULL);

	pkg_deferred_rc_free(&rc);
}

ATF_TC_BODY(deferred_rc_mixed_stop_start, tc)
{
	struct deferred_rc rc;
	struct deferred_rc_stop s;

	pkg_deferred_rc_init(&rc);

	/*
	 * Simulate an upgrade scenario: sshd appears in both stop and start.
	 * nginx is only stopped (deleted).
	 * postfix is only started (new install).
	 */
	pkghash_safe_add(rc.seen_stop, "sshd", NULL, NULL);
	s.name = xstrdup("sshd");
	s.oldpath = NULL;
	vec_push(&rc.to_stop, s);

	pkghash_safe_add(rc.seen_stop, "nginx", NULL, NULL);
	s.name = xstrdup("nginx");
	s.oldpath = NULL;
	vec_push(&rc.to_stop, s);

	pkghash_safe_add(rc.seen_start, "sshd", NULL, NULL);
	vec_push(&rc.to_start, xstrdup("sshd"));

	pkghash_safe_add(rc.seen_start, "postfix", NULL, NULL);
	vec_push(&rc.to_start, xstrdup("postfix"));

	ATF_REQUIRE_EQ(rc.to_stop.len, 2);
	ATF_REQUIRE_EQ(rc.to_start.len, 2);

	/* sshd is an upgrade: in both sets */
	ATF_REQUIRE(pkghash_get(rc.seen_stop, "sshd") != NULL);
	ATF_REQUIRE(pkghash_get(rc.seen_start, "sshd") != NULL);

	/* nginx is a deletion: stop only */
	ATF_REQUIRE(pkghash_get(rc.seen_stop, "nginx") != NULL);
	ATF_REQUIRE(pkghash_get(rc.seen_start, "nginx") == NULL);

	/* postfix is a new install: start only */
	ATF_REQUIRE(pkghash_get(rc.seen_stop, "postfix") == NULL);
	ATF_REQUIRE(pkghash_get(rc.seen_start, "postfix") != NULL);

	pkg_deferred_rc_free(&rc);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, deferred_rc_init_free);
	ATF_TP_ADD_TC(tp, deferred_rc_free_null);
	ATF_TP_ADD_TC(tp, deferred_rc_stop_entries);
	ATF_TP_ADD_TC(tp, deferred_rc_start_entries);
	ATF_TP_ADD_TC(tp, deferred_rc_dedup);
	ATF_TP_ADD_TC(tp, deferred_rc_tmpdir_cleanup);
	ATF_TP_ADD_TC(tp, deferred_rc_free_reuse);
	ATF_TP_ADD_TC(tp, deferred_rc_stop_all_null_oldpath);
	ATF_TP_ADD_TC(tp, deferred_rc_tmpdir_multiple_scripts);
	ATF_TP_ADD_TC(tp, deferred_rc_seen_sets_independent);
	ATF_TP_ADD_TC(tp, deferred_rc_mixed_stop_start);

	return (atf_no_error());
}
