/*
 * Copyright (c) 2012 Matthew Seaman <matthew@FreeBSD.org>
 * All rights reserved.
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

#include <string.h>

#include <atf-c.h>
#include <pkg.h>
#include <private/pkg_printf.h>

ATF_TC(gen_format);
ATF_TC_HEAD(gen_format, tc)
{
	atf_tc_set_md_var(tc, "descr",
			  "Generate printf format code for final output");
}
ATF_TC_BODY(gen_format, tc)
{
	char		 buf[32];
	unsigned	 i;
	char		*tail = "x";

	struct gf_test_vals {
		const char *out;
		unsigned    flags;
	} gf_test_vals[] = {

		{ "%*x",     0, },

		{ "%*x",     PP_ALTERNATE_FORM1, }, /* Has no effect */

		{ "%#*x",    PP_ALTERNATE_FORM2, },
		{ "%-*x",    PP_LEFT_ALIGN, },
		{ "%#-*x",   PP_LEFT_ALIGN|PP_ALTERNATE_FORM2, },
		{ "%+*x",    PP_EXPLICIT_PLUS, },
		{ "%#+*x",   PP_EXPLICIT_PLUS|PP_ALTERNATE_FORM2, },
		{ "%-+*x",   PP_EXPLICIT_PLUS|PP_LEFT_ALIGN, },
		{ "%#-+*x",  PP_EXPLICIT_PLUS|PP_LEFT_ALIGN|PP_ALTERNATE_FORM2, },
		{ "% *x",    PP_SPACE_FOR_PLUS, },
		{ "%# *x",   PP_SPACE_FOR_PLUS|PP_ALTERNATE_FORM2, },
		{ "%- *x",   PP_SPACE_FOR_PLUS|PP_LEFT_ALIGN, },
		{ "%#- *x",  PP_SPACE_FOR_PLUS|PP_LEFT_ALIGN|PP_ALTERNATE_FORM2, },
		{ "%+*x",    PP_SPACE_FOR_PLUS|PP_EXPLICIT_PLUS, },
		{ "%#+*x",   PP_SPACE_FOR_PLUS|PP_EXPLICIT_PLUS|PP_ALTERNATE_FORM2, },
		{ "%-+*x",   PP_SPACE_FOR_PLUS|PP_EXPLICIT_PLUS|PP_LEFT_ALIGN, },
		{ "%#-+*x",  PP_SPACE_FOR_PLUS|PP_EXPLICIT_PLUS|PP_LEFT_ALIGN|PP_ALTERNATE_FORM2, },
		{ "%0*x",    PP_ZERO_PAD, },
		{ "%#0*x",   PP_ZERO_PAD|PP_ALTERNATE_FORM2, },
		{ "%-*x",    PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ "%#-*x",   PP_ZERO_PAD|PP_LEFT_ALIGN|PP_ALTERNATE_FORM2, },
		{ "%0+*x",   PP_ZERO_PAD|PP_EXPLICIT_PLUS, },
		{ "%#0+*x",  PP_ZERO_PAD|PP_EXPLICIT_PLUS|PP_ALTERNATE_FORM2, },
		{ "%-+*x",   PP_ZERO_PAD|PP_EXPLICIT_PLUS|PP_LEFT_ALIGN, },
		{ "%#-+*x",  PP_ZERO_PAD|PP_EXPLICIT_PLUS|PP_LEFT_ALIGN|PP_ALTERNATE_FORM2, },
		{ "%0 *x",   PP_ZERO_PAD|PP_SPACE_FOR_PLUS, },
		{ "%#0 *x",  PP_ZERO_PAD|PP_SPACE_FOR_PLUS|PP_ALTERNATE_FORM2, },
		{ "%- *x",   PP_ZERO_PAD|PP_SPACE_FOR_PLUS|PP_LEFT_ALIGN, },
		{ "%#- *x",  PP_ZERO_PAD|PP_SPACE_FOR_PLUS|PP_LEFT_ALIGN|PP_ALTERNATE_FORM2, },
		{ "%0+*x",   PP_ZERO_PAD|PP_SPACE_FOR_PLUS|PP_EXPLICIT_PLUS, },
		{ "%#0+*x",  PP_ZERO_PAD|PP_SPACE_FOR_PLUS|PP_EXPLICIT_PLUS|PP_ALTERNATE_FORM2, },
		{ "%-+*x",   PP_ZERO_PAD|PP_SPACE_FOR_PLUS|PP_EXPLICIT_PLUS|PP_LEFT_ALIGN, },
		{ "%#-+*x",  PP_ZERO_PAD|PP_SPACE_FOR_PLUS|PP_EXPLICIT_PLUS|PP_LEFT_ALIGN|PP_ALTERNATE_FORM2, },
		{ "%'*x",    PP_THOUSANDS_SEP, },
		{ "%#'*x",   PP_THOUSANDS_SEP|PP_ALTERNATE_FORM2, },
		{ "%-'*x",   PP_THOUSANDS_SEP|PP_LEFT_ALIGN, },
		{ "%#-'*x",  PP_THOUSANDS_SEP|PP_LEFT_ALIGN|PP_ALTERNATE_FORM2, },
		{ "%+'*x",   PP_THOUSANDS_SEP|PP_EXPLICIT_PLUS, },
		{ "%#+'*x",  PP_THOUSANDS_SEP|PP_EXPLICIT_PLUS|PP_ALTERNATE_FORM2, },
		{ "%-+'*x",  PP_THOUSANDS_SEP|PP_EXPLICIT_PLUS|PP_LEFT_ALIGN, },
		{ "%#-+'*x", PP_THOUSANDS_SEP|PP_EXPLICIT_PLUS|PP_LEFT_ALIGN|PP_ALTERNATE_FORM2, },
		{ "% '*x",   PP_THOUSANDS_SEP|PP_SPACE_FOR_PLUS, },
		{ "%# '*x",  PP_THOUSANDS_SEP|PP_SPACE_FOR_PLUS|PP_ALTERNATE_FORM2, },
		{ "%- '*x",  PP_THOUSANDS_SEP|PP_SPACE_FOR_PLUS|PP_LEFT_ALIGN, },
		{ "%#- '*x", PP_THOUSANDS_SEP|PP_SPACE_FOR_PLUS|PP_LEFT_ALIGN|PP_ALTERNATE_FORM2, },
		{ "%+'*x",   PP_THOUSANDS_SEP|PP_SPACE_FOR_PLUS|PP_EXPLICIT_PLUS, },
		{ "%#+'*x",  PP_THOUSANDS_SEP|PP_SPACE_FOR_PLUS|PP_EXPLICIT_PLUS|PP_ALTERNATE_FORM2, },
		{ "%-+'*x",  PP_THOUSANDS_SEP|PP_SPACE_FOR_PLUS|PP_EXPLICIT_PLUS|PP_LEFT_ALIGN, },
		{ "%#-+'*x", PP_THOUSANDS_SEP|PP_SPACE_FOR_PLUS|PP_EXPLICIT_PLUS|PP_LEFT_ALIGN|PP_ALTERNATE_FORM2, },
		{ "%0'*x",   PP_THOUSANDS_SEP|PP_ZERO_PAD, },
		{ "%#0'*x",  PP_THOUSANDS_SEP|PP_ZERO_PAD|PP_ALTERNATE_FORM2, },
		{ "%-'*x",   PP_THOUSANDS_SEP|PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ "%#-'*x",  PP_THOUSANDS_SEP|PP_ZERO_PAD|PP_LEFT_ALIGN|PP_ALTERNATE_FORM2, },
		{ "%0+'*x",  PP_THOUSANDS_SEP|PP_ZERO_PAD|PP_EXPLICIT_PLUS, },
		{ "%#0+'*x", PP_THOUSANDS_SEP|PP_ZERO_PAD|PP_EXPLICIT_PLUS|PP_ALTERNATE_FORM2, },
		{ "%-+'*x",  PP_THOUSANDS_SEP|PP_ZERO_PAD|PP_EXPLICIT_PLUS|PP_LEFT_ALIGN, },
		{ "%#-+'*x", PP_THOUSANDS_SEP|PP_ZERO_PAD|PP_EXPLICIT_PLUS|PP_LEFT_ALIGN|PP_ALTERNATE_FORM2, },
		{ "%0 '*x",  PP_THOUSANDS_SEP|PP_ZERO_PAD|PP_SPACE_FOR_PLUS, },
		{ "%#0 '*x", PP_THOUSANDS_SEP|PP_ZERO_PAD|PP_SPACE_FOR_PLUS|PP_ALTERNATE_FORM2, },
		{ "%- '*x",  PP_THOUSANDS_SEP|PP_ZERO_PAD|PP_SPACE_FOR_PLUS|PP_LEFT_ALIGN, },
		{ "%#- '*x", PP_THOUSANDS_SEP|PP_ZERO_PAD|PP_SPACE_FOR_PLUS|PP_LEFT_ALIGN|PP_ALTERNATE_FORM2, },
		{ "%0+'*x",  PP_THOUSANDS_SEP|PP_ZERO_PAD|PP_SPACE_FOR_PLUS|PP_EXPLICIT_PLUS, },
		{ "%#0+'*x", PP_THOUSANDS_SEP|PP_ZERO_PAD|PP_SPACE_FOR_PLUS|PP_EXPLICIT_PLUS|PP_ALTERNATE_FORM2, },
		{ "%-+'*x",  PP_THOUSANDS_SEP|PP_ZERO_PAD|PP_SPACE_FOR_PLUS|PP_EXPLICIT_PLUS|PP_LEFT_ALIGN, },
		{ "%#-+'*x", PP_THOUSANDS_SEP|PP_ZERO_PAD|PP_SPACE_FOR_PLUS|PP_EXPLICIT_PLUS|PP_LEFT_ALIGN|PP_ALTERNATE_FORM2, },

		{ NULL, 0, },
	};

	for (i = 0; gf_test_vals[i].out != NULL; i++) {
		ATF_CHECK_STREQ(gen_format(buf, sizeof(buf),
					   gf_test_vals[i].flags, tail),
				gf_test_vals[i].out);
	}
}

