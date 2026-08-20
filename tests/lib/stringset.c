/*-
 * Copyright(c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <atf-c.h>
#include <stdio.h>
#include <pkg/stringset.h>

ATF_TC_WITHOUT_HEAD(stringset_new);
ATF_TC_WITHOUT_HEAD(stringset_add);
ATF_TC_WITHOUT_HEAD(stringset_contains);
ATF_TC_WITHOUT_HEAD(stringset_del);
ATF_TC_WITHOUT_HEAD(stringset_count);
ATF_TC_WITHOUT_HEAD(stringset_foreach);
ATF_TC_WITHOUT_HEAD(stringset_grow);
ATF_TC_WITHOUT_HEAD(stringset_tombstone_reuse);
ATF_TC_WITHOUT_HEAD(stringset_null);

ATF_TC_BODY(stringset_new, tc)
{
	stringset_t *set = stringset_new();
	ATF_REQUIRE_MSG(set != NULL, "stringset_new returned NULL");
	ATF_REQUIRE_EQ_MSG(stringset_count(set), 0, "new set should be empty");
	stringset_destroy(set);
}

ATF_TC_BODY(stringset_add, tc)
{
	stringset_t *set = stringset_new();

	ATF_REQUIRE_MSG(stringset_add(set, "foo"), "first add should return true");
	ATF_REQUIRE_EQ_MSG(stringset_count(set), 1, "count mismatch after add");

	/* Adding a duplicate returns false and does not grow the set. */
	ATF_REQUIRE_MSG(!stringset_add(set, "foo"), "duplicate add should return false");
	ATF_REQUIRE_EQ_MSG(stringset_count(set), 1, "duplicate add changed count");

	ATF_REQUIRE_MSG(stringset_add(set, "bar"), "second distinct add should return true");
	ATF_REQUIRE_EQ_MSG(stringset_count(set), 2, "count mismatch after 2nd add");

	stringset_destroy(set);
}

ATF_TC_BODY(stringset_contains, tc)
{
	stringset_t *set = stringset_new();

	ATF_REQUIRE_MSG(!stringset_contains(set, "foo"), "empty set should not contain foo");

	stringset_add(set, "foo");
	stringset_add(set, "bar");
	stringset_add(set, "baz");

	ATF_REQUIRE_MSG(stringset_contains(set, "foo"), "set should contain foo");
	ATF_REQUIRE_MSG(stringset_contains(set, "bar"), "set should contain bar");
	ATF_REQUIRE_MSG(stringset_contains(set, "baz"), "set should contain baz");
	ATF_REQUIRE_MSG(!stringset_contains(set, "qux"), "set should not contain qux");
	ATF_REQUIRE_MSG(!stringset_contains(set, "fo"), "set should not contain prefix fo");

	stringset_destroy(set);
}

ATF_TC_BODY(stringset_del, tc)
{
	stringset_t *set = stringset_new();

	stringset_add(set, "foo");
	stringset_add(set, "bar");
	ATF_REQUIRE_EQ_MSG(stringset_count(set), 2, "count mismatch before del");

	ATF_REQUIRE_MSG(stringset_del(set, "foo"), "del of present key should return true");
	ATF_REQUIRE_EQ_MSG(stringset_count(set), 1, "count mismatch after del");
	ATF_REQUIRE_MSG(!stringset_contains(set, "foo"), "deleted key should not be present");

	/* Deleting a non-present key returns false. */
	ATF_REQUIRE_MSG(!stringset_del(set, "foo"), "del of absent key should return false");
	ATF_REQUIRE_MSG(!stringset_del(set, "nope"), "del of never-added key should return false");
	ATF_REQUIRE_EQ_MSG(stringset_count(set), 1, "count changed after failed del");

	ATF_REQUIRE_MSG(stringset_contains(set, "bar"), "remaining key should still be present");

	stringset_destroy(set);
}

ATF_TC_BODY(stringset_count, tc)
{
	stringset_t *set = stringset_new();

	ATF_REQUIRE_EQ_MSG(stringset_count(set), 0, "empty set count");

	for (int i = 0; i < 100; i++) {
		char key[16];
		snprintf(key, sizeof(key), "key%d", i);
		stringset_add(set, key);
	}
	ATF_REQUIRE_EQ_MSG(stringset_count(set), 100, "count mismatch after 100 adds");

	/* Duplicates do not change the count. */
	stringset_add(set, "key0");
	stringset_add(set, "key50");
	ATF_REQUIRE_EQ_MSG(stringset_count(set), 100, "count changed after duplicate adds");

	stringset_destroy(set);
}

