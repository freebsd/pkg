/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 */

#include <atf-c.h>
#include <pkg.h>
#include <private/pkg.h>

/*
 * Test that pkg_jobs_apply() honors PKG_FLAG_DRY_RUN for delete jobs.
 * See: https://github.com/freebsd/pkg/issues/2137
 */

ATF_TC_WITHOUT_HEAD(delete_dry_run_via_jobs_apply);

ATF_TC_BODY(delete_dry_run_via_jobs_apply, tc)
{
	struct pkgdb *db = NULL;
	struct pkg_jobs *jobs = NULL;
	struct pkg *p = NULL;
	struct pkgdb_it *it = NULL;
	char *argv[] = { "testpkg", NULL };

	setenv("INSTALL_AS_USER", "yes", 1);
	setenv("PKG_DBDIR", ".", 1);
	setenv("NO_TICK", "yes", 1);
	setenv("PKG_ENABLE_PLUGINS", "false", 1);

	ATF_REQUIRE_EQ(EPKG_OK, pkg_ini(NULL, NULL, 0));
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_open(&db, PKGDB_DEFAULT));

	/* Create and register a package directly */
	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_INSTALLED));
	p->name = xstrdup("testpkg");
	p->uid = xstrdup("testpkg");
	p->origin = xstrdup("test/testpkg");
	p->version = xstrdup("1.0");
	p->maintainer = xstrdup("test");
	p->www = xstrdup("http://test");
	p->comment = xstrdup("a test package");
	p->desc = xstrdup("Test package for dry-run");
	p->prefix = xstrdup("/usr/local");
	p->abi = xstrdup("*");

	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_register_pkg(db, p, 0, NULL));
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_register_finale(db, EPKG_OK, NULL));
	pkg_free(p);

	/* Verify the package is registered */
	it = pkgdb_query(db, "testpkg", MATCH_EXACT);
	ATF_REQUIRE(it != NULL);
	p = NULL;
	ATF_REQUIRE_EQ(EPKG_OK, pkgdb_it_next(it, &p, PKG_LOAD_BASIC));
	ATF_REQUIRE(p != NULL);
	ATF_REQUIRE_STREQ(p->name, "testpkg");
	pkg_free(p);
	pkgdb_it_free(it);

	/* Create a DEINSTALL job with DRY_RUN flag */
	ATF_REQUIRE_EQ(EPKG_OK,
	    pkg_jobs_new(&jobs, PKG_JOBS_DEINSTALL, db));
	pkg_jobs_set_flags(jobs, PKG_FLAG_DRY_RUN | PKG_FLAG_FORCE);
	ATF_REQUIRE_EQ(EPKG_OK,
	    pkg_jobs_add(jobs, MATCH_EXACT, argv, 1));
	ATF_REQUIRE_EQ(EPKG_OK, pkg_jobs_solve(jobs));
	ATF_REQUIRE(pkg_jobs_count(jobs) == 1);

	/* Apply with DRY_RUN — package must NOT be removed */
	ATF_REQUIRE_EQ(EPKG_OK, pkg_jobs_apply(jobs));
	pkg_jobs_free(jobs);

	/* Verify the package is still in the database */
	it = pkgdb_query(db, "testpkg", MATCH_EXACT);
	ATF_REQUIRE(it != NULL);
	p = NULL;
	ATF_REQUIRE_EQ_MSG(EPKG_OK, pkgdb_it_next(it, &p, PKG_LOAD_BASIC),
	    "Package was removed despite PKG_FLAG_DRY_RUN");
	ATF_REQUIRE(p != NULL);
	ATF_REQUIRE_STREQ(p->name, "testpkg");

	pkg_free(p);
	pkgdb_it_free(it);
	pkgdb_close(db);
	pkg_shutdown();
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, delete_dry_run_via_jobs_apply);

	return (atf_no_error());
}
