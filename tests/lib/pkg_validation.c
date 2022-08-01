/*-
 * Copyright (c) 2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * All rights reserved.
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

#include <sys/types.h>

#include <string.h>

#include <atf-c.h>
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

ATF_TC_WITHOUT_HEAD(valid_installed);

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

void
check_valid(struct pkg *p)
{
	msg = xstring_new();

	ATF_REQUIRE_EQ(EPKG_FATAL, pkg_is_valid(p));
	fflush(msg->fp);
	ATF_REQUIRE_STREQ(msg->buf, "Invalid package: object has missing property origin");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_set(p, PKG_ORIGIN, "test/bla"));
	ATF_REQUIRE_EQ(EPKG_FATAL, pkg_is_valid(p));
	fflush(msg->fp);
	ATF_REQUIRE_STREQ(msg->buf, "Invalid package: object has missing property name");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_set(p, PKG_NAME, "test"));
	ATF_REQUIRE_EQ(EPKG_FATAL, pkg_is_valid(p));
	fflush(msg->fp);
	ATF_REQUIRE_STREQ(msg->buf, "Invalid package: object has missing property comment");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_set(p, PKG_COMMENT, "test comment"));
	ATF_REQUIRE_EQ(EPKG_FATAL, pkg_is_valid(p));
	fflush(msg->fp);
	ATF_REQUIRE_STREQ(msg->buf, "Invalid package: object has missing property version");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_set(p, PKG_VERSION, "1.1.0"));
	ATF_REQUIRE_EQ(EPKG_FATAL, pkg_is_valid(p));
	fflush(msg->fp);
	ATF_REQUIRE_STREQ(msg->buf, "Invalid package: object has missing property desc");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_set(p, PKG_DESC, "test description"));
	ATF_REQUIRE_EQ(EPKG_FATAL, pkg_is_valid(p));
	fflush(msg->fp);
	ATF_REQUIRE_STREQ(msg->buf, "Invalid package: object has missing property maintainer");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_set(p, PKG_MAINTAINER, "tester"));
	ATF_REQUIRE_EQ(EPKG_FATAL, pkg_is_valid(p));
	fflush(msg->fp);
	ATF_REQUIRE_STREQ(msg->buf, "Invalid package: object has missing property www");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_set(p, PKG_WWW, "test website"));
	ATF_REQUIRE_EQ(EPKG_FATAL, pkg_is_valid(p));
	fflush(msg->fp);
	ATF_REQUIRE_STREQ(msg->buf, "Invalid package: object has missing property prefix");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_set(p, PKG_PREFIX, "/usr/local"));
	ATF_REQUIRE_EQ(EPKG_OK, pkg_is_valid(p));

	xstring_free(msg);
}

ATF_TC_BODY(valid_installed, tc)
{
	struct pkg *p = NULL;

	pkg_event_register(event_callback, NULL);

	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_INSTALLED));
	ATF_REQUIRE(p != NULL);

	check_valid(p);
}

ATF_TC(valid_file);

ATF_TC_HEAD(valid_file, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "pkg_valid() tests file packages");
}

ATF_TC_BODY(valid_file, tc)
{
	struct pkg *p = NULL;

	pkg_event_register(event_callback, NULL);

	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_FILE));
	ATF_REQUIRE(p != NULL);

	check_valid(p);
}

ATF_TC(valid_remote);

ATF_TC_HEAD(valid_remote, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "pkg_valid() tests remote packages");
}

ATF_TC_BODY(valid_remote, tc)
{
	struct pkg *p = NULL;

	pkg_event_register(event_callback, NULL);

	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_REMOTE));
	ATF_REQUIRE(p != NULL);

	check_valid(p);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, valid_installed);
	ATF_TP_ADD_TC(tp, valid_file);
	ATF_TP_ADD_TC(tp, valid_remote);

	return (atf_no_error());
}
