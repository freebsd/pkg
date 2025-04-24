/*-
 * Copyright(c) 2024 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <atf-c.h>
#include <private/utils.h>
#include <xmalloc.h>
#include <pkg/vec.h>

ATF_TC_WITHOUT_HEAD(c_charv_t);
ATF_TC_WITHOUT_HEAD(c_charv_contains);
ATF_TC_WITHOUT_HEAD(charv_t);
ATF_TC_WITHOUT_HEAD(vec_remove_and_free);
ATF_TC_WITHOUT_HEAD(charv_search);

ATF_TC_BODY(c_charv_t, tc)
{
	c_charv_t list;

	vec_init(&list);
	ATF_REQUIRE_EQ_MSG(list.d, NULL, "vec_init failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 0, "vec_init failed");
	ATF_REQUIRE_EQ_MSG(list.len, 0, "vec_init failed");

	vec_push(&list, "test1");
	ATF_REQUIRE_MSG(list.d != NULL, "vec_push failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 1, "vec_push failed");
	ATF_REQUIRE_EQ_MSG(list.len, 1, "vec_push failed");

	vec_push(&list, "test2");
	ATF_REQUIRE_MSG(list.d != NULL, "vec_push2 failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 2, "vec_push2 failed");
	ATF_REQUIRE_EQ_MSG(list.len, 2, "vec_push2 failed");

	vec_push(&list, "test3");
	ATF_REQUIRE_MSG(list.d != NULL, "vec_push3 failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 4, "vec_push3 failed");
	ATF_REQUIRE_EQ_MSG(list.len, 3, "vec_push3 failed");

	ATF_REQUIRE_STREQ_MSG(vec_first(&list), "test1", "vec_first failed");
	ATF_REQUIRE_STREQ_MSG(vec_last(&list), "test3", "vec_last failed");

	vec_clear(&list);
	ATF_REQUIRE_MSG(list.d != NULL, "vec_clear failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 4, "vec_clear failed");
	ATF_REQUIRE_EQ_MSG(list.len, 0, "vec_clear failed");

	vec_free(&list);
	ATF_REQUIRE_EQ_MSG(list.d, NULL, "vec_free failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 0, "vec_free failed");
	ATF_REQUIRE_EQ_MSG(list.len, 0, "vec_free failed");
}

ATF_TC_BODY(charv_t, tc)
{
	charv_t list;

	vec_init(&list);
	ATF_REQUIRE_EQ_MSG(list.d, NULL, "vec_init failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 0, "vec_init failed");
	ATF_REQUIRE_EQ_MSG(list.len, 0, "vec_init failed");

	vec_push(&list, xstrdup("test1"));
	ATF_REQUIRE_MSG(list.d != NULL, "vec_push failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 1, "vec_push failed");
	ATF_REQUIRE_EQ_MSG(list.len, 1, "vec_push failed");

	vec_push(&list, xstrdup("test2"));
	ATF_REQUIRE_MSG(list.d != NULL, "vec_push2 failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 2, "vec_push2 failed");
	ATF_REQUIRE_EQ_MSG(list.len, 2, "vec_push2 failed");

	vec_push(&list, xstrdup("test3"));
	ATF_REQUIRE_MSG(list.d != NULL, "vec_push3 failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 4, "vec_push3 failed");
	ATF_REQUIRE_EQ_MSG(list.len, 3, "vec_push3 failed");

	ATF_REQUIRE_STREQ_MSG(vec_first(&list), "test1", "vec_first failed");
	ATF_REQUIRE_STREQ_MSG(vec_last(&list), "test3", "vec_last failed");

	vec_clear_and_free(&list, free);
	ATF_REQUIRE_MSG(list.d != NULL, "vec_clear failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 4, "vec_clear failed");
	ATF_REQUIRE_EQ_MSG(list.len, 0, "vec_clear failed");

	vec_free_and_free(&list, free);
	ATF_REQUIRE_EQ_MSG(list.d, NULL, "vec_free failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 0, "vec_free failed");
	ATF_REQUIRE_EQ_MSG(list.len, 0, "vec_free failed");
}

ATF_TC_BODY(c_charv_contains, tc)
{
	c_charv_t list;

	vec_init(&list);
	ATF_REQUIRE_EQ_MSG(list.d, NULL, "vec_init failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 0, "vec_init failed");
	ATF_REQUIRE_EQ_MSG(list.len, 0, "vec_init failed");

	vec_push(&list, "test1");
	ATF_REQUIRE_MSG(list.d != NULL, "vec_push failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 1, "vec_push failed");
	ATF_REQUIRE_EQ_MSG(list.len, 1, "vec_push failed");

	vec_push(&list, "test2");
	ATF_REQUIRE_MSG(list.d != NULL, "vec_push2 failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 2, "vec_push2 failed");
	ATF_REQUIRE_EQ_MSG(list.len, 2, "vec_push2 failed");

	vec_push(&list, "test3");
	ATF_REQUIRE_MSG(list.d != NULL, "vec_push3 failed");
	ATF_REQUIRE_EQ_MSG(list.cap, 4, "vec_push3 failed");
	ATF_REQUIRE_EQ_MSG(list.len, 3, "vec_push3 failed");

	ATF_REQUIRE_EQ_MSG(c_charv_contains(&list, "Test3", true), false, "c_charv_contains not case sensitive");
	ATF_REQUIRE_EQ_MSG(c_charv_contains(&list, "Test3", false), true, "c_charv_contains not case insensitive");
	ATF_REQUIRE_EQ_MSG(c_charv_contains(&list, "aest3", false), false, "c_charv_contains should not find anything");

	vec_free(&list);
}

ATF_TC_BODY(vec_remove_and_free, tc)
{
	charv_t list;

	vec_init(&list);
	vec_push(&list, xstrdup("test1"));
	ATF_REQUIRE_EQ(list.len, 1);
	vec_remove_and_free(&list, 0, free);
	ATF_REQUIRE_EQ(list.len, 0);
	vec_push(&list, xstrdup("test2"));
	vec_push(&list, xstrdup("test3"));
	vec_push(&list, xstrdup("test4"));
	ATF_REQUIRE_EQ(list.len, 3);
	vec_foreach(list, i) {
		if (strcmp(list.d[i], "test3") == 0) {
			vec_remove_and_free(&list, i, free);
		}
	}
	ATF_REQUIRE_EQ(list.len, 2);
	ATF_REQUIRE_STREQ(list.d[0], "test2");
	ATF_REQUIRE_STREQ(list.d[1], "test4");
}

ATF_TC_BODY(charv_search, tc)
{
	charv_t list;
	vec_init(&list);

	ATF_REQUIRE(charv_search(&list, "key") == NULL);
	vec_push(&list, xstrdup("bla"));
	ATF_REQUIRE(charv_search(&list, "key") == NULL);
	ATF_REQUIRE_STREQ(charv_search(&list, "bla"), "bla");
	vec_free_and_free(&list, free);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, c_charv_t);
	ATF_TP_ADD_TC(tp, charv_t);
	ATF_TP_ADD_TC(tp, c_charv_contains);
	ATF_TP_ADD_TC(tp, vec_remove_and_free);
	ATF_TP_ADD_TC(tp, charv_search);

	return (atf_no_error());
}