ATF_TC(human_number);
ATF_TC_HEAD(human_number, tc)
{
	atf_tc_set_md_var(tc, "descr",
			  "Testing human_number() output routine");
}
ATF_TC_BODY(human_number, tc)
{
	struct sbuf		*sbuf;
	struct percent_esc	*p;
	int			 i;

	struct hn_test_vals {
		int64_t	in;
		const char *out;
		int width;
		unsigned flags;
	} hn_test_vals[] = {
		{ 0,                   "0.00",   0, 0, },
		{ 1,                   "1.00",   0, 0, },
		{ 10,                  "10.0",   0, 0, },
		{ 100,                 "100",    0, 0. },
		{ 1000,                "1.00k",  0, 0, },
		{ 10000,               "10.0k",  0, 0, },
		{ 100000,              "100k",   0, 0, },
		{ 1000000,             "1.00M",  0, 0, },
		{ 1000000000,          "1.00G",  0, 0, },
		{ 1000000000000,       "1.00T",  0, 0, },
		{ 1000000000000000,    "1.00P",  0, 0, },
		{ 1000000000000000000, "1.00E",  0, 0, },

		{ 999,                 "999",    0, 0, },
		{ 1001,                "1.00k",  0, 0, },
		{ 1010,                "1.01k",  0, 0, },
		{ 1490,                "1.49k",  0, 0, },
		{ 1499,                "1.50k",  0, 0, },
		{ 1500,                "1.50k",  0, 0, },

		{ -1,                  "-1.00",  0, 0, },
		{ -1234,               "-1.23k", 0, 0, },
		{ -1234567,            "-1.23M", 0, 0, },
		{ -1234567890,         "-1.23G", 0, 0, },
		{ -1234567890123,      "-1.23T", 0, 0, },
		{ -1234567890123456,   "-1.23P", 0, 0, },
		{ -1234567890123456789,"-1.23E", 0, 0, },

		{ 512,                 "512",    0, PP_ALTERNATE_FORM2, },
		{ 1024,                "1.00Ki", 0, PP_ALTERNATE_FORM2, },
		{ 1536,                "1.50Ki", 0, PP_ALTERNATE_FORM2, },
		{ 24576,               "24.0Ki", 0, PP_ALTERNATE_FORM2, },
		{ 393216,              "384Ki",  0, PP_ALTERNATE_FORM2, },
		{ 2359296,             "2.25Mi", 0, PP_ALTERNATE_FORM2, },
		{ 3623878656,          "3.38Gi", 0, PP_ALTERNATE_FORM2, },
		{ 5566277615616,       "5.06Ti", 0, PP_ALTERNATE_FORM2, },
		{ 8549802417586176,    "7.59Pi", 0, PP_ALTERNATE_FORM2, },
		{ 1313249651341236633, "1.14Ei", 0, PP_ALTERNATE_FORM2, },


		{ 123,     "123",          0, 0, },
		{ 123,     "123",          1, 0, },
		{ 123,     "123",          2, 0, },
		{ 123,     "123",          3, 0, },
		{ 123,     " 123",         4, 0, },
		{ 123,     "  123",        5, 0, },
		{ 123,     "   123",       6, 0, },
		{ 123,     "    123",      7, 0, },
		{ 123,     "     123",     8, 0, },
		{ 123,     "      123",    9, 0, },

		{ 123,     " 123",         0, PP_SPACE_FOR_PLUS, },
		{ 123,     " 123",         1, PP_SPACE_FOR_PLUS, },
		{ 123,     " 123",         2, PP_SPACE_FOR_PLUS, },
		{ 123,     " 123",         3, PP_SPACE_FOR_PLUS, },
		{ 123,     " 123",         4, PP_SPACE_FOR_PLUS, },
		{ 123,     "  123",        5, PP_SPACE_FOR_PLUS, },
		{ 123,     "   123",       6, PP_SPACE_FOR_PLUS, },
		{ 123,     "    123",      7, PP_SPACE_FOR_PLUS, },
		{ 123,     "     123",     8, PP_SPACE_FOR_PLUS, },
		{ 123,     "      123",    9, PP_SPACE_FOR_PLUS, },

		{ -123,    "-123",         0, 0, },
		{ -123,    "-123",         1, 0, },
		{ -123,    "-123",         2, 0, },
		{ -123,    "-123",         3, 0, },
		{ -123,    "-123",         4, 0, },
		{ -123,    " -123",        5, 0, },
		{ -123,    "  -123",       6, 0, },
		{ -123,    "   -123",      7, 0, },
		{ -123,    "    -123",     8, 0, },
		{ -123,    "     -123",    9, 0, },

		{ 123,     "123",          0, PP_ALTERNATE_FORM2, },
		{ 123,     "123",          1, PP_ALTERNATE_FORM2, },
		{ 123,     "123",          2, PP_ALTERNATE_FORM2, },
		{ 123,     "123",          3, PP_ALTERNATE_FORM2, },
		{ 123,     " 123",         4, PP_ALTERNATE_FORM2, },
		{ 123,     "  123",        5, PP_ALTERNATE_FORM2, },
		{ 123,     "   123",       6, PP_ALTERNATE_FORM2, },
		{ 123,     "    123",      7, PP_ALTERNATE_FORM2, },
		{ 123,     "     123",     8, PP_ALTERNATE_FORM2, },
		{ 123,     "      123",    9, PP_ALTERNATE_FORM2, },

		{ 1234567, "1.23M",        0, 0, },
		{ 1234567, "1M",           1, 0, },
		{ 1234567, "1M",           2, 0, },
		{ 1234567, " 1M",          3, 0, },
		{ 1234567, "1.2M",         4, 0, },
		{ 1234567, "1.23M",        5, 0, },
		{ 1234567, " 1.23M",       6, 0, },
		{ 1234567, "  1.23M",      7, 0, },
		{ 1234567, "   1.23M",     8, 0, },
		{ 1234567, "    1.23M",    9, 0, },

		{ 12345678, "12.3M",       0, 0, },
		{ 12345678, "12M",         1, 0, },
		{ 12345678, "12M",         2, 0, },
		{ 12345678, "12M",         3, 0, },
		{ 12345678, " 12M",        4, 0, },
		{ 12345678, "12.3M",       5, 0, },
		{ 12345678, " 12.3M",      6, 0, },
		{ 12345678, "  12.3M",     7, 0, },
		{ 12345678, "   12.3M",    8, 0, },
		{ 12345678, "    12.3M",   9, 0, },

		{ 123456789, "123M",       0, 0, },
		{ 123456789, "123M",       1, 0, },
		{ 123456789, "123M",       2, 0, },
		{ 123456789, "123M",       3, 0, },
		{ 123456789, "123M",       4, 0, },
		{ 123456789, " 123M",      5, 0, },
		{ 123456789, "  123M",     6, 0, },
		{ 123456789, "   123M",    7, 0, },
		{ 123456789, "    123M",   8, 0, },
		{ 123456789, "     123M",  9, 0, },

		{ 1234567, "1.18Mi",       0, PP_ALTERNATE_FORM2, },
		{ 1234567, "1Mi",          1, PP_ALTERNATE_FORM2, },
		{ 1234567, "1Mi",          2, PP_ALTERNATE_FORM2, },
		{ 1234567, "1Mi",          3, PP_ALTERNATE_FORM2, },
		{ 1234567, " 1Mi",         4, PP_ALTERNATE_FORM2, },
		{ 1234567, "1.2Mi",        5, PP_ALTERNATE_FORM2, },
		{ 1234567, "1.18Mi",       6, PP_ALTERNATE_FORM2, },
		{ 1234567, " 1.18Mi",      7, PP_ALTERNATE_FORM2, },
		{ 1234567, "  1.18Mi",     8, PP_ALTERNATE_FORM2, },
		{ 1234567, "   1.18Mi",    9, PP_ALTERNATE_FORM2, },

		{ 12345678, "11.8Mi",      0, PP_ALTERNATE_FORM2, },
		{ 12345678, "12Mi",        1, PP_ALTERNATE_FORM2, },
		{ 12345678, "12Mi",        2, PP_ALTERNATE_FORM2, },
		{ 12345678, "12Mi",        3, PP_ALTERNATE_FORM2, },
		{ 12345678, "12Mi",        4, PP_ALTERNATE_FORM2, },
		{ 12345678, " 12Mi",       5, PP_ALTERNATE_FORM2, },
		{ 12345678, "11.8Mi",      6, PP_ALTERNATE_FORM2, },
		{ 12345678, " 11.8Mi",     7, PP_ALTERNATE_FORM2, },
		{ 12345678, "  11.8Mi",    8, PP_ALTERNATE_FORM2, },
		{ 12345678, "   11.8Mi",   9, PP_ALTERNATE_FORM2, },

		{ 123456789, "118Mi",      0, PP_ALTERNATE_FORM2, },
		{ 123456789, "118Mi",      1, PP_ALTERNATE_FORM2, },
		{ 123456789, "118Mi",      2, PP_ALTERNATE_FORM2, },
		{ 123456789, "118Mi",      3, PP_ALTERNATE_FORM2, },
		{ 123456789, "118Mi",      4, PP_ALTERNATE_FORM2, },
		{ 123456789, "118Mi",      5, PP_ALTERNATE_FORM2, },
		{ 123456789, " 118Mi",     6, PP_ALTERNATE_FORM2, },
		{ 123456789, "  118Mi",    7, PP_ALTERNATE_FORM2, },
		{ 123456789, "   118Mi",   8, PP_ALTERNATE_FORM2, },
		{ 123456789, "    118Mi",  9, PP_ALTERNATE_FORM2, },

		{  1234567, "1.23M",  0, PP_ALTERNATE_FORM1, },
		{  1234567, "1.18Mi", 0, PP_ALTERNATE_FORM2, },
		{  1234567, "1.23 M", 6, PP_LEFT_ALIGN, },
		{  1234567, "+1.23M", 0, PP_EXPLICIT_PLUS, },
		{ -1234567, "-1.23M", 0, PP_EXPLICIT_PLUS, },
		{  1234567, " 1.23M", 0, PP_SPACE_FOR_PLUS, },
		{ -1234567, "-1.23M", 0, PP_SPACE_FOR_PLUS, },
		{  1234567, "01.23M", 6, PP_ZERO_PAD, },
		{  1234567, "1.23M",  0, PP_THOUSANDS_SEP, },
		{  1023,"1023", 0, PP_ALTERNATE_FORM2|PP_THOUSANDS_SEP, },

		{ -1,                  NULL,     0, 0, },
	};

	sbuf = sbuf_new_auto();
	p = new_percent_esc(NULL);

	ATF_REQUIRE_EQ(sbuf != NULL, true);
	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; hn_test_vals[i].out != NULL; i++) {
		p->width = hn_test_vals[i].width;
		p->flags = hn_test_vals[i].flags;
		sbuf = human_number(sbuf, hn_test_vals[i].in, p);
		ATF_CHECK_STREQ(sbuf_data(sbuf), hn_test_vals[i].out);
		sbuf_clear(sbuf);
	}

	free_percent_esc(p);
	sbuf_delete(sbuf);
}

