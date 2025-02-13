/*-
 * Copyright(c) 2024 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/types.h>

#include <string.h>

#include <atf-c.h>
#include <private/pkg.h>
#include <private/pkg_abi.h>
#include <private/binfmt.h>
#include <xstring.h>
#include <tllist.h>
#include <pkg.h>

#ifndef __unused
# ifdef __GNUC__
# define __unused __attribute__ ((__unused__))
# else
# define __unused
# endif
#endif

xstring *msg;

ATF_TC_WITHOUT_HEAD(analyse_elf);

int
event_callback(void *data __unused, struct pkg_event *ev)
{
	switch (ev->type) {
	case PKG_EVENT_ERROR:
		xstring_reset(msg);
		fprintf(msg->fp, "%s", ev->e_pkg_error.msg);
		break;
	default:
		/* IGNORE */
		break;
	}

	return (0);
}

ATF_TC_BODY(analyse_elf, tc)
{
	struct pkg *p = NULL;
	char *binpath = NULL;
	char *provided = NULL;
	enum pkg_shlib_flags provided_flags = PKG_SHLIB_FLAGS_NONE;

	ctx.abi.os = PKG_OS_FREEBSD;
	ctx.abi.arch = PKG_ARCH_AMD64;

	xasprintf(&binpath, "%s/frontend/libtestfbsd.so.1", atf_tc_get_config_var(tc, "srcdir"));

	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_INSTALLED));
	ATF_REQUIRE(p != NULL);

	ATF_REQUIRE_EQ(tll_length(p->shlibs_required), 0);
	ATF_REQUIRE_EQ(pkg_analyse_elf(false, p, binpath, &provided, &provided_flags), EPKG_OK);
	ATF_REQUIRE_EQ(tll_length(p->shlibs_provided), 0);
	ATF_REQUIRE_STREQ(provided, "libtestfbsd.so.1");
	ATF_REQUIRE_EQ(provided_flags, PKG_SHLIB_FLAGS_NONE);
	ATF_REQUIRE_EQ(tll_length(p->shlibs_required), 1);
	ATF_REQUIRE_STREQ(tll_front(p->shlibs_required), "libc.so.7");
	free(provided);
	free(binpath);

	provided = NULL;
	provided_flags = PKG_SHLIB_FLAGS_NONE;
	xasprintf(&binpath, "%s/Makefile", atf_tc_get_config_var(tc, "srcdir"));
	ATF_REQUIRE_EQ(pkg_analyse_elf(false, p, binpath, &provided, &provided_flags), EPKG_END);
	ATF_REQUIRE_EQ(tll_length(p->shlibs_provided), 0);
	ATF_REQUIRE_EQ(provided, NULL);
	ATF_REQUIRE_EQ(provided_flags, PKG_SHLIB_FLAGS_NONE);
	ATF_REQUIRE_EQ(tll_length(p->shlibs_required), 1);
	free(provided);
	free(binpath);

	provided = NULL;
	provided_flags = PKG_SHLIB_FLAGS_NONE;
	xasprintf(&binpath, "%s/frontend/libtest2fbsd.so.1", atf_tc_get_config_var(tc, "srcdir"));
	ATF_REQUIRE_EQ(pkg_analyse_elf(false, p, binpath, &provided, &provided_flags), EPKG_OK);
	ATF_REQUIRE_EQ(tll_length(p->shlibs_provided), 0);
	ATF_REQUIRE_STREQ(provided, "libtest2fbsd.so.1");
	ATF_REQUIRE_EQ(provided_flags, PKG_SHLIB_FLAGS_NONE);
	ATF_REQUIRE_EQ(tll_length(p->shlibs_required), 2);
	ATF_REQUIRE_STREQ(tll_front(p->shlibs_required), "libc.so.7");
	ATF_REQUIRE_STREQ(tll_back(p->shlibs_required), "libfoo.so.1");
	free(provided);
	free(binpath);

	pkg_free(p);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, analyse_elf);

	return (atf_no_error());
}
