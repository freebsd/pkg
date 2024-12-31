/*
 * Copyright (c) 2024 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <atf-c.h>
#include <pkghash.h>
#include <xmalloc.h>

ATF_TC_WITHOUT_HEAD(hash);

ATF_TC_BODY(hash, tc)
{
	struct pkghash *h = pkghash_new();
	ATF_REQUIRE_EQ(pkghash_count(h), 0);
	ATF_REQUIRE(pkghash_add(h, "key", "value", NULL));
	ATF_REQUIRE_EQ(pkghash_count(h), 1);
	ATF_REQUIRE(!pkghash_del(h, "plop"));
	ATF_REQUIRE_EQ(pkghash_count(h), 1);
	ATF_REQUIRE(pkghash_del(h, "key"));
	ATF_REQUIRE_EQ(pkghash_count(h), 0);
	char *val = xstrdup("value");
	ATF_REQUIRE(pkghash_add(h, "key", val, free));
	ATF_REQUIRE_EQ(pkghash_count(h), 1);
	ATF_REQUIRE_STREQ((char *)pkghash_delete(h, "key"), "value");
	ATF_REQUIRE_STREQ(val, "value");
	ATF_REQUIRE_EQ(pkghash_count(h), 0);
	ATF_REQUIRE(pkghash_add(h, "key", val, free));
	ATF_REQUIRE_EQ(pkghash_count(h), 1);
	ATF_REQUIRE(pkghash_del(h, "key"));
	ATF_REQUIRE_EQ(pkghash_count(h), 0);
	val = xstrdup("value");
	ATF_REQUIRE(pkghash_add(h, "key", val, free));
	ATF_REQUIRE_EQ(pkghash_delete(h, "bla"), NULL);
	pkghash_destroy(h);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, hash);

	return (atf_no_error());
}