ATF_TC(string_val);
ATF_TC_HEAD(string_val, tc)
{
	atf_tc_set_md_var(tc, "descr",
			  "Testing string_val() output routine");
}
ATF_TC_BODY(string_val, tc)
{
	struct sbuf		*sbuf;
	struct percent_esc	*p;
	int			 i;

	struct sv_test_vals {
		const char *in;
		const char *out;
		int width;
		unsigned flags;
	} sv_test_vals[] = {
		{ "xxx", "xxx",    0, 0, },
		{ "xxx", "xxx",    1, 0, },
		{ "xxx", "xxx",    2, 0, },
		{ "xxx", "xxx",    3, 0, },
		{ "xxx", " xxx",   4, 0, },
		{ "xxx", "  xxx",  5, 0, },
		{ "xxx", "   xxx", 6, 0, },

		{ "xxy", "xxy",    0, PP_LEFT_ALIGN, },
		{ "xxy", "xxy",    1, PP_LEFT_ALIGN, },
		{ "xxy", "xxy",    2, PP_LEFT_ALIGN, },
		{ "xxy", "xxy",    3, PP_LEFT_ALIGN, },
		{ "xxy", "xxy ",   4, PP_LEFT_ALIGN, },
		{ "xxy", "xxy  ",  5, PP_LEFT_ALIGN, },
		{ "xxy", "xxy   ", 6, PP_LEFT_ALIGN, },

		{ "xxz", "xxz",    0, PP_ZERO_PAD, },
		{ "xxz", "xxz",    1, PP_ZERO_PAD, },
		{ "xxz", "xxz",    2, PP_ZERO_PAD, },
		{ "xxz", "xxz",    3, PP_ZERO_PAD, },
		{ "xxz", "0xxz",   4, PP_ZERO_PAD, },
		{ "xxz", "00xxz",  5, PP_ZERO_PAD, },
		{ "xxz", "000xxz", 6, PP_ZERO_PAD, },

		/* Seems you can't zero pad on the RHS of a string */

		{ "xyx", "xyx",    0, PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ "xyx", "xyx",    1, PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ "xyx", "xyx",    2, PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ "xyx", "xyx",    3, PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ "xyx", "xyx ",   4, PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ "xyx", "xyx  ",  5, PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ "xyx", "xyx   ", 6, PP_ZERO_PAD|PP_LEFT_ALIGN, },

		/* Most of the format modifiers don't affect strings */

		{ "aaa", "aaa", 0, PP_ALTERNATE_FORM1, },
		{ "bbb", "bbb", 0, PP_ALTERNATE_FORM2, },
		{ "ccc", "ccc", 0, PP_EXPLICIT_PLUS, },
		{ "ddd", "ddd", 0, PP_SPACE_FOR_PLUS, },
		{ "eee", "eee", 0, PP_THOUSANDS_SEP, },

		{ NULL, NULL, 0, 0, },
	};

	sbuf = sbuf_new_auto();
	p = new_percent_esc(NULL);

	ATF_REQUIRE_EQ(sbuf != NULL, true);
	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; sv_test_vals[i].out != NULL; i++) {
		p->width = sv_test_vals[i].width;
		p->flags = sv_test_vals[i].flags;
		sbuf = string_val(sbuf, sv_test_vals[i].in, p);
		ATF_CHECK_STREQ(sbuf_data(sbuf), sv_test_vals[i].out);
		sbuf_clear(sbuf);
	}

	free_percent_esc(p);
	sbuf_delete(sbuf);
}

