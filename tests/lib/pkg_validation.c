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

#include <sys/sbuf.h>

#include <string.h>

#include <atf-c.h>
#include <pkg.h>

struct sbuf *msg;

ATF_TC(valid_installed);

ATF_TC_HEAD(valid_installed, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "pkg_valid() tests installed packages");
}

int
event_callback(void *data, struct pkg_event *ev)
{
	switch (ev->type) {
	case PKG_EVENT_ERROR:
		sbuf_clear(msg);
		sbuf_printf(msg, "%s", ev->e_pkg_error.msg);
		sbuf_finish(msg);
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
	msg = sbuf_new_auto();

	ATF_REQUIRE_EQ(EPKG_FATAL, pkg_is_valid(p));
	ATF_REQUIRE_STREQ(sbuf_data(msg), "Invalid package: object has missing property origin");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_set(p, PKG_ORIGIN, "test/bla"));
	ATF_REQUIRE_EQ(EPKG_FATAL, pkg_is_valid(p));
	ATF_REQUIRE_STREQ(sbuf_data(msg), "Invalid package: object has missing property name");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_set(p, PKG_NAME, "test"));
	ATF_REQUIRE_EQ(EPKG_FATAL, pkg_is_valid(p));
	ATF_REQUIRE_STREQ(sbuf_data(msg), "Invalid package: object has missing property comment");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_set(p, PKG_COMMENT, "test comment"));
	ATF_REQUIRE_EQ(EPKG_FATAL, pkg_is_valid(p));
	ATF_REQUIRE_STREQ(sbuf_data(msg), "Invalid package: object has missing property version");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_set(p, PKG_VERSION, "1.1.0"));
	ATF_REQUIRE_EQ(EPKG_FATAL, pkg_is_valid(p));
	ATF_REQUIRE_STREQ(sbuf_data(msg), "Invalid package: object has missing property desc");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_set(p, PKG_DESC, "test description"));
	ATF_REQUIRE_EQ(EPKG_FATAL, pkg_is_valid(p));
	ATF_REQUIRE_STREQ(sbuf_data(msg), "Invalid package: object has missing property maintainer");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_set(p, PKG_MAINTAINER, "tester"));
	ATF_REQUIRE_EQ(EPKG_FATAL, pkg_is_valid(p));
	ATF_REQUIRE_STREQ(sbuf_data(msg), "Invalid package: object has missing property www");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_set(p, PKG_WWW, "test website"));
	ATF_REQUIRE_EQ(EPKG_FATAL, pkg_is_valid(p));
	ATF_REQUIRE_STREQ(sbuf_data(msg), "Invalid package: object has missing property prefix");

	ATF_REQUIRE_EQ(EPKG_OK, pkg_set(p, PKG_PREFIX, "/usr/local"));
	ATF_REQUIRE_EQ(EPKG_OK, pkg_is_valid(p));

	sbuf_delete(msg);
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
