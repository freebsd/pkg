/*-
 * Copyright(c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <atf-c.h>
#include <pkg/sb.h>

ATF_TC_WITHOUT_HEAD(sb_init);
ATF_TC_WITHOUT_HEAD(sb_cat);
ATF_TC_WITHOUT_HEAD(sb_cat_c);
ATF_TC_WITHOUT_HEAD(sb_cat_n);
ATF_TC_WITHOUT_HEAD(sb_printf);
ATF_TC_WITHOUT_HEAD(sb_get);
ATF_TC_WITHOUT_HEAD(sb_str);
ATF_TC_WITHOUT_HEAD(sb_reset);
ATF_TC_WITHOUT_HEAD(sb_fini);
ATF_TC_WITHOUT_HEAD(sb_grow);
ATF_TC_WITHOUT_HEAD(sb_empty_get);

ATF_TC_BODY(sb_init, tc)
{
	sb_t sb = sb_init();

	ATF_REQUIRE_EQ_MSG(sb.d, NULL, "sb_init: d should be NULL");
	ATF_REQUIRE_EQ_MSG(sb.len, 0, "sb_init: len should be 0");
	ATF_REQUIRE_EQ_MSG(sb.cap, 0, "sb_init: cap should be 0");
}

ATF_TC_BODY(sb_cat, tc)
{
	sb_t sb = sb_init();

	sb_cat(&sb, "hello");
	ATF_REQUIRE_EQ_MSG(sb.len, 5, "sb_cat: len mismatch");
	ATF_REQUIRE_STREQ_MSG(sb.d, "hello", "sb_cat: content mismatch");

	sb_cat(&sb, " world");
	ATF_REQUIRE_EQ_MSG(sb.len, 11, "sb_cat: len mismatch after 2nd cat");
	ATF_REQUIRE_STREQ_MSG(sb.d, "hello world", "sb_cat: content mismatch after 2nd cat");

	/* Buffer must remain NUL-terminated. */
	ATF_REQUIRE_EQ_MSG(sb.d[sb.len], '\0', "sb_cat: not NUL-terminated");

	sb_fini(&sb);
}

ATF_TC_BODY(sb_cat_c, tc)
{
	sb_t sb = sb_init();

	sb_cat_c(&sb, 'a');
	sb_cat_c(&sb, 'b');
	sb_cat_c(&sb, 'c');
	ATF_REQUIRE_EQ_MSG(sb.len, 3, "sb_cat_c: len mismatch");
	ATF_REQUIRE_STREQ_MSG(sb.d, "abc", "sb_cat_c: content mismatch");
	ATF_REQUIRE_EQ_MSG(sb.d[sb.len], '\0', "sb_cat_c: not NUL-terminated");

	sb_fini(&sb);
}

ATF_TC_BODY(sb_cat_n, tc)
{
	sb_t sb = sb_init();

	sb_cat_n(&sb, "hello world", 5);
	ATF_REQUIRE_EQ_MSG(sb.len, 5, "sb_cat_n: len mismatch");
	ATF_REQUIRE_STREQ_MSG(sb.d, "hello", "sb_cat_n: content mismatch");

	/* n == 0 is a no-op. */
	sb_cat_n(&sb, "ignored", 0);
	ATF_REQUIRE_EQ_MSG(sb.len, 5, "sb_cat_n: zero-length append changed len");

	sb_fini(&sb);
}

ATF_TC_BODY(sb_printf, tc)
{
	sb_t sb = sb_init();

	sb_printf(&sb, "answer = %d", 42);
	ATF_REQUIRE_STREQ_MSG(sb.d, "answer = 42", "sb_printf: content mismatch");

	sb_printf(&sb, " and %s", "done");
	ATF_REQUIRE_STREQ_MSG(sb.d, "answer = 42 and done", "sb_printf: append mismatch");

	/* Long format that forces multiple growth steps. */
	sb_printf(&sb, " %s", "x");
	for (int i = 0; i < 100; i++)
		sb_printf(&sb, "0123456789");
	ATF_REQUIRE_EQ_MSG(sb.len, 1022, "sb_printf: long append len mismatch");

	sb_fini(&sb);
}

