/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 */

#include <atf-c.h>
#include <pkg.h>
#include <private/pkg.h>
#include <private/pkg_jobs.h>

/*
 * Unit tests for pkg_jobs_need_upgrade().
 *
 * Regression test for https://github.com/freebsd/pkg/issues/2731
 * where options/dependencies differences were reported twice
 */

static void
make_pkgs(struct pkg **lp, struct pkg **rp, const char *lpabi,
    const char *rpabi)
{
	setenv("INSTALL_AS_USER", "yes", 1);
	setenv("NO_TICK", "yes", 1);
	setenv("PKG_ENABLE_PLUGINS", "false", 1);

	ATF_REQUIRE_EQ(EPKG_OK, pkg_ini(NULL, NULL, 0));
	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(lp, PKG_INSTALLED));
	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(rp, PKG_REMOTE));
	/* identical version & ABI so the function reaches the option/dep checks */
	(*lp)->version = xstrdup("1.0");
	(*rp)->version = xstrdup("1.0");
	(*lp)->abi = xstrdup(lpabi);
	(*rp)->abi = xstrdup(rpabi);
}

ATF_TC_WITHOUT_HEAD(options_offset_no_doubling);
ATF_TC_BODY(options_offset_no_doubling, tc)
{
	struct pkg *lp, *rp;
	charv_t sys = vec_init();
	static const char *loptions[] = {
	    "AMRNB", "AMRWB", "AO", "FLAC", "GSM", "ID3TAG", "LADSPA",
	    "LAME", "MAD", "OPUS", "OSS", "PNG", "PULSEAUDIO", "SNDFILE",
	    "SNDIO", NULL,
	};
	static const char *roptions[] = {
	    "AMR", "AO", "FLAC", "GSM", "ID3TAG", "LADSPA", "LAME", "MAD",
	    "OPUS", "OSS", "PNG", "PULSEAUDIO", "SNDFILE", "SNDIO",
	    "SYMLINK", NULL,
	};

	make_pkgs(&lp, &rp, "FreeBSD:14:amd64", "FreeBSD:14:amd64");

	for (int i = 0; loptions[i] != NULL; i++)
		ATF_REQUIRE_EQ(EPKG_OK, pkg_addoption(lp, loptions[i], "on"));
	for (int i = 0; roptions[i] != NULL; i++)
		ATF_REQUIRE_EQ(EPKG_OK, pkg_addoption(rp, roptions[i], "on"));

	ATF_REQUIRE(pkg_jobs_need_upgrade(&sys, rp, lp));

	/*
	 * The only real differences are: removed AMRNB/AMRWB, added AMR/SYMLINK.
	 * A given option must never be reported both as added and removed.
	 */
	ATF_REQUIRE(strstr(rp->reason, "AO (added), AO (removed)") == NULL);
	ATF_REQUIRE(strstr(rp->reason, "SNDFILE (added), SNDFILE (removed)") == NULL);
	ATF_REQUIRE_STREQ(rp->reason,
	    "option changed: AMR (added), AMRNB (removed), AMRWB (removed), "
	    "SYMLINK (added)");

	pkg_free(lp);
	pkg_free(rp);
	vec_autofree(&sys);
}

ATF_TC_WITHOUT_HEAD(options_identical_no_upgrade);
ATF_TC_BODY(options_identical_no_upgrade, tc)
{
	struct pkg *lp, *rp;
	charv_t sys = vec_init();

	make_pkgs(&lp, &rp, "FreeBSD:14:amd64", "FreeBSD:14:amd64");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_addoption(lp, "FOO", "on"));
	ATF_REQUIRE_EQ(EPKG_OK, pkg_addoption(rp, "FOO", "on"));

	ATF_REQUIRE(!pkg_jobs_need_upgrade(&sys, rp, lp));
	ATF_REQUIRE(rp->reason == NULL);

	pkg_free(lp);
	pkg_free(rp);
	vec_autofree(&sys);
}

ATF_TC_WITHOUT_HEAD(options_value_change);
ATF_TC_BODY(options_value_change, tc)
{
	struct pkg *lp, *rp;
	charv_t sys = vec_init();

	make_pkgs(&lp, &rp, "FreeBSD:14:amd64", "FreeBSD:14:amd64");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_addoption(lp, "FOO", "on"));
	ATF_REQUIRE_EQ(EPKG_OK, pkg_addoption(rp, "FOO", "off"));

	ATF_REQUIRE(pkg_jobs_need_upgrade(&sys, rp, lp));
	ATF_REQUIRE_STREQ(rp->reason, "option changed: FOO (on -> off)");

	pkg_free(lp);
	pkg_free(rp);
	vec_autofree(&sys);
}

ATF_TC_WITHOUT_HEAD(deps_added_removed);
ATF_TC_BODY(deps_added_removed, tc)
{
	struct pkg *lp, *rp;
	charv_t sys = vec_init();

	make_pkgs(&lp, &rp, "FreeBSD:14:amd64", "FreeBSD:14:amd64");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_adddep(lp, "foo", "cat/foo", "1.0", false));
	ATF_REQUIRE_EQ(EPKG_OK, pkg_adddep(lp, "baz", "cat/baz", "1.0", false));
	ATF_REQUIRE_EQ(EPKG_OK, pkg_adddep(rp, "bar", "cat/bar", "1.0", false));
	ATF_REQUIRE_EQ(EPKG_OK, pkg_adddep(rp, "baz", "cat/baz", "1.0", false));

	ATF_REQUIRE(pkg_jobs_need_upgrade(&sys, rp, lp));
	ATF_REQUIRE_STREQ(rp->reason,
	    "direct dependency changed: bar (added), foo (removed)");

	pkg_free(lp);
	pkg_free(rp);
	vec_autofree(&sys);
}

ATF_TC_WITHOUT_HEAD(abi_change);
ATF_TC_BODY(abi_change, tc)
{
	struct pkg *lp, *rp;
	charv_t sys = vec_init();

	make_pkgs(&lp, &rp, "FreeBSD:14:amd64", "FreeBSD:14:*");

	ATF_REQUIRE(pkg_jobs_need_upgrade(&sys, rp, lp));
	ATF_REQUIRE_STREQ(rp->reason,
	    "ABI changed: 'FreeBSD:14:amd64' -> 'FreeBSD:14:*'");

	pkg_free(lp);
	pkg_free(rp);
	vec_autofree(&sys);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, options_offset_no_doubling);
	ATF_TP_ADD_TC(tp, options_identical_no_upgrade);
	ATF_TP_ADD_TC(tp, options_value_change);
	ATF_TP_ADD_TC(tp, deps_added_removed);
	ATF_TP_ADD_TC(tp, abi_change);

	return (atf_no_error());
}