ATF_TC_BODY(stringset_foreach, tc)
{
	stringset_t *set = stringset_new();

	stringset_add(set, "alpha");
	stringset_add(set, "beta");
	stringset_add(set, "gamma");

	int seen = 0;
	bool saw_alpha = false, saw_beta = false, saw_gamma = false;
	stringset_it it;
	stringset_foreach(set, it) {
		seen++;
		if (strcmp(it, "alpha") == 0)
			saw_alpha = true;
		else if (strcmp(it, "beta") == 0)
			saw_beta = true;
		else if (strcmp(it, "gamma") == 0)
			saw_gamma = true;
	}
	ATF_REQUIRE_EQ_MSG(seen, 3, "foreach visited wrong number of elements");
	ATF_REQUIRE_MSG(saw_alpha, "foreach missed alpha");
	ATF_REQUIRE_MSG(saw_beta, "foreach missed beta");
	ATF_REQUIRE_MSG(saw_gamma, "foreach missed gamma");

	stringset_destroy(set);
}

ATF_TC_BODY(stringset_grow, tc)
{
	stringset_t *set = stringset_new();

	/* Force multiple expansions past the initial 128 capacity. */
	for (int i = 0; i < 1000; i++) {
		char key[16];
		snprintf(key, sizeof(key), "key%d", i);
		ATF_REQUIRE_MSG(stringset_add(set, key), "add failed during growth");
	}
	ATF_REQUIRE_EQ_MSG(stringset_count(set), 1000, "count mismatch after growth");

	/* All keys must still be present after expansion. */
	for (int i = 0; i < 1000; i++) {
		char key[16];
		snprintf(key, sizeof(key), "key%d", i);
		ATF_REQUIRE_MSG(stringset_contains(set, key), "key lost after expansion");
	}

	stringset_destroy(set);
}

ATF_TC_BODY(stringset_tombstone_reuse, tc)
{
	stringset_t *set = stringset_new();

	/* Add and delete many keys to create tombstones. */
	for (int i = 0; i < 200; i++) {
		char key[16];
		snprintf(key, sizeof(key), "key%d", i);
		stringset_add(set, key);
	}
	for (int i = 0; i < 200; i++) {
		char key[16];
		snprintf(key, sizeof(key), "key%d", i);
		ATF_REQUIRE_MSG(stringset_del(set, key), "del failed during tombstone creation");
	}
	ATF_REQUIRE_EQ_MSG(stringset_count(set), 0, "count not zero after deleting all");

	/* Re-add the same keys: must not create duplicates. */
	for (int i = 0; i < 200; i++) {
		char key[16];
		snprintf(key, sizeof(key), "key%d", i);
		ATF_REQUIRE_MSG(stringset_add(set, key), "re-add failed");
	}
	ATF_REQUIRE_EQ_MSG(stringset_count(set), 200, "count mismatch after re-add");

	/* No duplicates: each key appears exactly once. */
	int total = 0;
	stringset_it it;
	stringset_foreach(set, it) {
		total++;
	}
	ATF_REQUIRE_EQ_MSG(total, 200, "duplicate entries after tombstone reuse");

	stringset_destroy(set);
}

ATF_TC_BODY(stringset_null, tc)
{
	/* NULL-safe operations. */
	ATF_REQUIRE_EQ_MSG(stringset_count(NULL), 0, "count(NULL) should be 0");
	ATF_REQUIRE_MSG(!stringset_contains(NULL, "foo"), "contains(NULL) should be false");

	stringset_destroy(NULL);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, stringset_new);
	ATF_TP_ADD_TC(tp, stringset_add);
	ATF_TP_ADD_TC(tp, stringset_contains);
	ATF_TP_ADD_TC(tp, stringset_del);
	ATF_TP_ADD_TC(tp, stringset_count);
	ATF_TP_ADD_TC(tp, stringset_foreach);
	ATF_TP_ADD_TC(tp, stringset_grow);
	ATF_TP_ADD_TC(tp, stringset_tombstone_reuse);
	ATF_TP_ADD_TC(tp, stringset_null);

	return (atf_no_error());
}