ATF_TC(int_val);
ATF_TC_HEAD(int_val, tc)
{
	atf_tc_set_md_var(tc, "descr",
			  "Testing int_val() output routine");
}
ATF_TC_BODY(int_val, tc)
{
	struct sbuf		*sbuf;
	struct percent_esc	*p;
	int			 i;

	struct iv_test_vals {
		int64_t in;
		const char *out;
		int width;
		unsigned flags;
	} iv_test_vals[] = {
		{ 0,      "0",   0, 0, },

		{ 1,      "1",   0, 0, },
		{ -1,     "-1",  0, 0, },

		{ 340,    "340",       0, 0, },
		{ 341,    "341",       1, 0, },
		{ 342,    "342",       2, 0, },
		{ 343,    "343",       3, 0, },
		{ 344,    " 344",      4, 0, },
		{ 345,    "  345",     5, 0, },
		{ 346,    "   346",    6, 0, },
		{ 347,    "    347",   7, 0, },
		{ 348,    "     348",  8, 0, },
		{ 349,    "      349", 9, 0, },

		{ 350,    "350",       0, PP_LEFT_ALIGN, },
		{ 351,    "351",       1, PP_LEFT_ALIGN, },
		{ 352,    "352",       2, PP_LEFT_ALIGN, },
		{ 353,    "353",       3, PP_LEFT_ALIGN, },
		{ 354,    "354 ",      4, PP_LEFT_ALIGN, },
		{ 355,    "355  ",     5, PP_LEFT_ALIGN, },
		{ 356,    "356   ",    6, PP_LEFT_ALIGN, },
		{ 357,    "357    ",   7, PP_LEFT_ALIGN, },
		{ 358,    "358     ",  8, PP_LEFT_ALIGN, },
		{ 359,    "359      ", 9, PP_LEFT_ALIGN, },

		{ 360,    "+360",      0, PP_EXPLICIT_PLUS, },
		{ 361,    "+361",      1, PP_EXPLICIT_PLUS, },
		{ 362,    "+362",      2, PP_EXPLICIT_PLUS, },
		{ 363,    "+363",      3, PP_EXPLICIT_PLUS, },
		{ 364,    "+364",      4, PP_EXPLICIT_PLUS, },
		{ 365,    " +365",     5, PP_EXPLICIT_PLUS, },
		{ 366,    "  +366",    6, PP_EXPLICIT_PLUS, },
		{ 367,    "   +367",   7, PP_EXPLICIT_PLUS, },
		{ 368,    "    +368",  8, PP_EXPLICIT_PLUS, },
		{ 369,    "     +369", 9, PP_EXPLICIT_PLUS, },

		{ -370,   "-370",      0, PP_EXPLICIT_PLUS, },
		{ -371,   "-371",      1, PP_EXPLICIT_PLUS, },
		{ -372,   "-372",      2, PP_EXPLICIT_PLUS, },
		{ -373,   "-373",      3, PP_EXPLICIT_PLUS, },
		{ -374,   "-374",      4, PP_EXPLICIT_PLUS, },
		{ -375,   " -375",     5, PP_EXPLICIT_PLUS, },
		{ -376,   "  -376",    6, PP_EXPLICIT_PLUS, },
		{ -377,   "   -377",   7, PP_EXPLICIT_PLUS, },
		{ -378,   "    -378",  8, PP_EXPLICIT_PLUS, },
		{ -379,   "     -379", 9, PP_EXPLICIT_PLUS, },

		{ 380,    " 380",      0, PP_SPACE_FOR_PLUS, },
		{ 381,    " 381",      1, PP_SPACE_FOR_PLUS, },
		{ 382,    " 382",      2, PP_SPACE_FOR_PLUS, },
		{ 383,    " 383",      3, PP_SPACE_FOR_PLUS, },
		{ 384,    " 384",      4, PP_SPACE_FOR_PLUS, },
		{ 385,    "  385",     5, PP_SPACE_FOR_PLUS, },
		{ 386,    "   386",    6, PP_SPACE_FOR_PLUS, },
		{ 387,    "    387",   7, PP_SPACE_FOR_PLUS, },
		{ 388,    "     388",  8, PP_SPACE_FOR_PLUS, },
		{ 389,    "      389", 9, PP_SPACE_FOR_PLUS, },

		{ -390,   "-390",      0, PP_SPACE_FOR_PLUS, },
		{ -391,   "-391",      1, PP_SPACE_FOR_PLUS, },
		{ -392,   "-392",      2, PP_SPACE_FOR_PLUS, },
		{ -393,   "-393",      3, PP_SPACE_FOR_PLUS, },
		{ -394,   "-394",      4, PP_SPACE_FOR_PLUS, },
		{ -395,   " -395",     5, PP_SPACE_FOR_PLUS, },
		{ -396,   "  -396",    6, PP_SPACE_FOR_PLUS, },
		{ -397,   "   -397",   7, PP_SPACE_FOR_PLUS, },
		{ -398,   "    -398",  8, PP_SPACE_FOR_PLUS, },
		{ -399,   "     -399", 9, PP_SPACE_FOR_PLUS, },

		{ 400,    "+400",      0, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ 401,    "+401",      1, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ 402,    "+402",      2, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ 403,    "+403",      3, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ 404,    "+404",      4, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ 405,    "+405 ",     5, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ 406,    "+406  ",    6, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ 407,    "+407   ",   7, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ 408,    "+408    ",  8, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ 409,    "+409     ", 9, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },

		{ -410,   "-410",      0, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ -411,   "-411",      1, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ -412,   "-412",      2, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ -413,   "-413",      3, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ -414,   "-414",      4, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ -415,   "-415 ",     5, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ -416,   "-416  ",    6, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ -417,   "-417   ",   7, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ -418,   "-418    ",  8, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },
		{ -419,   "-419     ", 9, PP_LEFT_ALIGN|PP_EXPLICIT_PLUS, },

		{ 420,    " 420",      0, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ 421,    " 421",      1, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ 422,    " 422",      2, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ 423,    " 423",      3, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ 424,    " 424",      4, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ 425,    " 425 ",     5, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ 426,    " 426  ",    6, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ 427,    " 427   ",   7, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ 428,    " 428    ",  8, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ 429,    " 429     ", 9, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },

		{ -430,   "-430",      0, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ -431,   "-431",      1, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ -432,   "-432",      2, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ -433,   "-433",      3, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ -434,   "-434",      4, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ -435,   "-435 ",     5, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ -436,   "-436  ",    6, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ -437,   "-437   ",   7, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ -438,   "-438    ",  8, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },
		{ -439,   "-439     ", 9, PP_LEFT_ALIGN|PP_SPACE_FOR_PLUS, },

		{ 440,    "440",       0, PP_ZERO_PAD, },
		{ 441,    "441",       1, PP_ZERO_PAD, },
		{ 442,    "442",       2, PP_ZERO_PAD, },
		{ 443,    "443",       3, PP_ZERO_PAD, },
		{ 444,    "0444",      4, PP_ZERO_PAD, },
		{ 445,    "00445",     5, PP_ZERO_PAD, },
		{ 446,    "000446",    6, PP_ZERO_PAD, },
		{ 447,    "0000447",   7, PP_ZERO_PAD, },
		{ 448,    "00000448",  8, PP_ZERO_PAD, },
		{ 449,    "000000449", 9, PP_ZERO_PAD, },

		{ -450,    "-450",      0, PP_ZERO_PAD, },
		{ -451,    "-451",      1, PP_ZERO_PAD, },
		{ -452,    "-452",      2, PP_ZERO_PAD, },
		{ -453,    "-453",      3, PP_ZERO_PAD, },
		{ -454,    "-454",      4, PP_ZERO_PAD, },
		{ -455,    "-0455",     5, PP_ZERO_PAD, },
		{ -456,    "-00456",    6, PP_ZERO_PAD, },
		{ -457,    "-000457",   7, PP_ZERO_PAD, },
		{ -458,    "-0000458",  8, PP_ZERO_PAD, },
		{ -459,    "-00000459", 9, PP_ZERO_PAD, },

		{ 460,    "+460",      0, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ 461,    "+461",      1, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ 462,    "+462",      2, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ 463,    "+463",      3, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ 464,    "+464",      4, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ 465,    "+0465",     5, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ 466,    "+00466",    6, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ 467,    "+000467",   7, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ 468,    "+0000468",  8, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ 469,    "+00000469", 9, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },

		{ -470,    "-470",      0, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ -471,    "-471",      1, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ -472,    "-472",      2, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ -473,    "-473",      3, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ -474,    "-474",      4, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ -475,    "-0475",     5, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ -476,    "-00476",    6, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ -477,    "-000477",   7, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ -478,    "-0000478",  8, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },
		{ -479,    "-00000479", 9, PP_EXPLICIT_PLUS|PP_ZERO_PAD, },

		{ 480,    " 480",      0, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ 481,    " 481",      1, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ 482,    " 482",      2, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ 483,    " 483",      3, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ 484,    " 484",      4, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ 485,    " 0485",     5, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ 486,    " 00486",    6, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ 487,    " 000487",   7, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ 488,    " 0000488",  8, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ 489,    " 00000489", 9, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },

		{ -490,    "-490",      0, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ -491,    "-491",      1, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ -492,    "-492",      2, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ -493,    "-493",      3, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ -494,    "-494",      4, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ -495,    "-0495",     5, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ -496,    "-00496",    6, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ -497,    "-000497",   7, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ -498,    "-0000498",  8, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },
		{ -499,    "-00000499", 9, PP_SPACE_FOR_PLUS|PP_ZERO_PAD, },

		/* PP_LEFT_ALIGN beats PP_ZERO_PAD */

		{ 500,    "500",       0, PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ 501,    "501",       1, PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ 502,    "502",       2, PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ 503,    "503",       3, PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ 504,    "504 ",      4, PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ 505,    "505  ",     5, PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ 506,    "506   ",    6, PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ 507,    "507    ",   7, PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ 508,    "508     ",  8, PP_ZERO_PAD|PP_LEFT_ALIGN, },
		{ 509,    "509      ", 9, PP_ZERO_PAD|PP_LEFT_ALIGN, },

		/* PP_EXPLICIT_PLUS beats PP_SPACE_FOR_PLUS */

		{ 700,    "+700",       0, PP_EXPLICIT_PLUS|PP_SPACE_FOR_PLUS, },

		/* See human_number for comprehensive tests of
		   PP_ALTERNATE_FORM1 and PP_ALTERNATE_FORM2 */

		{ -1, NULL, 0, 0, },
	}; 

	sbuf = sbuf_new_auto();
	p = new_percent_esc(NULL);

	ATF_REQUIRE_EQ(sbuf != NULL, true);
	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; iv_test_vals[i].out != NULL; i++) {
		p->width = iv_test_vals[i].width;
		p->flags = iv_test_vals[i].flags;
		sbuf = int_val(sbuf, iv_test_vals[i].in, p);
		ATF_CHECK_STREQ(sbuf_data(sbuf), iv_test_vals[i].out);
		sbuf_clear(sbuf);
	}

	free_percent_esc(p);
	sbuf_delete(sbuf);
}

