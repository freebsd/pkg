/*-
 * Copyright (c) 2022 Baptiste Daroussin <bapt@FreeBSD.org>
 *~
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *~
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
#include <err.h>
#include <private/pkg.h>
#include <pkg_config.h>

ATF_TC_WITHOUT_HEAD(basics);

ATF_TC_BODY(basics, tc) {
#ifdef HAVE_FFLAGSTOSTR
	const char *file = "./file type=file uname=root gname=wheel mode=644 flags=uchg\n"
		"./dir type=dir uname=root gname=wheel mode=644 flags=uchg\n"
		"./link type=link uname=root gname=wheel mode=644 link=bla\n";
#else
	const char *file = "./file type=file uname=root gname=wheel mode=644 flags=\n"
		"./dir type=dir uname=root gname=wheel mode=644 flags=\n"
		"./link type=link uname=root gname=wheel mode=644 link=bla\n";
#endif
	ATF_REQUIRE_EQ(EPKG_FATAL, metalog_open("/dev/nope/nope"));
	ATF_REQUIRE_EQ(EPKG_FATAL, metalog_add(PKG_METALOG_FILE, "meh", "root", "wheel", 0644, 2, NULL));
	ATF_REQUIRE_EQ(EPKG_OK, metalog_open("out"));
	ATF_REQUIRE_EQ(EPKG_OK, metalog_add(PKG_METALOG_FILE, "file", "root", "wheel", 0644, 2, NULL));
	ATF_REQUIRE_EQ(EPKG_OK, metalog_add(PKG_METALOG_DIR, "dir", "root", "wheel", 0644, 2, NULL));
	ATF_REQUIRE_EQ(EPKG_OK, metalog_add(PKG_METALOG_LINK, "link", "root", "wheel", 0644, 0, "bla"));
	metalog_close();
	if (!atf_utils_compare_file("out", file)) {
		atf_utils_cat_file("out", ">");
		atf_tc_fail("Invalid file");
	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, basics);

	return (atf_no_error());
}
