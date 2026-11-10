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

ATF_TC_WITHOUT_HEAD(pkg_add_dir_to_del);

ATF_TC_BODY(pkg_add_dir_to_del, tc)
{
	struct pkg *p = NULL;

	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_FILE));
	pkg_set(p, PKG_ATTR_PREFIX, "/usr/local");

	ATF_REQUIRE_EQ(vec_len(&p->dir_to_del), 0);

	/* the parent of a file is queued with a trailing slash */
	pkg_add_dir_to_del(p, "/usr/local/plop/bla", NULL);
	ATF_REQUIRE_EQ(vec_len(&p->dir_to_del), 1);
	ATF_REQUIRE_STREQ(p->dir_to_del.d[0].path, "/usr/local/plop/");
	ATF_REQUIRE(p->dir_to_del.d[0].under_prefix);

	/* a directory out of the prefix is queued too, but kept aside */
	pkg_add_dir_to_del(p, NULL, "/var/run/yeah");
	ATF_REQUIRE_EQ(vec_len(&p->dir_to_del), 2);
	ATF_REQUIRE_STREQ(p->dir_to_del.d[1].path, "/var/run/yeah/");
	ATF_REQUIRE(!p->dir_to_del.d[1].under_prefix);

	/* a queued ancestor is dropped in favour of its descendants, a
	 * directory out of the prefix is never dropped */
	pkg_add_dir_to_del(p, NULL, "/usr/local/plop");
	pkg_add_dir_to_del(p, NULL, "/usr/local");
	pkg_dir_to_del_prune(p);

	ATF_REQUIRE_EQ(vec_len(&p->dir_to_del), 2);
	ATF_REQUIRE_STREQ(p->dir_to_del.d[0].path, "/var/run/yeah/");
	ATF_REQUIRE_STREQ(p->dir_to_del.d[1].path, "/usr/local/plop/");

	/* a deeper directory replaces the queued one */
	pkg_add_dir_to_del(p, NULL, "/usr/local/plop/other");
	pkg_dir_to_del_prune(p);

	ATF_REQUIRE_EQ(vec_len(&p->dir_to_del), 2);
	ATF_REQUIRE_STREQ(p->dir_to_del.d[1].path, "/usr/local/plop/other/");

	/* the same directory queued twice is only kept once */
	pkg_add_dir_to_del(p, NULL, "/usr/local/plop/other");
	pkg_dir_to_del_prune(p);

	ATF_REQUIRE_EQ(vec_len(&p->dir_to_del), 2);

	pkg_free(p);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, pkg_add_dir_to_del);

	return (atf_no_error());
}
