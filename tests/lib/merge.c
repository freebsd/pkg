/*-
 * Copyright (c) 2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * All rights reserved.
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
#include <sys/types.h>
#include <private/utils.h>
#include <utstring.h>

ATF_TC(merge);

ATF_TC_HEAD(merge, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "3 way merge");
}

ATF_TC_BODY(merge, tc)
{
	UT_string *b;
	utstring_new(b);
	char *pivot = "test1\ntest2\n";
	char *modified = "test1\n#test2\n";
	char *new = "test1\ntest2\ntest3\n";

	ATF_REQUIRE_EQ(merge_3way(pivot, modified, new, b), 0);
	ATF_REQUIRE_STREQ(utstring_body(b), "test1\n#test2\ntest3\n");

	utstring_clear(b);
	pivot = "test1\ntest2";
	modified = "test1\n#test2";
	new = "test1\ntest2\ntest3";

	ATF_REQUIRE_EQ(merge_3way(pivot, modified, new, b), 0);
	ATF_REQUIRE_STREQ(utstring_body(b), "test1\n#test2test3");

	utstring_clear(b);
	pivot = "test1\ntest2";
	modified = "test1\n";
	new = "test1\ntest2\ntest3";

	ATF_REQUIRE_EQ(merge_3way(pivot, modified, new, b), 0);
	ATF_REQUIRE_STREQ(utstring_body(b), "test1\ntest3");

	utstring_clear(b);
	pivot = "test1\ntest2\ntest3";
	modified = "test1\na\ntest2\ntest3";
	new = "test1\ntest2\ntest3";

	ATF_REQUIRE_EQ(merge_3way(pivot, modified, new, b), 0);
	ATF_REQUIRE_STREQ(utstring_body(b), "test1\na\ntest2\ntest3");

	utstring_free(b);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, merge);

	return (atf_no_error());
}
