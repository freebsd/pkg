/*-
 * Copyright (c) 2013-2020 Baptiste Daroussin <bapt@FreeBSD.org>
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

ATF_TC_WITHOUT_HEAD(parse_mode);

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

ATF_TC(parse_keyword_attributes);

ATF_TC_HEAD(parse_keyword_attributes, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "parse_keyword_attributes()");
}

ATF_TC_BODY(parse_keyword_attributes, tc)
{
	char buf[BUFSIZ];
	struct file_attr *a;

	strlcpy(buf, "()", BUFSIZ);
	ATF_REQUIRE(parse_keyword_args(buf, "plop") == NULL);

	strlcpy(buf, "(root, wheel)", BUFSIZ);
	ATF_REQUIRE((a = parse_keyword_args(buf, "plop")) != NULL);
	ATF_REQUIRE_STREQ(a->owner, "root");
	ATF_REQUIRE_STREQ(a->group, "wheel");
	free_file_attr(a);

	strlcpy(buf, "(root, wheel, 0755)", BUFSIZ);
	ATF_REQUIRE((a = parse_keyword_args(buf, "plop")) != NULL);
	ATF_REQUIRE_STREQ(a->owner, "root");
	ATF_REQUIRE_STREQ(a->group, "wheel");
	free_file_attr(a);

	strlcpy(buf, "(root, wheel, 0755,)", BUFSIZ);
	ATF_REQUIRE((a = parse_keyword_args(buf, "plop")) != NULL);
	ATF_REQUIRE_STREQ(a->owner, "root");
	ATF_REQUIRE_STREQ(a->group, "wheel");
	free_file_attr(a);
}

ATF_TC(parse_keyword);

ATF_TC_HEAD(parse_keyword, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "parse_keyword()");
}

ATF_TC_BODY(parse_keyword, tc)
{
	char *keyword;
	struct file_attr *attr;
	char buf[BUFSIZ];

	strlcpy(buf, "something", BUFSIZ);
	keyword = NULL;
	attr = NULL;
	ATF_REQUIRE_STREQ(extract_keywords(buf, &keyword, &attr), "");
	ATF_REQUIRE_STREQ(keyword, "something");
	ATF_REQUIRE_EQ(attr, NULL);

	/* empty keyword */
	strlcpy(buf, "", BUFSIZ);
	keyword = NULL;
	attr = NULL;
	ATF_REQUIRE_STREQ(extract_keywords(buf, &keyword, &attr), "");
	ATF_REQUIRE_STREQ(keyword, "");
	ATF_REQUIRE_EQ(attr, NULL);

	/* bad keyword */
	strlcpy(buf, "(", BUFSIZ);
	keyword = NULL;
	attr = NULL;
	ATF_REQUIRE_EQ(extract_keywords(buf, &keyword, &attr), NULL);
	ATF_REQUIRE_EQ(keyword, NULL);
	ATF_REQUIRE_EQ(attr, NULL);

	/* bad: empty keyword */
	strlcpy(buf, "()", BUFSIZ);
	keyword = NULL;
	attr = NULL;
	ATF_REQUIRE_EQ(extract_keywords(buf, &keyword, &attr), NULL);
	ATF_REQUIRE_EQ(keyword, NULL);
	ATF_REQUIRE_EQ(attr, NULL);

	/* ok only user keyword */
	strlcpy(buf, "(root) that", BUFSIZ);
	keyword = NULL;
	attr = NULL;
	ATF_REQUIRE_STREQ(extract_keywords(buf, &keyword, &attr), "that");
	ATF_REQUIRE_STREQ(keyword, "");
	ATF_REQUIRE(attr != NULL);
	ATF_REQUIRE_STREQ(attr->owner, "root");

	/* ok only group keyword */
	strlcpy(buf, "(,wheel) that", BUFSIZ);
	keyword = NULL;
	attr = NULL;
	ATF_REQUIRE_STREQ(extract_keywords(buf, &keyword, &attr), "that");
	ATF_REQUIRE_STREQ(keyword, "");
	ATF_REQUIRE(attr != NULL);
	ATF_REQUIRE_STREQ(attr->group, "wheel");

	/* ok only group with space keyword */
	strlcpy(buf, "( , wheel ,) that", BUFSIZ);
	keyword = NULL;
	attr = NULL;
	ATF_REQUIRE_STREQ(extract_keywords(buf, &keyword, &attr), "that");
	ATF_REQUIRE_STREQ(keyword, "");
	ATF_REQUIRE(attr != NULL);
	ATF_REQUIRE_STREQ(attr->group, "wheel");
	ATF_REQUIRE_EQ(attr->owner, NULL);

	strlcpy(buf, "(, wheel ,perm,ffags,)", BUFSIZ);
	ATF_REQUIRE_EQ(parse_keyword_args(buf, "plop"), NULL);
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

	/* On a non existing directory this should not work */
	plist = plist_new(p, "/nonexist");
	ATF_REQUIRE(plist == NULL);

	plist = plist_new(p, NULL);
	ATF_REQUIRE(plist != NULL);
	plist_free(plist);

	plist = plist_new(p, "/tmp");
	ATF_REQUIRE(plist != NULL);

	ATF_REQUIRE(plist->pkg == p);
	ATF_REQUIRE_EQ(plist->prefix[0], '\0');

	strlcpy(buf, "@name name1", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_FATAL, plist_parse_line(plist, buf));

	strlcpy(buf, "@name name1-1", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(plist, buf));
	ATF_REQUIRE_STREQ(p->name, "name1");
	ATF_REQUIRE_STREQ(p->version, "1");

	/* if already set, name should not change */
	strlcpy(buf, "@name name2-2", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(plist, buf));
	ATF_REQUIRE_STREQ(p->name, "name1");
	ATF_REQUIRE_STREQ(p->version, "1");

	strlcpy(buf, "@cwd /myprefix", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(plist, buf));
	ATF_REQUIRE_STREQ(p->prefix, "/myprefix");

	ATF_REQUIRE_STREQ(plist->prefix, "/myprefix");

	ATF_REQUIRE_STREQ(plist->uname, "root");
	ATF_REQUIRE_STREQ(plist->gname, "wheel");

	strlcpy(buf, "@owner bob", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(plist, buf));
	ATF_REQUIRE_STREQ(plist->uname, "bob");

	strlcpy(buf, "@group sponge", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(plist, buf));
	ATF_REQUIRE_STREQ(plist->gname, "sponge");

	strlcpy(buf, "@group", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(plist, buf));
	ATF_REQUIRE_STREQ(plist->gname, "wheel");

	strlcpy(buf, "@owner", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(plist, buf));
	ATF_REQUIRE_STREQ(plist->uname, "root");

	strlcpy(buf, "@cwd plop", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(plist, buf));
	ATF_REQUIRE_STREQ(plist->prefix, "plop");

	strlcpy(buf, "@cwd", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(plist, buf));
	ATF_REQUIRE_STREQ(plist->prefix, "/myprefix");
	ATF_REQUIRE_STREQ(plist->slash, "/");

	strlcpy(buf, "@cwd /another/prefix/", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(plist, buf));
	ATF_REQUIRE_STREQ(plist->prefix, "/another/prefix/");
	ATF_REQUIRE_STREQ(plist->slash, "");

	ATF_REQUIRE_EQ(0, plist->perm);
	strlcpy(buf, "@mode 0755", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(plist, buf));
	ATF_REQUIRE_EQ(0755, plist->perm);

	strlcpy(buf, "@mode", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_OK, plist_parse_line(plist, buf));
	ATF_REQUIRE_EQ(0, plist->perm);

	strlcpy(buf, "@blabla", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_FATAL, plist_parse_line(plist, buf));

	strlcpy(buf, "nonexisting/file", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_FATAL, plist_parse_line(plist, buf));

	strlcpy(buf, "@dir nonexisting", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_FATAL, plist_parse_line(plist, buf));

	strlcpy(buf, "@dirrm nonexisting", BUFSIZ);
	ATF_REQUIRE_EQ(EPKG_FATAL, plist_parse_line(plist, buf));

	pkg_free(p);
	plist_free(plist);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, parse_mode);
	ATF_TP_ADD_TC(tp, parse_plist);
	ATF_TP_ADD_TC(tp, parse_keyword_attributes);
	ATF_TP_ADD_TC(tp, parse_keyword);

	return (atf_no_error());
}