ATF_TC_BODY(sb_get, tc)
{
	sb_t sb = sb_init();

	sb_cat(&sb, "transfer me");
	char *s = sb_get(&sb);
	ATF_REQUIRE_STREQ_MSG(s, "transfer me", "sb_get: content mismatch");
	ATF_REQUIRE_EQ_MSG(sb.d, NULL, "sb_get: ownership not transferred (d not NULL)");
	ATF_REQUIRE_EQ_MSG(sb.len, 0, "sb_get: len not reset");
	ATF_REQUIRE_EQ_MSG(sb.cap, 0, "sb_get: cap not reset");
	free(s);
}

ATF_TC_BODY(sb_empty_get, tc)
{
	sb_t sb = sb_init();

	/* sb_get on an empty buffer must return a valid empty string. */
	char *s = sb_get(&sb);
	ATF_REQUIRE_MSG(s != NULL, "sb_get: empty buffer returned NULL");
	ATF_REQUIRE_STREQ_MSG(s, "", "sb_get: empty buffer should be empty string");
	free(s);
}

ATF_TC_BODY(sb_str, tc)
{
	sb_t sb = sb_init();

	/* sb_str on an empty buffer must return a valid empty string. */
	ATF_REQUIRE_STREQ_MSG(sb_str(&sb), "", "sb_str: empty buffer should be empty string");

	sb_cat(&sb, "peek");
	ATF_REQUIRE_STREQ_MSG(sb_str(&sb), "peek", "sb_str: content mismatch");

	/* sb_str does not transfer ownership. */
	ATF_REQUIRE_MSG(sb.d != NULL, "sb_str: should not transfer ownership");

	sb_fini(&sb);
}

ATF_TC_BODY(sb_reset, tc)
{
	sb_t sb = sb_init();

	sb_cat(&sb, "to be reset");
	ATF_REQUIRE_EQ_MSG(sb.len, 11, "sb_reset: pre-reset len mismatch");

	sb_reset(&sb);
	ATF_REQUIRE_EQ_MSG(sb.len, 0, "sb_reset: len not reset");
	ATF_REQUIRE_STREQ_MSG(sb.d, "", "sb_reset: content not cleared");

	/* Reuse after reset. */
	sb_cat(&sb, "again");
	ATF_REQUIRE_STREQ_MSG(sb.d, "again", "sb_reset: reuse after reset failed");

	sb_fini(&sb);
}

ATF_TC_BODY(sb_fini, tc)
{
	sb_t sb = sb_init();

	sb_cat(&sb, "data");
	sb_fini(&sb);
	ATF_REQUIRE_EQ_MSG(sb.d, NULL, "sb_fini: d not freed");
	ATF_REQUIRE_EQ_MSG(sb.len, 0, "sb_fini: len not reset");
	ATF_REQUIRE_EQ_MSG(sb.cap, 0, "sb_fini: cap not reset");

	/* sb_fini(NULL) is a no-op. */
	sb_fini(NULL);
}

ATF_TC_BODY(sb_grow, tc)
{
	sb_t sb = sb_init();

	/* Append enough data to force multiple growth steps. */
	for (int i = 0; i < 10000; i++)
		sb_cat_c(&sb, 'x');

	ATF_REQUIRE_EQ_MSG(sb.len, 10000, "sb_grow: len mismatch after growth");
	ATF_REQUIRE_MSG(sb.cap >= sb.len + 1, "sb_grow: cap too small");
	ATF_REQUIRE_EQ_MSG(sb.d[sb.len], '\0', "sb_grow: not NUL-terminated after growth");

	/* Verify content integrity. */
	for (size_t i = 0; i < sb.len; i++)
		ATF_REQUIRE_EQ_MSG(sb.d[i], 'x', "sb_grow: content corrupted");

	sb_fini(&sb);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, sb_init);
	ATF_TP_ADD_TC(tp, sb_cat);
	ATF_TP_ADD_TC(tp, sb_cat_c);
	ATF_TP_ADD_TC(tp, sb_cat_n);
	ATF_TP_ADD_TC(tp, sb_printf);
	ATF_TP_ADD_TC(tp, sb_get);
	ATF_TP_ADD_TC(tp, sb_empty_get);
	ATF_TP_ADD_TC(tp, sb_str);
	ATF_TP_ADD_TC(tp, sb_reset);
	ATF_TP_ADD_TC(tp, sb_fini);
	ATF_TP_ADD_TC(tp, sb_grow);

	return (atf_no_error());
}
