/*-
 * Copyright(c) 2025 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <atf-c.h>
#include <private/pkg.h>

ATF_TC_WITHOUT_HEAD(kv_sort);
ATF_TC_WITHOUT_HEAD(kv_insert_sorted);
ATF_TC_WITHOUT_HEAD(kv_search);

ATF_TC_BODY(kv_insert_sorted, tc)
{
	kvlist_t kvl = vec_init();

	ATF_REQUIRE_EQ_MSG(kvl.d, NULL, "vec_init failed");
	ATF_REQUIRE_EQ_MSG(kvl.cap, 0, "vec_init failed");
	ATF_REQUIRE_EQ_MSG(kvl.len, 0, "vec_init failed");

	struct pkg_kv *kv = pkg_kv_new("key", "value");
	ATF_REQUIRE_EQ(pkg_kv_insert_sorted(&kvl, kv), NULL);
	ATF_REQUIRE_EQ(kvl.len, 1);
	ATF_REQUIRE(pkg_kv_insert_sorted(&kvl, kv) != NULL);
	ATF_REQUIRE_EQ(kvl.len, 1);

	kv = pkg_kv_new("akey", "value");
	ATF_REQUIRE(pkg_kv_insert_sorted(&kvl, kv) != NULL);
	ATF_REQUIRE_EQ(kvl.len, 2);
	ATF_REQUIRE_STREQ(kvl.d[0]->key, "akey");
	ATF_REQUIRE_STREQ(kvl.d[1]->key, "key");
}

ATF_TC_BODY(kv_sort, tc)
{
	kvlist_t kvl = vec_init();

	ATF_REQUIRE_EQ_MSG(kvl.d, NULL, "vec_init failed");
	ATF_REQUIRE_EQ_MSG(kvl.cap, 0, "vec_init failed");
	ATF_REQUIRE_EQ_MSG(kvl.len, 0, "vec_init failed");

	struct pkg_kv *kv = pkg_kv_new("key", "value");
	vec_push(&kvl, kv);
	ATF_REQUIRE_MSG(kvl.d != NULL, "vec_push failed");
	ATF_REQUIRE_EQ_MSG(kvl.cap, 1, "vec_push failed");
	ATF_REQUIRE_EQ_MSG(kvl.len, 1, "vec_push failed");

	pkg_kv_sort(&kvl);

	kv = pkg_kv_new("akey", "value");
	vec_push(&kvl, kv);
	ATF_REQUIRE_EQ_MSG(kvl.cap, 2, "vec_push failed");
	ATF_REQUIRE_EQ_MSG(kvl.len, 2, "vec_push failed");
	ATF_REQUIRE_STREQ_MSG(kvl.d[0]->key, "key", "Invalid first key");
	ATF_REQUIRE_STREQ_MSG(kvl.d[1]->key, "akey", "Invalid first key");

	pkg_kv_sort(&kvl);
	ATF_REQUIRE_STREQ_MSG(kvl.d[0]->key, "akey", "Invalid first key");
	ATF_REQUIRE_STREQ_MSG(kvl.d[1]->key, "key", "Invalid first key");

	vec_free_and_free(&kvl, pkg_kv_free);
}

ATF_TC_BODY(kv_search, tc)
{
	kvlist_t kvl = vec_init();

	ATF_REQUIRE_EQ_MSG(kvl.d, NULL, "vec_init failed");
	ATF_REQUIRE_EQ_MSG(kvl.cap, 0, "vec_init failed");
	ATF_REQUIRE_EQ_MSG(kvl.len, 0, "vec_init failed");

	struct pkg_kv *kv = pkg_kv_new("key", "value");
	vec_push(&kvl, kv);
	ATF_REQUIRE_MSG(kvl.d != NULL, "vec_push failed");
	ATF_REQUIRE_EQ_MSG(kvl.cap, 1, "vec_push failed");
	ATF_REQUIRE_EQ_MSG(kvl.len, 1, "vec_push failed");

	kv = pkg_kv_search(&kvl, "bla");
	ATF_REQUIRE(kv == NULL);

	kv = pkg_kv_search(&kvl, "key");
	ATF_REQUIRE(kv != NULL);
	ATF_REQUIRE_STREQ_MSG(kv->key, "key", "Invalid search result");
	ATF_REQUIRE_STREQ_MSG(kv->value, "value", "Invalid search result");

	kv = pkg_kv_new("akey", "value");
	vec_push(&kvl, kv);
	ATF_REQUIRE_EQ_MSG(kvl.cap, 2, "vec_push failed");
	ATF_REQUIRE_EQ_MSG(kvl.len, 2, "vec_push failed");
	ATF_REQUIRE_STREQ_MSG(kvl.d[0]->key, "key", "Invalid first key");
	ATF_REQUIRE_STREQ_MSG(kvl.d[1]->key, "akey", "Invalid first key");

	pkg_kv_sort(&kvl);
	ATF_REQUIRE_STREQ_MSG(kvl.d[0]->key, "akey", "Invalid first key");
	ATF_REQUIRE_STREQ_MSG(kvl.d[1]->key, "key", "Invalid first key");

	kv = pkg_kv_search(&kvl, "key");
	ATF_REQUIRE(kv != NULL);
	ATF_REQUIRE_STREQ_MSG(kv->key, "key", "Invalid search result");
	ATF_REQUIRE_STREQ_MSG(kv->value, "value", "Invalid search result");

	kv = pkg_kv_search(&kvl, "akey");
	ATF_REQUIRE(kv != NULL);
	ATF_REQUIRE_STREQ_MSG(kv->key, "akey", "Invalid search result");
	ATF_REQUIRE_STREQ_MSG(kv->value, "value", "Invalid search result");

	kv = pkg_kv_search(&kvl, "bla");
	ATF_REQUIRE(kv == NULL);

	vec_free_and_free(&kvl, pkg_kv_free);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, kv_insert_sorted);
	ATF_TP_ADD_TC(tp, kv_sort);
	ATF_TP_ADD_TC(tp, kv_search);

	return (atf_no_error());
}
