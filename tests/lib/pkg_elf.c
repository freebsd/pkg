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

	ATF_REQUIRE_EQ(vec_len(&p->shlibs_required), 0);
	ATF_REQUIRE_EQ(pkg_analyse_elf(false, p, binpath, &provided, &provided_flags), EPKG_OK);
	ATF_REQUIRE_EQ(vec_len(&p->shlibs_provided), 0);
	ATF_REQUIRE_STREQ(provided, "libtestfbsd.so.1");
	ATF_REQUIRE_EQ(provided_flags, PKG_SHLIB_FLAGS_NONE);
	ATF_REQUIRE_EQ(vec_len(&p->shlibs_required), 1);
	ATF_REQUIRE_STREQ(vec_first(&p->shlibs_required), "libc.so.7");
	free(provided);
	free(binpath);

	provided = NULL;
	provided_flags = PKG_SHLIB_FLAGS_NONE;
	xasprintf(&binpath, "%s/Makefile", atf_tc_get_config_var(tc, "srcdir"));
	ATF_REQUIRE_EQ(pkg_analyse_elf(false, p, binpath, &provided, &provided_flags), EPKG_END);
	ATF_REQUIRE_EQ(vec_len(&p->shlibs_provided), 0);
	ATF_REQUIRE_EQ(provided, NULL);
	ATF_REQUIRE_EQ(provided_flags, PKG_SHLIB_FLAGS_NONE);
	ATF_REQUIRE_EQ(vec_len(&p->shlibs_required), 1);
	free(provided);
	free(binpath);

	provided = NULL;
	provided_flags = PKG_SHLIB_FLAGS_NONE;
	xasprintf(&binpath, "%s/frontend/libtest2fbsd.so.1", atf_tc_get_config_var(tc, "srcdir"));
	ATF_REQUIRE_EQ(pkg_analyse_elf(false, p, binpath, &provided, &provided_flags), EPKG_OK);
	ATF_REQUIRE_EQ(vec_len(&p->shlibs_provided), 0);
	ATF_REQUIRE_STREQ(provided, "libtest2fbsd.so.1");
	ATF_REQUIRE_EQ(provided_flags, PKG_SHLIB_FLAGS_NONE);
	ATF_REQUIRE_EQ(vec_len(&p->shlibs_required), 2);
	ATF_REQUIRE_STREQ(vec_first(&p->shlibs_required), "libc.so.7");
	ATF_REQUIRE_STREQ(vec_last(&p->shlibs_required), "libfoo.so.1");
	free(provided);
	free(binpath);

	pkg_free(p);
}

ATF_TC_WITHOUT_HEAD(static_lib_non_elf);

ATF_TC_BODY(static_lib_non_elf, tc)
{
	struct pkg *p = NULL;
	char *provided = NULL;
	enum pkg_shlib_flags provided_flags = PKG_SHLIB_FLAGS_NONE;

	ctx.abi.os = PKG_OS_FREEBSD;
	ctx.abi.arch = PKG_ARCH_AMD64;
	ctx.developer_mode = true;

	/* Create a non-ELF .a archive (e.g. WASM static library) */
	FILE *fp = fopen("dummy.txt", "w");
	ATF_REQUIRE(fp != NULL);
	fprintf(fp, "not an ELF object\n");
	fclose(fp);
	ATF_REQUIRE_EQ(0, system("ar rcs libwasm.a dummy.txt"));

	/* Create an ELF .a archive (native static library) */
	fp = fopen("empty.c", "w");
	ATF_REQUIRE(fp != NULL);
	fprintf(fp, "int placeholder;\n");
	fclose(fp);
	ATF_REQUIRE_EQ(0, system("cc -c -o empty.o empty.c"));
	ATF_REQUIRE_EQ(0, system("ar rcs libnative.a empty.o"));

	/* Non-ELF .a should NOT set PKG_CONTAINS_STATIC_LIBS */
	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_INSTALLED));
	p->flags &= ~PKG_CONTAINS_STATIC_LIBS;
	ATF_REQUIRE_EQ(pkg_analyse_elf(true, p, "libwasm.a",
	    &provided, &provided_flags), EPKG_END);
	ATF_CHECK_EQ_MSG(0, p->flags & PKG_CONTAINS_STATIC_LIBS,
	    "non-ELF .a should not set PKG_CONTAINS_STATIC_LIBS");
	pkg_free(p);
	free(provided);

	/* ELF .a should set PKG_CONTAINS_STATIC_LIBS */
	provided = NULL;
	provided_flags = PKG_SHLIB_FLAGS_NONE;
	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_INSTALLED));
	p->flags &= ~PKG_CONTAINS_STATIC_LIBS;
	ATF_REQUIRE_EQ(pkg_analyse_elf(true, p, "libnative.a",
	    &provided, &provided_flags), EPKG_END);
	ATF_CHECK_EQ_MSG(PKG_CONTAINS_STATIC_LIBS,
	    p->flags & PKG_CONTAINS_STATIC_LIBS,
	    "ELF .a should set PKG_CONTAINS_STATIC_LIBS");
	pkg_free(p);
	free(provided);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, analyse_elf);
	ATF_TP_ADD_TC(tp, static_lib_non_elf);

	return (atf_no_error());
}
