/*-
 * Copyright(c) 2024 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/types.h>

#include <string.h>

#include <atf-c.h>
#include <private/pkg.h>
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

	xasprintf(&binpath, "%s/frontend/libtestfbsd.so.1", atf_tc_get_config_var(tc, "srcdir"));

	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_INSTALLED));
	ATF_REQUIRE(p != NULL);

	ATF_REQUIRE_EQ(tll_length(p->shlibs_provided), 0);
	ATF_REQUIRE_EQ(pkg_analyse_elf(false, p, binpath), EPKG_OK);
	ATF_REQUIRE_EQ(tll_length(p->shlibs_provided), 1);
	ATF_REQUIRE_STREQ(tll_front(p->shlibs_provided), "libtestfbsd.so.1");

	free(binpath);
	xasprintf(&binpath, "%s/Makefile.autosetup", atf_tc_get_config_var(tc, "srcdir"));
	ATF_REQUIRE_EQ(pkg_analyse_elf(false, p, binpath), EPKG_END);
	ATF_REQUIRE_EQ(tll_length(p->shlibs_provided), 1);
	free(binpath);

}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, analyse_elf);

	return (atf_no_error());
}
