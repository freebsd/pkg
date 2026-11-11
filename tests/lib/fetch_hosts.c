/*
 * Copyright (c) 2026 FreeBSD Foundation
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <sys/param.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fetch.h>
#include <atf-c.h>
#include "pkg.h"
#include "private/pkg.h"
#include "private/utils.h"
#include "private/fetch.h"

static void
initialize(void)
{
	FILE *f = fopen("hosts.conf", "w");
	ATF_REQUIRE(f != NULL);
	fputs("FETCH_RETRY: 2; FETCH_WORKERS_PER_HOST: 1;", f);
	fclose(f);
	ATF_REQUIRE_EQ(pkg_ini("hosts.conf", NULL, 0), EPKG_OK);
}

ATF_TC_WITHOUT_HEAD(srv_host_pool);
ATF_TC_BODY(srv_host_pool, tc)
{
	struct dns_srvinfo backup = { .host = "backup.test", .port = 80, .priority = 20 };
	struct dns_srvinfo second = { .host = "two.test.", .port = 80, .priority = 10, .next = &backup };
	struct dns_srvinfo first = { .host = "ONE.test", .port = 80, .priority = 10, .next = &second };
	struct pkg_repo a = { .url = "pkg+http://repo.test/base", .mirror_type = SRV, .srv = &first };
	struct pkg_repo b = { .url = "http://one.test/other", .mirror_type = NOMIRROR };
	struct pkg x = { .repo = &a, .repopath = "one.pkg" };
	struct pkg y = { .repo = &a, .repopath = "two.pkg" };
	struct pkg z = { .repo = &b, .repopath = "three.pkg" };
	struct pkg *pkgs[] = { &x, &y, &z };
	char *paths[] = { "one", "two", "three" };
	const char *url;
	size_t i, j, k;

	initialize();
	struct pkg_fetch_plan *p = pkg_fetch_plan_new(pkgs, paths, 3);
	ATF_REQUIRE(p != NULL);
	ATF_REQUIRE_EQ(pkg_fetch_plan_next(p, &i, &url), 1);
	ATF_REQUIRE(strstr(url, "backup.test") == NULL);
	ATF_REQUIRE(strstr(url, "/base/") != NULL);
	ATF_REQUIRE_EQ(pkg_fetch_plan_next(p, &j, &url), 1);
	ATF_REQUIRE(strstr(url, "backup.test") == NULL);
	/* A third job cannot use a backup merely because primary hosts are busy. */
	ATF_REQUIRE_EQ(pkg_fetch_plan_next(p, &k, &url), 0);
	struct fetch_response response = {0};
	ATF_REQUIRE_EQ(pkg_fetch_plan_result(p, i, EPKG_OK, FETCH_OK, &response, ""), EPKG_END);
	ATF_REQUIRE_EQ(pkg_fetch_plan_result(p, j, EPKG_OK, FETCH_OK, &response, ""), EPKG_END);
	ATF_REQUIRE_EQ(pkg_fetch_plan_next(p, &k, &url), 1);
	ATF_REQUIRE(strstr(url, "backup.test") == NULL);
	if (k == 2)
		ATF_REQUIRE(strstr(url, "one.test") != NULL);
	pkg_fetch_plan_free(p);
	pkg_shutdown();
}

ATF_TC_WITHOUT_HEAD(shared_host_failures);
ATF_TC_BODY(shared_host_failures, tc)
{
	struct pkg_repo a = { .url = "http://one.test/a", .mirror_type = HTTP };
	struct pkg_repo b = { .url = "http://one.test/b", .mirror_type = NOMIRROR };
	struct pkg x = { .repo = &a, .repopath = "one.pkg" };
	struct pkg y = { .repo = &b, .repopath = "two.pkg" };
	struct pkg *pkgs[] = { &x, &y };
	char *paths[] = { "one", "two" };
	const char *url;
	size_t i, j;
	struct fetch_response response = {0};

	initialize();
	pkg_repo_http_mirror_append(&a, "http://ONE.test.:8080/edge/", false);
	pkg_repo_http_mirror_append(&a, "http://two.test/edge/", false);
	struct pkg_fetch_plan *p = pkg_fetch_plan_new(pkgs, paths, 2);
	ATF_REQUIRE(p != NULL);
	ATF_REQUIRE_EQ(pkg_fetch_plan_next(p, &i, &url), 1);
	ATF_REQUIRE(strstr(url, "/edge/a/one.pkg") != NULL);
	ATF_REQUIRE_EQ(pkg_fetch_plan_next(p, &j, &url), 0);
	/* A timeout/low-speed failure suspends this FQDN across repos and ports. */
	ATF_REQUIRE_EQ(pkg_fetch_plan_result(p, i, EPKG_FATAL, FETCH_TIMEOUT,
	    &response, "slow"), EPKG_OK);
	ATF_REQUIRE_EQ(pkg_fetch_plan_next(p, &j, &url), 1);
	ATF_REQUIRE_EQ(i, j);
	ATF_REQUIRE(strstr(url, "two.test") != NULL);
	pkg_fetch_plan_free(p);
	libfetch_cleanup(&a);
	pkg_shutdown();
}

