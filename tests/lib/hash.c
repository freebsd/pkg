/*
 * Copyright (c) 2024 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <atf-c.h>
#include <hash.h>
#include <xmalloc.h>

ATF_TC_WITHOUT_HEAD(hash);
ATF_TC_WITHOUT_HEAD(hash_null_safety);
ATF_TC_WITHOUT_HEAD(hash_safe_add);

ATF_TC_BODY(hash, tc)
{
	hash_t *h = hash_new();
	ATF_REQUIRE_EQ(hash_count(h), 0);
	ATF_REQUIRE(hash_add(h, "key", "value", NULL));
	ATF_REQUIRE_EQ(hash_count(h), 1);
	ATF_REQUIRE(!hash_del(h, "plop"));
	ATF_REQUIRE_EQ(hash_count(h), 1);
	ATF_REQUIRE(hash_del(h, "key"));
	ATF_REQUIRE_EQ(hash_count(h), 0);
	char *val = xstrdup("value");
	ATF_REQUIRE(hash_add(h, "key", val, free));
	ATF_REQUIRE_EQ(hash_count(h), 1);
	ATF_REQUIRE_STREQ((char *)hash_delete(h, "key"), "value");
	ATF_REQUIRE_STREQ(val, "value");
	ATF_REQUIRE_EQ(hash_count(h), 0);
	ATF_REQUIRE(hash_add(h, "key", val, free));
	ATF_REQUIRE_EQ(hash_count(h), 1);
	ATF_REQUIRE(hash_del(h, "key"));
	ATF_REQUIRE_EQ(hash_count(h), 0);
	val = xstrdup("value");
	ATF_REQUIRE(hash_add(h, "key", val, free));
	ATF_REQUIRE_EQ(hash_delete(h, "bla"), NULL);
	hash_destroy(h);
}

ATF_TC_BODY(hash_null_safety, tc)
{
	hash_t *h = hash_new();
	hash_it it = hash_iterator(NULL);

	/* Every entry point must tolerate a NULL table. */
	ATF_REQUIRE_MSG(!hash_add(NULL, "key", "value", NULL), "hash_add(NULL table)");
	ATF_REQUIRE_MSG(hash_get(NULL, "key") == NULL, "hash_get(NULL table)");
	ATF_REQUIRE_MSG(hash_get_value(NULL, "key") == NULL, "hash_get_value(NULL table)");
	ATF_REQUIRE_MSG(!hash_del(NULL, "key"), "hash_del(NULL table)");
	ATF_REQUIRE_MSG(hash_delete(NULL, "key") == NULL, "hash_delete(NULL table)");
	ATF_REQUIRE_EQ_MSG(hash_count(NULL), 0, "hash_count(NULL table)");
	ATF_REQUIRE_MSG(!hash_next(&it), "hash_next on a NULL table");
	hash_destroy(NULL);

	/* ... and a NULL key, which is never stored. */
	ATF_REQUIRE_MSG(!hash_add(h, NULL, "value", NULL), "hash_add(NULL key)");
	ATF_REQUIRE_MSG(hash_get(h, NULL) == NULL, "hash_get(NULL key)");
	ATF_REQUIRE_MSG(hash_get_value(h, NULL) == NULL, "hash_get_value(NULL key)");
	ATF_REQUIRE_MSG(!hash_del(h, NULL), "hash_del(NULL key)");
	ATF_REQUIRE_MSG(hash_delete(h, NULL) == NULL, "hash_delete(NULL key)");
	ATF_REQUIRE_EQ_MSG(hash_count(h), 0, "a NULL key was stored");
	hash_destroy(h);
}

ATF_TC_BODY(hash_safe_add, tc)
{
	hash_t *h = NULL;
	char *first = xstrdup("first");
	char *dup = xstrdup("duplicate");

	/* The table is created by the first add. */
	ATF_REQUIRE_MSG(hash_safe_add(h, "key", first, free), "first hash_safe_add");
	ATF_REQUIRE_MSG(h != NULL, "hash_safe_add did not create the table");
	ATF_REQUIRE_EQ_MSG(hash_count(h), 1, "count after the first add");

	/* The duplicate is not stored: the caller still owns its value. */
	ATF_REQUIRE_MSG(!hash_safe_add(h, "key", dup, free), "duplicate hash_safe_add");
	ATF_REQUIRE_EQ_MSG(hash_count(h), 1, "count after the duplicate add");
	ATF_REQUIRE_MSG(hash_get_value(h, "key") == first, "existing value replaced");
	free(dup);

	hash_destroy(h);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, hash);
	ATF_TP_ADD_TC(tp, hash_null_safety);
	ATF_TP_ADD_TC(tp, hash_safe_add);

	return (atf_no_error());
}