ATF_TC(bool_val);
ATF_TC_HEAD(bool_val, tc)
{
	atf_tc_set_md_var(tc, "descr",
			  "Testing bool_val() output routine");
}
ATF_TC_BODY(bool_val, tc)
{
	struct sbuf		*sbuf;
	struct percent_esc	*p;
	int			 i;

	struct bv_test_vals {
		bool in;
		const char *out;
		int width;
		unsigned flags;
	} bv_test_vals[] = {
		{ false, "0",     0, 0, },
		{ true,  "1",     0, 0, },

		{ false, "no",    0, PP_ALTERNATE_FORM1, },
		{ true,  "yes",   0, PP_ALTERNATE_FORM1, },

		{ false, "false", 0, PP_ALTERNATE_FORM2, },
		{ true,  "true",  0, PP_ALTERNATE_FORM2, },

		/*
		 * See string_val() for tests on field-width and
		 * left-align
		 */

		{ false, NULL, 0, 0, },
	};

	sbuf = sbuf_new_auto();
	p = new_percent_esc(NULL);

	ATF_REQUIRE_EQ(sbuf != NULL, true);
	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; bv_test_vals[i].out != NULL; i++) {
		p->width = bv_test_vals[i].width;
		p->flags = bv_test_vals[i].flags;
		sbuf = bool_val(sbuf, bv_test_vals[i].in, p);
		ATF_CHECK_STREQ(sbuf_data(sbuf), bv_test_vals[i].out);
		sbuf_clear(sbuf);
	}

	free_percent_esc(p);
	sbuf_delete(sbuf);
}

