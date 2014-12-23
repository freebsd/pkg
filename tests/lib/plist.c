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

#include <atf-c.h>
#include <pkg.h>
#include <private/pkg.h>

ATF_TC(parse_mode);

ATF_TC_HEAD(parse_mode, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "parse_mode()");
}

ATF_TC_BODY(parse_mode, tc)
{
	void *set;

	set = parse_mode("u+x");
	ATF_REQUIRE(set == NULL);

	set = parse_mode("plop");
	ATF_REQUIRE(set == NULL);

	set = parse_mode("0755");
	ATF_REQUIRE(set != NULL);

	free(set);

	set = parse_mode("u=r,g=rX");
	ATF_REQUIRE(set != NULL);

	free(set);
}

ATF_TC(parse_plist);

ATF_TC_HEAD(parse_plist, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "parse_plist()");
}

ATF_TC_BODY(parse_plist, tc)
{
	struct pkg *p;
	struct plist *plist;
	char buf[BUFSIZ];

	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_INSTALLED));

	plist = plist_new(p, "/plop");
	ATF_REQUIRE(plist != NULL);
	ATF_REQUIRE(plist->pkg == p);
	ATF_REQUIRE_EQ(plist->prefix[0], '\0');

	strlcpy(buf, "@cwd /myprefix", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(p, plist, buf));
	ATF_REQUIRE_STREQ(p->prefix, "/myprefix");

	ATF_REQUIRE_STREQ(plist->prefix, "/myprefix");

	ATF_REQUIRE_STREQ(plist->uname, "root");
	ATF_REQUIRE_STREQ(plist->gname, "wheel");

	strlcpy(buf, "@owner bob", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(p, plist, buf));
	ATF_REQUIRE_STREQ(plist->uname, "bob");

	strlcpy(buf, "@group sponge", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(p, plist, buf));
	ATF_REQUIRE_STREQ(plist->gname, "sponge");

	strlcpy(buf, "@group", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(p, plist, buf));
	ATF_REQUIRE_STREQ(plist->gname, "wheel");

	strlcpy(buf, "@owner", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(p, plist, buf));
	ATF_REQUIRE_STREQ(plist->uname, "root");

	strlcpy(buf, "@cwd plop", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(p, plist, buf));
	ATF_REQUIRE_STREQ(plist->prefix, "plop");

	strlcpy(buf, "@cwd", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(p, plist, buf));
	ATF_REQUIRE_STREQ(plist->prefix, "/myprefix");
	ATF_REQUIRE_STREQ(plist->slash, "/");

	strlcpy(buf, "@cwd /another/prefix/", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(p, plist, buf));
	ATF_REQUIRE_STREQ(plist->prefix, "/another/prefix/");
	ATF_REQUIRE_STREQ(plist->slash, "");

	ATF_REQUIRE_EQ(0, plist->perm);
	strlcpy(buf, "@mode 0755", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(p, plist, buf));
	ATF_REQUIRE_EQ(0755, plist->perm);

	strlcpy(buf, "@mode", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(p, plist, buf));
	ATF_REQUIRE_EQ(0, plist->perm);

	strlcpy(buf, "@blabla", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_FATAL, plist_parse_line(p, plist, buf));

	strlcpy(buf, "nonexisting/file", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_FATAL, plist_parse_line(p, plist, buf));

	pkg_free(p);
	plist_free(plist);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, parse_mode);
	ATF_TP_ADD_TC(tp, parse_plist);

	return (atf_no_error());
}
