/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 *​ Copyright (c) 2025 The FreeBSD Foundation
 *​
 *​ Portions of this software were developed by
 * Tuukka Pasanen <tuukka.pasanen@ilmi.fi> under sponsorship from
 * the FreeBSD Foundation
 */

#include <atf-c.h>
#include <stdint.h>
#include <stdlib.h>

#include <private/pkg_cpe.h>

ATF_TC_WITHOUT_HEAD(cpeparse);

ATF_TC_BODY(cpeparse, tc)
{
	struct pkg_audit_cpe *cpe_struct = pkg_cpe_parse("");

	ATF_REQUIRE(cpe_struct == NULL);

	cpe_struct = pkg_cpe_parse("cpe:");
	ATF_REQUIRE(cpe_struct == NULL);

	cpe_struct = pkg_cpe_parse("cpe:2.3:");
	ATF_REQUIRE(cpe_struct == NULL);

	cpe_struct = pkg_cpe_parse("cpe:2.2:a:test");
	ATF_REQUIRE(cpe_struct == NULL);

	cpe_struct = pkg_cpe_parse("cpu:2.3:a:test");
	ATF_REQUIRE(cpe_struct == NULL);


	cpe_struct = pkg_cpe_parse("cpe:2.3:a:test");
	ATF_REQUIRE(cpe_struct != NULL);

	ATF_CHECK_EQ(cpe_struct->version_major, 2);
	ATF_CHECK_EQ(cpe_struct->version_minor, 3);
	ATF_CHECK_EQ(cpe_struct->part, CPE_APPLICATIONS);
	ATF_CHECK_STREQ(cpe_struct->vendor, "test");
	ATF_REQUIRE(cpe_struct->product == NULL);
	ATF_REQUIRE(cpe_struct->version == NULL);
	ATF_REQUIRE(cpe_struct->update == NULL);
	ATF_REQUIRE(cpe_struct->edition == NULL);
	ATF_REQUIRE(cpe_struct->language == NULL);
	ATF_REQUIRE(cpe_struct->sw_edition == NULL);
	ATF_REQUIRE(cpe_struct->target_sw == NULL);
	ATF_REQUIRE(cpe_struct->target_hw == NULL);
	ATF_REQUIRE(cpe_struct->other == NULL);

	char *test_str = pkg_cpe_create(cpe_struct);
	ATF_CHECK_STREQ(test_str, "cpe:2.3:a:test:::::::::");
	free(test_str);
	pkg_cpe_free(cpe_struct);

	cpe_struct = pkg_cpe_parse("cpe:2.3:a:test:test_product:1.0:sp1:1:en-us:14.3:FreeBSD:x86_64:other_things");
	ATF_CHECK_EQ(cpe_struct->version_major, 2);
	ATF_CHECK_EQ(cpe_struct->version_minor, 3);
	ATF_CHECK_EQ(cpe_struct->part, CPE_APPLICATIONS);
	ATF_CHECK_STREQ(cpe_struct->vendor, "test");
	ATF_CHECK_STREQ(cpe_struct->product, "test_product");
	ATF_CHECK_STREQ(cpe_struct->version, "1.0");
	ATF_CHECK_STREQ(cpe_struct->update, "sp1");
	ATF_CHECK_STREQ(cpe_struct->edition, "1");
	ATF_CHECK_STREQ(cpe_struct->language, "en-us");
	ATF_CHECK_STREQ(cpe_struct->sw_edition, "14.3");
	ATF_CHECK_STREQ(cpe_struct->target_sw, "FreeBSD");
	ATF_CHECK_STREQ(cpe_struct->target_hw, "x86_64");
	ATF_CHECK_STREQ(cpe_struct->other, "other_things");

	test_str = pkg_cpe_create(cpe_struct);
	ATF_CHECK_STREQ(test_str, "cpe:2.3:a:test:test_product:1.0:sp1:1:en-us:14.3:FreeBSD:x86_64:other_things");
	free(test_str);


	pkg_cpe_free(cpe_struct);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, cpeparse);

	return (atf_no_error());
}