ATF_TC(mode_val);
ATF_TC_HEAD(mode_val, tc)
{
	atf_tc_set_md_var(tc, "descr",
			  "Testing mode_val() output routine");
}
ATF_TC_BODY(mode_val, tc)
{
	struct sbuf		*sbuf;
	struct percent_esc	*p;
	int			 i;

	struct mv_test_vals {
		int64_t in;
		const char *out;
		int width;
		unsigned flags;
	} mv_test_vals[] = {
		{ 00000, "0",          0, 0, },
		{ 00007, "7",          0, 0, },
		{ 00070, "70",         0, 0, },
		{ 00700, "700",        0, 0, },
		{ 07000, "7000",       0, 0, },

		{ 00000, "    0",      5, 0, },
		{ 00007, "    7",      5, 0, },
		{ 00070, "   70",      5, 0, },
		{ 00700, "  700",      5, 0, },
		{ 07000, " 7000",      5, 0, },

		{ 00000, "        0",  9, 0, },
		{ 00007, "        7",  9, 0, },
		{ 00070, "       70",  9, 0, },
		{ 00700, "      700",  9, 0, },
		{ 07000, "     7000",  9, 0, },

		/*
                 * Shows a ? character for 'unknown inode type'.  Note
		 * the trailing space.
		 */

		{ 00000, "?--------- ", 0, PP_ALTERNATE_FORM1, },
		{ 00007, "?------rwx ", 0, PP_ALTERNATE_FORM1, },
		{ 00070, "?---rwx--- ", 0, PP_ALTERNATE_FORM1, },
		{ 00700, "?rwx------ ", 0, PP_ALTERNATE_FORM1, },
		{ 07000, "?--S--S--T ", 0, PP_ALTERNATE_FORM1, },
		{ 07111, "?--s--s--t ", 0, PP_ALTERNATE_FORM1, },

		{ 00000, "0",          0, PP_ALTERNATE_FORM2, },
		{ 00007, "07",         0, PP_ALTERNATE_FORM2, },
		{ 00070, "070",        0, PP_ALTERNATE_FORM2, },
		{ 00700, "0700",       0, PP_ALTERNATE_FORM2, },
		{ 07000, "07000",      0, PP_ALTERNATE_FORM2, },

		{ 00000, "    0",      5, PP_ALTERNATE_FORM2, },
		{ 00007, "   07",      5, PP_ALTERNATE_FORM2, },
		{ 00070, "  070",      5, PP_ALTERNATE_FORM2, },
		{ 00700, " 0700",      5, PP_ALTERNATE_FORM2, },
		{ 07000, "07000",      5, PP_ALTERNATE_FORM2, },

		{ 00000, "        0",  9, PP_ALTERNATE_FORM2, },
		{ 00007, "       07",  9, PP_ALTERNATE_FORM2, },
		{ 00070, "      070",  9, PP_ALTERNATE_FORM2, },
		{ 00700, "     0700",  9, PP_ALTERNATE_FORM2, },
		{ 07000, "    07000",  9, PP_ALTERNATE_FORM2, },

		/*
		 * The device type bits: 0170000
		 */

		{ 0010000, "0", 0, 0, }, /* FIFO */
		{ 0020000, "0", 0, 0, }, /* Char special */
		{ 0060000, "0", 0, 0, }, /* Block special */
		{ 0100000, "0", 0, 0, }, /* Regular file */
		{ 0120000, "0", 0, 0, }, /* Sym-link */
		{ 0140000, "0", 0, 0, }, /* socket */
		{ 0160000, "0", 0, 0, }, /* whiteout */

		{ 0010000, "p--------- ", 0, PP_ALTERNATE_FORM1, }, /* FIFO */
		{ 0020000, "c--------- ", 0, PP_ALTERNATE_FORM1, }, /* Char special */
		{ 0060000, "b--------- ", 0, PP_ALTERNATE_FORM1, }, /* Block special */
		{ 0100000, "---------- ", 0, PP_ALTERNATE_FORM1, }, /* Regular file */
		{ 0120000, "l--------- ", 0, PP_ALTERNATE_FORM1, }, /* Sym-link */
		{ 0140000, "s--------- ", 0, PP_ALTERNATE_FORM1, }, /* socket */
		{ 0160000, "w--------- ", 0, PP_ALTERNATE_FORM1, }, /* whiteout */

		{ 0010000, "10000",  0, PP_EXPLICIT_PLUS, }, /* FIFO */
		{ 0020000, "20000",  0, PP_EXPLICIT_PLUS, }, /* Char special */
		{ 0060000, "60000",  0, PP_EXPLICIT_PLUS, }, /* Block special */
		{ 0100000, "100000", 0, PP_EXPLICIT_PLUS, }, /* Regular file */
		{ 0120000, "120000", 0, PP_EXPLICIT_PLUS, }, /* Sym-link */
		{ 0140000, "140000", 0, PP_EXPLICIT_PLUS, }, /* socket */
		{ 0160000, "160000", 0, PP_EXPLICIT_PLUS, }, /* whiteout */

		{ 0, NULL, 0, 0, },
	};

	sbuf = sbuf_new_auto();
	p = new_percent_esc(NULL);

	ATF_REQUIRE_EQ(sbuf != NULL, true);
	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; mv_test_vals[i].out != NULL; i++) {
		p->width = mv_test_vals[i].width;
		p->flags = mv_test_vals[i].flags;
		sbuf = mode_val(sbuf, mv_test_vals[i].in, p);
		ATF_CHECK_STREQ(sbuf_data(sbuf), mv_test_vals[i].out);
		sbuf_clear(sbuf);
	}

	free_percent_esc(p);
	sbuf_delete(sbuf);
}
	
