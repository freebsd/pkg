/*
 * Copyright (c) 2021 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
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
#include <archive.h>
#include <pkg.h>
#include <private/pkg.h>
#include <private/packing.h>
#include <pkg_config.h>

ATF_TC_WITHOUT_HEAD(packing_format_from_string);
ATF_TC_WITHOUT_HEAD(packing_format_to_string);
ATF_TC_WITHOUT_HEAD(packing_format_is_valid);
ATF_TC_WITHOUT_HEAD(packing_set_format);

ATF_TC_BODY(packing_format_from_string, tc)
{
	ATF_REQUIRE_EQ(packing_format_from_string(NULL), DEFAULT_COMPRESSION);
	ATF_REQUIRE_EQ(packing_format_from_string("tzst"), TZS);
	ATF_REQUIRE_EQ(packing_format_from_string("txz"), TXZ);
	ATF_REQUIRE_EQ(packing_format_from_string("tbz"), TBZ);
	ATF_REQUIRE_EQ(packing_format_from_string("tgz"), TGZ);
	ATF_REQUIRE_EQ(packing_format_from_string("tar"), TAR);
	ATF_REQUIRE_EQ(packing_format_from_string("plop"), TXZ);
}

ATF_TC_BODY(packing_format_to_string, tc)
{
	ATF_REQUIRE_EQ(packing_format_to_string(27), NULL);
	ATF_REQUIRE_STREQ(packing_format_to_string(TZS), "tzst");
	ATF_REQUIRE_STREQ(packing_format_to_string(TXZ), "txz");
	ATF_REQUIRE_STREQ(packing_format_to_string(TBZ), "tbz");
	ATF_REQUIRE_STREQ(packing_format_to_string(TGZ), "tgz");
	ATF_REQUIRE_STREQ(packing_format_to_string(TAR), "tar");
}

ATF_TC_BODY(packing_format_is_valid, tc)
{
	ATF_REQUIRE_EQ(packing_is_valid_format("pkg"), true);
	ATF_REQUIRE_EQ(packing_is_valid_format("tzst"), true);
	ATF_REQUIRE_EQ(packing_is_valid_format("txz"), true);
	ATF_REQUIRE_EQ(packing_is_valid_format("tbz"), true);
	ATF_REQUIRE_EQ(packing_is_valid_format("tgz"), true);
	ATF_REQUIRE_EQ(packing_is_valid_format("tar"), true);
	ATF_REQUIRE_EQ(packing_is_valid_format("deb"), false);
	ATF_REQUIRE_EQ(packing_is_valid_format(NULL), false);
}

ATF_TC_BODY(packing_set_format, tc)
{
	struct archive *a = archive_write_new();
	ATF_CHECK(a != NULL);

#if defined(HAVE_ARCHIVE_WRITE_ADD_FILTER_ZSTD) && __FreeBSD_version >= 1300000
	ATF_REQUIRE_STREQ(packing_set_format(a, TZS, -1), "tzst");
#endif
	ATF_REQUIRE_STREQ(packing_set_format(a, TXZ, -1), "txz");
	ATF_REQUIRE_STREQ(packing_set_format(a, TBZ, -1), "tbz");
	ATF_REQUIRE_STREQ(packing_set_format(a, TGZ, -1), "tgz");
	ATF_REQUIRE_STREQ(packing_set_format(a, TAR, -1), "tar");
	ATF_REQUIRE_EQ(packing_set_format(a, 28, -1), NULL);

	/* compression min */
#if defined(HAVE_ARCHIVE_WRITE_ADD_FILTER_ZSTD) && __FreeBSD_version >= 1300000
	ATF_REQUIRE_STREQ(packing_set_format(a, TZS, INT_MIN), "tzst");
#endif
	ATF_REQUIRE_STREQ(packing_set_format(a, TXZ, INT_MIN), "txz");
	ATF_REQUIRE_STREQ(packing_set_format(a, TBZ, INT_MIN), "tbz");
	ATF_REQUIRE_STREQ(packing_set_format(a, TGZ, INT_MIN), "tgz");
	ATF_REQUIRE_STREQ(packing_set_format(a, TAR, INT_MIN), "tar");
	ATF_REQUIRE_EQ(packing_set_format(a, 28, INT_MIN), NULL);

	/* compression max */
#if defined(HAVE_ARCHIVE_WRITE_ADD_FILTER_ZSTD) && __FreeBSD_version >= 1300000
	ATF_REQUIRE_STREQ(packing_set_format(a, TZS, INT_MAX), "tzst");
#endif
	ATF_REQUIRE_STREQ(packing_set_format(a, TXZ, INT_MAX), "txz");
	ATF_REQUIRE_STREQ(packing_set_format(a, TBZ, INT_MAX), "tbz");
	ATF_REQUIRE_STREQ(packing_set_format(a, TGZ, INT_MAX), "tgz");
	ATF_REQUIRE_STREQ(packing_set_format(a, TAR, INT_MAX), "tar");
	ATF_REQUIRE_EQ(packing_set_format(a, 28, INT_MAX), NULL);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, packing_format_from_string);
	ATF_TP_ADD_TC(tp, packing_format_to_string);
	ATF_TP_ADD_TC(tp, packing_format_is_valid);
	ATF_TP_ADD_TC(tp, packing_set_format);

	return (atf_no_error());
}
