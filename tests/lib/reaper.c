/*-
 * Copyright(c) 2025 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <atf-c.h>
#include <private/utils.h>

ATF_TC_WITHOUT_HEAD(reaper_acquire_release);

ATF_TC_BODY(reaper_acquire_release, tc)
{
	struct pkg_reaper r;

	pkg_reaper_acquire(&r);
	ATF_REQUIRE_MSG(r.mypid > 0, "pkg_reaper_acquire did not set mypid");
	pkg_reaper_release(&r);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, reaper_acquire_release);

	return (atf_no_error());
}