ATF_TC_WITHOUT_HEAD(recovery_probe_generation);
ATF_TC_BODY(recovery_probe_generation, tc)
{
	struct pkg_repo repo = { .url = "http://one.test/base", .mirror_type = NOMIRROR };
	struct pkg a = { .repo = &repo, .repopath = "a.pkg" };
	struct pkg b = { .repo = &repo, .repopath = "b.pkg" };
	struct pkg c = { .repo = &repo, .repopath = "c.pkg" };
	struct pkg *pkgs[] = { &a, &b, &c };
	char *paths[] = { "a", "b", "c" };
	struct fetch_response response = {0};
	const char *url;
	size_t i, j, k;

	setenv("FETCH_WORKERS_PER_HOST", "2", 1);
	initialize();
	struct pkg_fetch_plan *p = pkg_fetch_plan_new(pkgs, paths, 3);
	ATF_REQUIRE(p != NULL);
	ATF_REQUIRE_EQ(pkg_fetch_plan_next(p, &i, &url), 1);
	ATF_REQUIRE_EQ(pkg_fetch_plan_next(p, &j, &url), 1);
	ATF_REQUIRE_EQ(pkg_fetch_plan_result(p, i, EPKG_FATAL, FETCH_NETWORK,
	    &response, "connection lost"), EPKG_OK);
	/* A transfer started before the failure must not restore the host. */
	ATF_REQUIRE_EQ(pkg_fetch_plan_result(p, j, EPKG_OK, FETCH_OK, &response, ""), EPKG_END);
	ATF_REQUIRE_EQ(pkg_fetch_plan_next(p, &k, &url), 0);
	sleep(3);
	ATF_REQUIRE_EQ(pkg_fetch_plan_next(p, &k, &url), 1);
	ATF_REQUIRE_EQ(pkg_fetch_plan_next(p, &i, &url), 0);
	pkg_fetch_plan_free(p);
	pkg_shutdown();
	unsetenv("FETCH_WORKERS_PER_HOST");
}

ATF_TC_WITHOUT_HEAD(resource_failure_scope);
ATF_TC_BODY(resource_failure_scope, tc)
{
	struct pkg_repo repo = { .url = "http://one.test/base", .mirror_type = NOMIRROR };
	struct pkg a = { .repo = &repo, .repopath = "missing.pkg" };
	struct pkg b = { .repo = &repo, .repopath = "available.pkg" };
	struct pkg *pkgs[] = { &a, &b };
	char *paths[] = { "a", "b" };
	struct fetch_response response = { .status = 404 };
	const char *url;
	size_t i, j;

	initialize();
	struct pkg_fetch_plan *p = pkg_fetch_plan_new(pkgs, paths, 2);
	ATF_REQUIRE(p != NULL);
	ATF_REQUIRE_EQ(pkg_fetch_plan_next(p, &i, &url), 1);
	ATF_REQUIRE_EQ(pkg_fetch_plan_result(p, i, EPKG_FATAL, FETCH_UNAVAIL,
	    &response, "missing"), EPKG_OK);
	ATF_REQUIRE_EQ(pkg_fetch_plan_next(p, &j, &url), 1);
	ATF_REQUIRE(i != j);
	pkg_fetch_plan_free(p);
	pkg_shutdown();
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, srv_host_pool);
	ATF_TP_ADD_TC(tp, shared_host_failures);
	ATF_TP_ADD_TC(tp, recovery_probe_generation);
	ATF_TP_ADD_TC(tp, resource_failure_scope);
	return (atf_no_error());
}