ATF_TC(liclog_val);
ATF_TC_HEAD(liclog_val, tc)
{
	atf_tc_set_md_var(tc, "descr",
			  "Testing liclog_val() output routine");
}
ATF_TC_BODY(liclog_val, tc)
{
	struct sbuf		*sbuf;
	struct percent_esc	*p;
	int			 i;

	struct lv_test_vals {
		lic_t in;
		const char *out;
		int width;
		unsigned flags;
	} lv_test_vals[] = {
		{ LICENSE_SINGLE, "single", 0, 0, },
		{ LICENSE_OR,     "or",     0, 0, },
		{ LICENSE_AND,    "and",    0, 0, },

		{ LICENSE_SINGLE, "",       0, PP_ALTERNATE_FORM1, },
		{ LICENSE_OR,     "|",      0, PP_ALTERNATE_FORM1, },
		{ LICENSE_AND,    "&",      0, PP_ALTERNATE_FORM1, },

		{ LICENSE_SINGLE, "==",     0, PP_ALTERNATE_FORM2, },
		{ LICENSE_OR,     "||",     0, PP_ALTERNATE_FORM2, },
		{ LICENSE_AND,    "&&",     0, PP_ALTERNATE_FORM2, },

		/*
		 * See string_val() for tests on field-width and
		 * left-align
		 */

		{ 0, NULL, 0, 0, },
	};

	sbuf = sbuf_new_auto();
	p = new_percent_esc(NULL);

	ATF_REQUIRE_EQ(sbuf != NULL, true);
	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; lv_test_vals[i].out != NULL; i++) {
		p->width = lv_test_vals[i].width;
		p->flags = lv_test_vals[i].flags;
		sbuf = liclog_val(sbuf, lv_test_vals[i].in, p);
		ATF_CHECK_STREQ(sbuf_data(sbuf), lv_test_vals[i].out);
		sbuf_clear(sbuf);
	}

	free_percent_esc(p);
	sbuf_delete(sbuf);
}


ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, gen_format);
	ATF_TP_ADD_TC(tp, human_number);
	ATF_TP_ADD_TC(tp, string_val);
	ATF_TP_ADD_TC(tp, int_val);
	ATF_TP_ADD_TC(tp, bool_val);
	ATF_TP_ADD_TC(tp, mode_val);
	ATF_TP_ADD_TC(tp, liclog_val);

	return atf_no_error();
}
/*
 * That's All Folks!
 */
