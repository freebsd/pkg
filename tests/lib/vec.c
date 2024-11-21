/*-
 * Copyright(c) 2024 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <atf-c.h>
#include <private/utils.h>
#include <pkgvec.h>
#include <xmalloc.h>

ATF_TC_WITHOUT_HEAD(c_charv_t);
ATF_TC_WITHOUT_HEAD(c_charv_contains);
ATF_TC_WITHOUT_HEAD(charv_t);

ATF_TC_BODY(c_charv_t, tc)
{
	c_charv_t list;

	pkgvec_init(&list);
	ATF_REQUIRE_EQ_MSG(list.d, NULL, "pkgvec_init failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 0, "pkgvec_init failed");
	ATF_REQUIRE_EQ_MSG(list.len, 0, "pkgvec_init failed");

	pkgvec_push(&list, "test1");
	ATF_REQUIRE_MSG(list.d != NULL, "pkgvec_push failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 1, "pkgvec_push failed");
	ATF_REQUIRE_EQ_MSG(list.len, 1, "pkgvec_push failed");

	pkgvec_push(&list, "test2");
	ATF_REQUIRE_MSG(list.d != NULL, "pkgvec_push2 failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 2, "pkgvec_push2 failed");
	ATF_REQUIRE_EQ_MSG(list.len, 2, "pkgvec_push2 failed");

	pkgvec_push(&list, "test3");
	ATF_REQUIRE_MSG(list.d != NULL, "pkgvec_push3 failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 4, "pkgvec_push3 failed");
	ATF_REQUIRE_EQ_MSG(list.len, 3, "pkgvec_push3 failed");

	ATF_REQUIRE_STREQ_MSG(pkgvec_first(&list), "test1", "pkgvec_first failed");
	ATF_REQUIRE_STREQ_MSG(pkgvec_last(&list), "test3", "pkgvec_last failed");

	pkgvec_clear(&list);
	ATF_REQUIRE_MSG(list.d != NULL, "pkgvec_clear failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 4, "pkgvec_clear failed");
	ATF_REQUIRE_EQ_MSG(list.len, 0, "pkgvec_clear failed");

	pkgvec_free(&list);
	ATF_REQUIRE_EQ_MSG(list.d, NULL, "pkgvec_free failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 0, "pkgvec_free failed");
	ATF_REQUIRE_EQ_MSG(list.len, 0, "pkgvec_free failed");
}

ATF_TC_BODY(charv_t, tc)
{
	charv_t list;

	pkgvec_init(&list);
	ATF_REQUIRE_EQ_MSG(list.d, NULL, "pkgvec_init failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 0, "pkgvec_init failed");
	ATF_REQUIRE_EQ_MSG(list.len, 0, "pkgvec_init failed");

	pkgvec_push(&list, xstrdup("test1"));
	ATF_REQUIRE_MSG(list.d != NULL, "pkgvec_push failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 1, "pkgvec_push failed");
	ATF_REQUIRE_EQ_MSG(list.len, 1, "pkgvec_push failed");

	pkgvec_push(&list, xstrdup("test2"));
	ATF_REQUIRE_MSG(list.d != NULL, "pkgvec_push2 failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 2, "pkgvec_push2 failed");
	ATF_REQUIRE_EQ_MSG(list.len, 2, "pkgvec_push2 failed");

	pkgvec_push(&list, xstrdup("test3"));
	ATF_REQUIRE_MSG(list.d != NULL, "pkgvec_push3 failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 4, "pkgvec_push3 failed");
	ATF_REQUIRE_EQ_MSG(list.len, 3, "pkgvec_push3 failed");

	ATF_REQUIRE_STREQ_MSG(pkgvec_first(&list), "test1", "pkgvec_first failed");
	ATF_REQUIRE_STREQ_MSG(pkgvec_last(&list), "test3", "pkgvec_last failed");

	pkgvec_clear_and_free(&list, free);
	ATF_REQUIRE_MSG(list.d != NULL, "pkgvec_clear failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 4, "pkgvec_clear failed");
	ATF_REQUIRE_EQ_MSG(list.len, 0, "pkgvec_clear failed");

	pkgvec_free_and_free(&list, free);
	ATF_REQUIRE_EQ_MSG(list.d, NULL, "pkgvec_free failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 0, "pkgvec_free failed");
	ATF_REQUIRE_EQ_MSG(list.len, 0, "pkgvec_free failed");
}

ATF_TC_BODY(c_charv_contains, tc)
{
	c_charv_t list;

	pkgvec_init(&list);
	ATF_REQUIRE_EQ_MSG(list.d, NULL, "pkgvec_init failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 0, "pkgvec_init failed");
	ATF_REQUIRE_EQ_MSG(list.len, 0, "pkgvec_init failed");

	pkgvec_push(&list, "test1");
	ATF_REQUIRE_MSG(list.d != NULL, "pkgvec_push failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 1, "pkgvec_push failed");
	ATF_REQUIRE_EQ_MSG(list.len, 1, "pkgvec_push failed");

	pkgvec_push(&list, "test2");
	ATF_REQUIRE_MSG(list.d != NULL, "pkgvec_push2 failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 2, "pkgvec_push2 failed");
	ATF_REQUIRE_EQ_MSG(list.len, 2, "pkgvec_push2 failed");

	pkgvec_push(&list, "test3");
	ATF_REQUIRE_MSG(list.d != NULL, "pkgvec_push3 failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 4, "pkgvec_push3 failed");
	ATF_REQUIRE_EQ_MSG(list.len, 3, "pkgvec_push3 failed");

	ATF_REQUIRE_EQ_MSG(c_charv_contains(&list, "Test3", true), false, "c_charv_contains not case sensitive");
	ATF_REQUIRE_EQ_MSG(c_charv_contains(&list, "Test3", false), true, "c_charv_contains not case insensitive");
	ATF_REQUIRE_EQ_MSG(c_charv_contains(&list, "aest3", false), false, "c_charv_contains should not find anything");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, c_charv_t);
	ATF_TP_ADD_TC(tp, charv_t);
	ATF_TP_ADD_TC(tp, c_charv_contains);

	return (atf_no_error());
}
