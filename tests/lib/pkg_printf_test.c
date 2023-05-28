/*-
 * Copyright (c) 2012-2015 Matthew Seaman <matthew@FreeBSD.org>
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
#include <xstring.h>
#include <pkg.h>
#include <private/pkg_printf.h>

ATF_TC_WITHOUT_HEAD(gen_format);

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
	xstring		*buf = NULL;
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

	p = new_percent_esc();

	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; hn_test_vals[i].out != NULL; i++) {
		xstring_renew(buf);
		p->width = hn_test_vals[i].width;
		p->flags = hn_test_vals[i].flags;
		buf = human_number(buf, hn_test_vals[i].in, p);
		fflush(buf->fp);
		ATF_CHECK_STREQ(buf->buf, hn_test_vals[i].out);
	}

	free_percent_esc(p);
	xstring_free(buf);
}

ATF_TC(string_val);
ATF_TC_HEAD(string_val, tc)
{
	atf_tc_set_md_var(tc, "descr",
			  "Testing string_val() output routine");
}
ATF_TC_BODY(string_val, tc)
{
	xstring		*buf = NULL;
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

		/* Zero padding a string is non-portable, so ignore 
		   that flag when printing string values */

		{ "xxz", "xxz",    0, PP_ZERO_PAD, },
		{ "xxz", "xxz",    1, PP_ZERO_PAD, },
		{ "xxz", "xxz",    2, PP_ZERO_PAD, },
		{ "xxz", "xxz",    3, PP_ZERO_PAD, },
		{ "xxz", " xxz",   4, PP_ZERO_PAD, },
		{ "xxz", "  xxz",  5, PP_ZERO_PAD, },
		{ "xxz", "   xxz", 6, PP_ZERO_PAD, },

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

	p = new_percent_esc();

	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; sv_test_vals[i].out != NULL; i++) {
		xstring_renew(buf);
		p->width = sv_test_vals[i].width;
		p->flags = sv_test_vals[i].flags;
		buf = string_val(buf, sv_test_vals[i].in, p);
		fflush(buf->fp);
		ATF_CHECK_STREQ(buf->buf, sv_test_vals[i].out);
	}

	free_percent_esc(p);
	xstring_free(buf);
}

ATF_TC(int_val);
ATF_TC_HEAD(int_val, tc)
{
	atf_tc_set_md_var(tc, "descr",
			  "Testing int_val() output routine");
}
ATF_TC_BODY(int_val, tc)
{
	xstring		*buf = NULL;
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

	p = new_percent_esc();

	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; iv_test_vals[i].out != NULL; i++) {
		xstring_renew(buf);
		p->width = iv_test_vals[i].width;
		p->flags = iv_test_vals[i].flags;
		buf = int_val(buf, iv_test_vals[i].in, p);
		fflush(buf->fp);
		ATF_CHECK_STREQ(buf->buf, iv_test_vals[i].out);
	}

	free_percent_esc(p);
	xstring_free(buf);
}

ATF_TC(bool_val);
ATF_TC_HEAD(bool_val, tc)
{
	atf_tc_set_md_var(tc, "descr",
			  "Testing bool_val() output routine");
}
ATF_TC_BODY(bool_val, tc)
{
	xstring		*buf = NULL;
	struct percent_esc	*p;
	int			 i;

	struct bv_test_vals {
		bool in;
		const char *out;
		int width;
		unsigned flags;
	} bv_test_vals[] = {
		{ false, "false", 0, 0, },
		{ true,  "true",  0, 0, },

		{ false, "no",    0, PP_ALTERNATE_FORM1, },
		{ true,  "yes",   0, PP_ALTERNATE_FORM1, },

		{ false, "",      0, PP_ALTERNATE_FORM2, },
		{ true,  "(*)",   0, PP_ALTERNATE_FORM2, },

		/*
		 * See string_val() for tests on field-width and
		 * left-align
		 */

		{ false, NULL, 0, 0, },
	};

	p = new_percent_esc();

	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; bv_test_vals[i].out != NULL; i++) {
		xstring_renew(buf);
		p->width = bv_test_vals[i].width;
		p->flags = bv_test_vals[i].flags;
		buf = bool_val(buf, bv_test_vals[i].in, p);
		fflush(buf->fp);
		ATF_CHECK_STREQ(buf->buf, bv_test_vals[i].out);
	}

	free_percent_esc(p);
	xstring_free(buf);
}

ATF_TC(mode_val);
ATF_TC_HEAD(mode_val, tc)
{
	atf_tc_set_md_var(tc, "descr",
			  "Testing mode_val() output routine");
}
ATF_TC_BODY(mode_val, tc)
{
	xstring		*buf = NULL;
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
#ifndef __linux__
		{ 0160000, "w--------- ", 0, PP_ALTERNATE_FORM1, }, /* whiteout */
#else
		{ 0160000, "?--------- ", 0, PP_ALTERNATE_FORM1, }, /* whiteout */
#endif

		{ 0010000, "10000",  0, PP_EXPLICIT_PLUS, }, /* FIFO */
		{ 0020000, "20000",  0, PP_EXPLICIT_PLUS, }, /* Char special */
		{ 0060000, "60000",  0, PP_EXPLICIT_PLUS, }, /* Block special */
		{ 0100000, "100000", 0, PP_EXPLICIT_PLUS, }, /* Regular file */
		{ 0120000, "120000", 0, PP_EXPLICIT_PLUS, }, /* Sym-link */
		{ 0140000, "140000", 0, PP_EXPLICIT_PLUS, }, /* socket */
		{ 0160000, "160000", 0, PP_EXPLICIT_PLUS, }, /* whiteout */

		{ 0, NULL, 0, 0, },
	};

	p = new_percent_esc();

	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; mv_test_vals[i].out != NULL; i++) {
		xstring_renew(buf);
		p->width = mv_test_vals[i].width;
		p->flags = mv_test_vals[i].flags;
		buf = mode_val(buf, mv_test_vals[i].in, p);
		fflush(buf->fp);
		ATF_CHECK_STREQ(buf->buf, mv_test_vals[i].out);
	}

	free_percent_esc(p);
	xstring_free(buf);
}

ATF_TC(liclog_val);
ATF_TC_HEAD(liclog_val, tc)
{
	atf_tc_set_md_var(tc, "descr",
			  "Testing liclog_val() output routine");
}
ATF_TC_BODY(liclog_val, tc)
{
	xstring		*buf = NULL;
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

	p = new_percent_esc();

	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; lv_test_vals[i].out != NULL; i++) {
		xstring_renew(buf);
		p->width = lv_test_vals[i].width;
		p->flags = lv_test_vals[i].flags;
		buf = liclog_val(buf, lv_test_vals[i].in, p);
		fflush(buf->fp);
		ATF_CHECK_STREQ(buf->buf, lv_test_vals[i].out);
	}

	free_percent_esc(p);
	xstring_free(buf);
}

ATF_TC(list_count);
ATF_TC_HEAD(list_count, tc)
{
	atf_tc_set_md_var(tc, "descr",
			  "Testing list_count() output routine");
}
ATF_TC_BODY(list_count, tc)
{
	xstring		*buf = NULL;
	struct percent_esc	*p;
	int			 i;

	struct lc_test_vals {
		int64_t in;
		const char *out;
		int width;
		unsigned flags;
	} lc_test_vals[] = {
		{ 10, "10", 0, 0, },
		{ 20, "1",  0, PP_ALTERNATE_FORM1, },
		{ 30, "30", 0, PP_ALTERNATE_FORM2, },

		/*
		 * See int_val() for tests on field-width and
		 * left-align
		 */

		{ 0, NULL, 0, 0, },
	};

	p = new_percent_esc();

	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; lc_test_vals[i].out != NULL; i++) {
		xstring_renew(buf);
		p->width = lc_test_vals[i].width;
		p->flags = lc_test_vals[i].flags;
		buf = list_count(buf, lc_test_vals[i].in, p);
		fflush(buf->fp);
		ATF_CHECK_STREQ(buf->buf, lc_test_vals[i].out);
	}

	free_percent_esc(p);
	xstring_free(buf);
}

ATF_TC(maybe_read_hex_byte);
ATF_TC_HEAD(maybe_read_hex_byte, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Testing maybe_read_hex_byte() format parsing routine");
}
ATF_TC_BODY(maybe_read_hex_byte, tc)
{
	xstring	*buf = NULL;
	const char	*f;
	int		 i;

	struct mrhb_test_vals {
		const char *in;
		const char *out; /* What gets written to the buf */
		ptrdiff_t   fend_offset; /* Where f is left pointing */
		char	    fend_val; /* expected first char in fend */
	} mrhb_test_vals[] = {
		{ "x61",   "a",    3, '\0', },
		{ "x",     "\\x",  1, '\0', },
		{ "xg",    "\\x",  1, 'g',  },
		{ "xf",    "\\x",  1, 'f',  },
		{ "xfg",   "\\x",  1, 'f',  },
		{ "xff",   "\xff", 3, '\0', },
		{ "xffg",  "\xff", 3, 'g',  },
		{ "xfffg", "\xff", 3, 'f',  },

		{ "x00",   "\0",   3, '\0', },
		{ "x01",   "\x01", 3, '\0', },
		{ "x02",   "\x02", 3, '\0', },
		{ "x03",   "\x03", 3, '\0', },
		{ "x04",   "\x04", 3, '\0', },
		{ "x05",   "\x05", 3, '\0', },
		{ "x06",   "\x06", 3, '\0', },
		{ "x07",   "\x07", 3, '\0', },
		{ "x08",   "\x08", 3, '\0', },
		{ "x09",   "\x09", 3, '\0', },
		{ "x0a",   "\x0a", 3, '\0', },
		{ "x0b",   "\x0b", 3, '\0', },
		{ "x0c",   "\x0c", 3, '\0', },
		{ "x0d",   "\x0d", 3, '\0', },
		{ "x0e",   "\x0e", 3, '\0', },
		{ "x0f",   "\x0f", 3, '\0', },

		{ "x0A",   "\x0a", 3, '\0', },
		{ "x0B",   "\x0b", 3, '\0', },
		{ "x0C",   "\x0c", 3, '\0', },
		{ "x0D",   "\x0d", 3, '\0', },
		{ "x0E",   "\x0e", 3, '\0', },
		{ "x0F",   "\x0f", 3, '\0', },

		{ "x10",   "\x10", 3, '\0', },
		{ "x20",   "\x20", 3, '\0', },
		{ "x30",   "\x30", 3, '\0', },
		{ "x40",   "\x40", 3, '\0', },
		{ "x50",   "\x50", 3, '\0', },
		{ "x60",   "\x60", 3, '\0', },
		{ "x70",   "\x70", 3, '\0', },
		{ "x80",   "\x80", 3, '\0', },
		{ "x90",   "\x90", 3, '\0', },
		{ "xa0",   "\xa0", 3, '\0', },
		{ "xb0",   "\xb0", 3, '\0', },
		{ "xc0",   "\xc0", 3, '\0', },
		{ "xd0",   "\xd0", 3, '\0', },
		{ "xe0",   "\xe0", 3, '\0', },
		{ "xf0",   "\xf0", 3, '\0', },

		{ "xA0",   "\xa0", 3, '\0', },
		{ "xB0",   "\xb0", 3, '\0', },
		{ "xC0",   "\xc0", 3, '\0', },
		{ "xD0",   "\xd0", 3, '\0', },
		{ "xE0",   "\xe0", 3, '\0', },
		{ "xF0",   "\xf0", 3, '\0', },

		{ NULL,   NULL,    0, '\0', },
	};

	for (i = 0; mrhb_test_vals[i].in != NULL; i++) {
		xstring_renew(buf);
		f = maybe_read_hex_byte(buf, mrhb_test_vals[i].in);
		fflush(buf->fp);

		ATF_CHECK_STREQ_MSG(buf->buf, mrhb_test_vals[i].out,
				    "(test %d)", i);
		ATF_CHECK_EQ_MSG(f - mrhb_test_vals[i].in,
				 mrhb_test_vals[i].fend_offset,
				 "(test %d)", i);
		ATF_CHECK_EQ_MSG(*f, mrhb_test_vals[i].fend_val,
				 "(test %d)", i);
	}

	xstring_free(buf);
}


ATF_TC(read_oct_byte);
ATF_TC_HEAD(read_oct_byte, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Testing read_oct_byte() format parsing routine");
}
ATF_TC_BODY(read_oct_byte, tc)
{
	xstring	*buf = NULL;
	const char	*f;
	int		 i;

	struct rob_test_vals {
		const char *in;
		const char *out; /* What gets written to the buf */
		ptrdiff_t   fend_offset; /* Where f is left pointing */
		char	    fend_val; /* expected first char in fend */
	} rob_test_vals[] = {
		{ "141",    "a",   3, '\0', },
		{ "0",      "\0",  1, '\0', },
		{ "08",     "\0",  1, '8',  },
		{ "008",    "\0",  2, '8',  },
		{ "0008",   "\0",  3, '8',  },
		{ "00008",  "\0",  3, '0',  },

		{ "1",     "\001", 1, '\0', },
		{ "2",     "\002", 1, '\0', },
		{ "3",     "\003", 1, '\0', },
		{ "4",     "\004", 1, '\0', },
		{ "5",     "\005", 1, '\0', },
		{ "6",     "\006", 1, '\0', },
		{ "7",     "\007", 1, '\0', },

		{ "00",    "\000", 2, '\0', },
		{ "01",    "\001", 2, '\0', },
		{ "02",    "\002", 2, '\0', },
		{ "03",    "\003", 2, '\0', },
		{ "04",    "\004", 2, '\0', },
		{ "05",    "\005", 2, '\0', },
		{ "06",    "\006", 2, '\0', },
		{ "07",    "\007", 2, '\0', },

		{ "000",   "\000", 3, '\0', },
		{ "001",   "\001", 3, '\0', },
		{ "002",   "\002", 3, '\0', },
		{ "003",   "\003", 3, '\0', },
		{ "004",   "\004", 3, '\0', },
		{ "005",   "\005", 3, '\0', },
		{ "006",   "\006", 3, '\0', },
		{ "007",   "\007", 3, '\0', },

		{ "10",    "\010", 2, '\0', },
		{ "20",    "\020", 2, '\0', },
		{ "30",    "\030", 2, '\0', },
		{ "40",    "\040", 2, '\0', },
		{ "50",    "\050", 2, '\0', },
		{ "60",    "\060", 2, '\0', },
		{ "70",    "\070", 2, '\0', },

		{ "010",   "\010", 3, '\0', },
		{ "020",   "\020", 3, '\0', },
		{ "030",   "\030", 3, '\0', },
		{ "040",   "\040", 3, '\0', },
		{ "050",   "\050", 3, '\0', },
		{ "060",   "\060", 3, '\0', },
		{ "070",   "\070", 3, '\0', },

		{ "100",   "\100", 3, '\0', },
		{ "200",   "\200", 3, '\0', },
		{ "300",   "\300", 3, '\0', },

		{ "370",   "\370", 3, '\0', },
		{ "371",   "\371", 3, '\0', },
		{ "372",   "\372", 3, '\0', },
		{ "373",   "\373", 3, '\0', },
		{ "374",   "\374", 3, '\0', },
		{ "375",   "\375", 3, '\0', },
		{ "376",   "\376", 3, '\0', },
		{ "377",   "\377", 3, '\0', },
		{ "400",   "\040", 2, '0',  },

		{ NULL,   NULL,    0, '\0', },
	};

	for (i = 0; rob_test_vals[i].in != NULL; i++) {
		xstring_renew(buf);
		f = read_oct_byte(buf, rob_test_vals[i].in);
		fflush(buf->fp);

		ATF_CHECK_STREQ_MSG(buf->buf, rob_test_vals[i].out,
				    "(test %d)", i);
		ATF_CHECK_EQ_MSG(f - rob_test_vals[i].in,
				 rob_test_vals[i].fend_offset,
				 "(test %d)", i);
		ATF_CHECK_EQ_MSG(*f, rob_test_vals[i].fend_val,
				 "(test %d)", i);
	}

	xstring_free(buf);
}

ATF_TC(process_escape);
ATF_TC_HEAD(process_escape, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Testing process_escape() format parsing routine");
}
ATF_TC_BODY(process_escape, tc)
{
	xstring	*buf = NULL;
	const char	*f;
	int		 i;

	struct pe_test_vals {
		const char *in;
		const char *out; /* What gets written to the buf */
		ptrdiff_t   fend_offset; /* Where f is left pointing */
		char	    fend_val; /* expected first char in fend */
	} pe_test_vals[] = {
		{ "\\a",   "\a",   2, '\0', },
		{ "\\b",   "\b",   2, '\0', },
		{ "\\f",   "\f",   2, '\0', },
		{ "\\n",   "\n",   2, '\0', },
		{ "\\t",   "\t",   2, '\0', },
		{ "\\v",   "\v",   2, '\0', },
		{ "\\'",   "'",    2, '\0', },
		{ "\\\"",  "\"",   2, '\0', },
		{ "\\\\",  "\\",   2, '\0', },

		{ "\\q",   "\\",   1, 'q',  },

		/* See read_oct_byte() for more comprehensive tests on
		   octal number escapes */

		{ "\\1234",  "S",   4, '4',  },
		{ "\\89",    "\\",  1, '8',  },

		/* See maybe_read_hex_byte() for more comprehensive
		   tests on hexadecimal number escapes */

		{ "\\x4cd",  "L",   4, 'd', },
		{ "\\xGG",   "\\x", 2, 'G', },

		{ NULL,   NULL,    0, '\0', },
	};

	for (i = 0; pe_test_vals[i].in != NULL; i++) {
		xstring_renew(buf);
		f = process_escape(buf, pe_test_vals[i].in);
		fflush(buf->fp);

		ATF_CHECK_STREQ_MSG(buf->buf, pe_test_vals[i].out,
				    "(test %d)", i);
		ATF_CHECK_EQ_MSG(f - pe_test_vals[i].in,
				 pe_test_vals[i].fend_offset,
				 "(test %d)", i);
		ATF_CHECK_EQ_MSG(*f, pe_test_vals[i].fend_val,
				 "(test %d)", i);
	}

	xstring_free(buf);
}

ATF_TC(field_modifier);
ATF_TC_HEAD(field_modifier, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Testing field_modifier() format parsing routine");
}
ATF_TC_BODY(field_modifier, tc)
{
	struct percent_esc	*p;
	const char		*f;
	int		 	i;

	struct fm_test_vals {
		const char *in;
		unsigned flags; 
		ptrdiff_t   fend_offset; /* Where f is left pointing */
		char	    fend_val; /* expected first char in fend */
	} fm_test_vals[] = {
		{ "?",  PP_ALTERNATE_FORM1, 1, '\0', },
		{ "#",  PP_ALTERNATE_FORM2, 1, '\0', },
		{ "-",  PP_LEFT_ALIGN,      1, '\0', },
		{ "+",  PP_EXPLICIT_PLUS,   1, '\0', },
		{ " ",  PP_SPACE_FOR_PLUS,  1, '\0', },
		{ "0",  PP_ZERO_PAD,        1, '\0', },
		{ "\'", PP_THOUSANDS_SEP,   1, '\0', },

		/* Not a format modifier... */
		{ "z",  0,  0, 'z', },
		{ "*",  0,  0, '*', },
		{ "1",  0,  0, '1', },

		{ "#",    PP_ALTERNATE_FORM2, 1, '\0', },
		{ "##",   PP_ALTERNATE_FORM2, 2, '\0', },
		{ "###",  PP_ALTERNATE_FORM2, 3, '\0', },
		{ "####", PP_ALTERNATE_FORM2, 4, '\0', },

		{ "#z",    PP_ALTERNATE_FORM2, 1, 'z', },
		{ "##z",   PP_ALTERNATE_FORM2, 2, 'z', },
		{ "###z",  PP_ALTERNATE_FORM2, 3, 'z', },
		{ "####z", PP_ALTERNATE_FORM2, 4, 'z', },

		{ "#",    PP_ALTERNATE_FORM2, 1, '\0', },
		{ "#?",   PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2, 2, '\0', },
		{ "#?#",  PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2, 3, '\0', },
		{ "#?#?", PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2, 4, '\0', },

		{ NULL,   0,    0, '\0', },
	};

	p = new_percent_esc();

	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; fm_test_vals[i].in != NULL; i++) {
		p->flags = 0;
		f = field_modifier(fm_test_vals[i].in, p);

		ATF_CHECK_EQ_MSG(p->flags, fm_test_vals[i].flags,
				    "(test %d)", i);
		ATF_CHECK_EQ_MSG(f - fm_test_vals[i].in,
				 fm_test_vals[i].fend_offset,
				 "(test %d)", i);
		ATF_CHECK_EQ_MSG(*f, fm_test_vals[i].fend_val,
				 "(test %d)", i);
	}

	free_percent_esc(p);
}

ATF_TC(field_width);
ATF_TC_HEAD(field_width, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Testing field_width() format parsing routine");
}
ATF_TC_BODY(field_width, tc)
{
	struct percent_esc	*p;
	const char		*f;
	int		 	i;

	struct fw_test_vals {
		const char *in;
		int width; 
		ptrdiff_t   fend_offset; /* Where f is left pointing */
		char	    fend_val; /* expected first char in fend */
	} fw_test_vals[] = {
		{  "0",  0, 1, '\0', },
		{  "1",  1, 1, '\0', },
		{  "2",  2, 1, '\0', },
		{  "3",  3, 1, '\0', },
		{  "4",  4, 1, '\0', },
		{  "5",  5, 1, '\0', },
		{  "6",  6, 1, '\0', },
		{  "7",  7, 1, '\0', },
		{  "8",  8, 1, '\0', },
		{  "9",  9, 1, '\0', },

		{ "10", 10, 2, '\0', },
		{ "11", 11, 2, '\0', },
		{ "12", 12, 2, '\0', },

		{ "23", 23, 2, '\0', },
		{ "34", 34, 2, '\0', },
		{ "45", 45, 2, '\0', },
		{ "56", 56, 2, '\0', },
		{ "67", 67, 2, '\0', },
		{ "78", 78, 2, '\0', },
		{ "89", 89, 2, '\0', },
		{ "90", 90, 2, '\0', },

		{ "00",  0, 2, '\0', },
		{ "001", 1, 3, '\0', },
		{ "x",   0, 0, 'x',  },
		{ "0x",  0, 1, 'x',  },

		{ NULL,  0, 0, '\0', },
	};

	p = new_percent_esc();

	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; fw_test_vals[i].in != NULL; i++) {
		p->width = 0;
		f = field_width(fw_test_vals[i].in, p);

		ATF_CHECK_EQ_MSG(p->width, fw_test_vals[i].width,
				    "(test %d)", i);
		ATF_CHECK_EQ_MSG(f - fw_test_vals[i].in,
				 fw_test_vals[i].fend_offset,
				 "(test %d)", i);
		ATF_CHECK_EQ_MSG(*f, fw_test_vals[i].fend_val,
				 "(test %d)", i);
	}

	free_percent_esc(p);
}

ATF_TC(format_code);
ATF_TC_HEAD(format_code, tc)
	
{
	atf_tc_set_md_var(tc, "descr",
	    "Testing format_code() format parsing routine");
}
ATF_TC_BODY(format_code, tc)
{
	struct percent_esc	*p;
	const char		*f;
	int		 	i;

	struct fc_test_vals {
		const char *in;
		unsigned context;
		fmt_code_t fmt_code; 
		ptrdiff_t   fend_offset; /* Where f is left pointing */
		char	    fend_val; /* expected first char in fend */
	} fc_test_vals[] = {
		{ "Bn", PP_PKG, PP_PKG_SHLIB_REQUIRED_NAME, 2, '\0', },
		{ "B",  PP_PKG, PP_PKG_SHLIBS_REQUIRED,     1, '\0', },
		{ "Cn", PP_PKG, PP_PKG_CATEGORY_NAME,       2, '\0', },
		{ "C",  PP_PKG, PP_PKG_CATEGORIES,          1, '\0', },
		{ "Dg", PP_PKG, PP_PKG_DIRECTORY_GROUP,     2, '\0', },
		{ "Dn", PP_PKG, PP_PKG_DIRECTORY_PATH,      2, '\0', },
		{ "Dp", PP_PKG, PP_PKG_DIRECTORY_PERMS,     2, '\0', },
		{ "Du", PP_PKG, PP_PKG_DIRECTORY_USER,      2, '\0', },
		{ "D",  PP_PKG, PP_PKG_DIRECTORIES,         1, '\0', },
		{ "Fg", PP_PKG, PP_PKG_FILE_GROUP,          2, '\0', },
		{ "Fn", PP_PKG, PP_PKG_FILE_PATH,           2, '\0', },
		{ "Fp", PP_PKG, PP_PKG_FILE_PERMS,          2, '\0', },
		{ "Fs", PP_PKG, PP_PKG_FILE_SHA256,         2, '\0', },
		{ "Fu", PP_PKG, PP_PKG_FILE_USER,           2, '\0', },
		{ "F",  PP_PKG, PP_PKG_FILES,               1, '\0', },
		{ "Gn", PP_PKG, PP_PKG_GROUP_NAME,          2, '\0', },
		{ "G",  PP_PKG, PP_PKG_GROUPS,              1, '\0', },
		{ "I",  PP_PKG, PP_UNKNOWN,                 0, 'I',  },
		{ "Ln", PP_PKG, PP_PKG_LICENSE_NAME,        2, '\0', },
		{ "L",  PP_PKG, PP_PKG_LICENSES,            1, '\0', },
		{ "M",  PP_PKG, PP_PKG_MESSAGE,             1, '\0', },
		{ "N",  PP_PKG, PP_PKG_REPO_IDENT,          1, '\0', },
		{ "On", PP_PKG, PP_PKG_OPTION_NAME,         2, '\0', },
		{ "Ov", PP_PKG, PP_PKG_OPTION_VALUE,        2, '\0', },
		{ "Od", PP_PKG, PP_PKG_OPTION_DEFAULT,      2, '\0', },
		{ "OD", PP_PKG, PP_PKG_OPTION_DESCRIPTION,  2, '\0', },
		{ "O",  PP_PKG, PP_PKG_OPTIONS,             1, '\0', },
		{ "R",  PP_PKG, PP_PKG_REPO_PATH,           1, '\0', },
		{ "S",  PP_PKG, PP_PKG_CHAR_STRING,         1, '\0', },
		{ "Un", PP_PKG, PP_PKG_USER_NAME,           2, '\0', },
		{ "U",  PP_PKG, PP_PKG_USERS,               1, '\0', },
		{ "V",  PP_PKG, PP_PKG_OLD_VERSION,         1, '\0', },
		{ "a",  PP_PKG, PP_PKG_AUTOREMOVE,          1, '\0', },
		{ "bn", PP_PKG, PP_PKG_SHLIB_PROVIDED_NAME, 2, '\0', },
		{ "b",  PP_PKG, PP_PKG_SHLIBS_PROVIDED,     1, '\0', },
		{ "c",  PP_PKG, PP_PKG_COMMENT,             1, '\0', },
		{ "dk", PP_PKG, PP_PKG_DEPENDENCY_LOCK,     2, '\0', },
		{ "dn", PP_PKG, PP_PKG_DEPENDENCY_NAME,     2, '\0', },
		{ "do", PP_PKG, PP_PKG_DEPENDENCY_ORIGIN,   2, '\0', },
		{ "dv", PP_PKG, PP_PKG_DEPENDENCY_VERSION,  2, '\0', },
		{ "d",  PP_PKG, PP_PKG_DEPENDENCIES,        1, '\0', },
		{ "e",  PP_PKG, PP_PKG_DESCRIPTION,         1, '\0', },
		{ "k",  PP_PKG, PP_PKG_LOCK_STATUS,         1, '\0', },
		{ "l",  PP_PKG, PP_PKG_LICENSE_LOGIC,       1, '\0', },
		{ "m",  PP_PKG, PP_PKG_MAINTAINER,          1, '\0', },
		{ "n",  PP_PKG, PP_PKG_NAME,                1, '\0', },
		{ "o",  PP_PKG, PP_PKG_ORIGIN,              1, '\0', },
		{ "p",  PP_PKG, PP_PKG_PREFIX,              1, '\0', },
		{ "q",  PP_PKG, PP_PKG_ARCHITECTURE,        1, '\0', },
		{ "rk", PP_PKG, PP_PKG_REQUIREMENT_LOCK,    2, '\0', },
		{ "rn", PP_PKG, PP_PKG_REQUIREMENT_NAME,    2, '\0', },
		{ "ro", PP_PKG, PP_PKG_REQUIREMENT_ORIGIN,  2, '\0', },
		{ "rv", PP_PKG, PP_PKG_REQUIREMENT_VERSION, 2, '\0', },
		{ "r",  PP_PKG, PP_PKG_REQUIREMENTS,        1, '\0', },
		{ "s",  PP_PKG, PP_PKG_FLATSIZE,            1, '\0', },
		{ "t",  PP_PKG, PP_PKG_INSTALL_TIMESTAMP,   1, '\0', },
		{ "u",  PP_PKG, PP_PKG_CHECKSUM,            1, '\0', },
		{ "v",  PP_PKG, PP_PKG_VERSION,             1, '\0', },
		{ "w",  PP_PKG, PP_PKG_HOME_PAGE,           1, '\0', },
		{ "x",  PP_PKG, PP_PKG_PKGSIZE,             1, '\0', },
		{ "z",  PP_PKG, PP_PKG_SHORT_CHECKSUM,      1, '\0', },
		{ "%",  PP_PKG, PP_LITERAL_PERCENT,         1, '\0', },
		{ "Z",  PP_PKG, PP_UNKNOWN,                 0, 'Z',  },

		{ "Bn", PP_B, PP_PKG_SHLIB_REQUIRED_NAME, 2, '\0', },
		{ "B",  PP_B, PP_UNKNOWN,                 0, 'B',  },
		{ "Cn", PP_B, PP_UNKNOWN,                 0, 'C',  },
		{ "C",  PP_B, PP_UNKNOWN,                 0, 'C',  },
		{ "Dg", PP_B, PP_UNKNOWN,                 0, 'D',  },
		{ "Dk", PP_B, PP_UNKNOWN,                 0, 'D',  },
		{ "Dn", PP_B, PP_UNKNOWN,                 0, 'D',  },
		{ "Dp", PP_B, PP_UNKNOWN,                 0, 'D',  },
		{ "Dt", PP_B, PP_UNKNOWN,                 0, 'D',  },
		{ "Du", PP_B, PP_UNKNOWN,                 0, 'D',  },
		{ "D",  PP_B, PP_UNKNOWN,                 0, 'D',  },
		{ "Fg", PP_B, PP_UNKNOWN,                 0, 'F',  },
		{ "Fk", PP_B, PP_UNKNOWN,                 0, 'F',  },
		{ "Fn", PP_B, PP_UNKNOWN,                 0, 'F',  },
		{ "Fp", PP_B, PP_UNKNOWN,                 0, 'F',  },
		{ "Fs", PP_B, PP_UNKNOWN,                 0, 'F',  },
		{ "Fu", PP_B, PP_UNKNOWN,                 0, 'F',  },
		{ "F",  PP_B, PP_UNKNOWN,                 0, 'F',  },
		{ "Gn", PP_B, PP_UNKNOWN,                 0, 'G',  },
		{ "G",  PP_B, PP_UNKNOWN,                 0, 'G',  },
		{ "I",  PP_B, PP_ROW_COUNTER,             1, '\0', },
		{ "Ln", PP_B, PP_UNKNOWN,                 0, 'L',  },
		{ "L",  PP_B, PP_UNKNOWN,                 0, 'L',  },
		{ "M",  PP_B, PP_PKG_MESSAGE,             1, '\0', },
		{ "N",  PP_B, PP_PKG_REPO_IDENT,          1, '\0', },
		{ "On", PP_B, PP_UNKNOWN,                 0, 'O',  },
		{ "Ov", PP_B, PP_UNKNOWN,                 0, 'O',  },
		{ "Od", PP_B, PP_UNKNOWN,                 0, 'O',  },
		{ "OD", PP_B, PP_UNKNOWN,                 0, 'O',  },
		{ "O",  PP_B, PP_UNKNOWN,                 0, 'O',  },
		{ "R",  PP_B, PP_PKG_REPO_PATH,           1, '\0', },
		{ "S",  PP_B, PP_UNKNOWN,                 0, 'S',  },
		{ "Un", PP_B, PP_UNKNOWN,                 0, 'U',  },
		{ "U",  PP_B, PP_UNKNOWN,                 0, 'U',  },
		{ "V",  PP_B, PP_PKG_OLD_VERSION,         1, '\0', },
		{ "a",  PP_B, PP_PKG_AUTOREMOVE,          1, '\0', },
		{ "bn", PP_B, PP_UNKNOWN,                 0, 'b',  },
		{ "b",  PP_B, PP_UNKNOWN,                 0, 'b',  },
		{ "c",  PP_B, PP_PKG_COMMENT,             1, '\0', },
		{ "dk", PP_B, PP_UNKNOWN,                 0, 'd',  },
		{ "dn", PP_B, PP_UNKNOWN,                 0, 'd',  },
		{ "do", PP_B, PP_UNKNOWN,                 0, 'd',  },
		{ "dv", PP_B, PP_UNKNOWN,                 0, 'd',  },
		{ "d",  PP_B, PP_UNKNOWN,                 0, 'd',  },
		{ "e",  PP_B, PP_PKG_DESCRIPTION,         1, '\0', },
		{ "k",  PP_B, PP_PKG_LOCK_STATUS,         1, '\0', },
		{ "l",  PP_B, PP_PKG_LICENSE_LOGIC,       1, '\0', },
		{ "m",  PP_B, PP_PKG_MAINTAINER,          1, '\0', },
		{ "n",  PP_B, PP_PKG_NAME,                1, '\0', },
		{ "o",  PP_B, PP_PKG_ORIGIN,              1, '\0', },
		{ "p",  PP_B, PP_PKG_PREFIX,              1, '\0', },
		{ "q",  PP_B, PP_PKG_ARCHITECTURE,        1, '\0', },
		{ "rk", PP_B, PP_UNKNOWN,                 0, 'r',  },
		{ "rn", PP_B, PP_UNKNOWN,                 0, 'r',  },
		{ "ro", PP_B, PP_UNKNOWN,                 0, 'r',  },
		{ "rv", PP_B, PP_UNKNOWN,                 0, 'r',  },
		{ "r",  PP_B, PP_UNKNOWN,                 0, 'r',  },
		{ "s",  PP_B, PP_PKG_FLATSIZE,            1, '\0', },
		{ "t",  PP_B, PP_PKG_INSTALL_TIMESTAMP,   1, '\0', },
		{ "u",  PP_B, PP_PKG_CHECKSUM,            1, '\0', },
		{ "v",  PP_B, PP_PKG_VERSION,             1, '\0', },
		{ "w",  PP_B, PP_PKG_HOME_PAGE,           1, '\0', },
		{ "x",  PP_B, PP_PKG_PKGSIZE,             1, '\0', },
		{ "z",  PP_B, PP_PKG_SHORT_CHECKSUM,      1, '\0', },
		{ "%",  PP_B, PP_LITERAL_PERCENT,         1, '\0', },
		{ "Z",  PP_B, PP_UNKNOWN,                 0, 'Z',  },

		{ "Bn", PP_C, PP_UNKNOWN,                 0, 'B',  },
		{ "B",  PP_C, PP_UNKNOWN,                 0, 'B',  },
		{ "Cn", PP_C, PP_PKG_CATEGORY_NAME,       2, '\0', },
		{ "C",  PP_C, PP_UNKNOWN,                 0, 'C',  },
		{ "Dg", PP_C, PP_UNKNOWN,                 0, 'D',  },
		{ "Dk", PP_C, PP_UNKNOWN,                 0, 'D',  },
		{ "Dn", PP_C, PP_UNKNOWN,                 0, 'D',  },
		{ "Dp", PP_C, PP_UNKNOWN,                 0, 'D',  },
		{ "Dt", PP_C, PP_UNKNOWN,                 0, 'D',  },
		{ "Du", PP_C, PP_UNKNOWN,                 0, 'D',  },
		{ "D",  PP_C, PP_UNKNOWN,                 0, 'D',  },
		{ "Fg", PP_C, PP_UNKNOWN,                 0, 'F',  },
		{ "Fk", PP_C, PP_UNKNOWN,                 0, 'F',  },
		{ "Fn", PP_C, PP_UNKNOWN,                 0, 'F',  },
		{ "Fp", PP_C, PP_UNKNOWN,                 0, 'F',  },
		{ "Fs", PP_C, PP_UNKNOWN,                 0, 'F',  },
		{ "Fu", PP_C, PP_UNKNOWN,                 0, 'F',  },
		{ "F",  PP_C, PP_UNKNOWN,                 0, 'F',  },
		{ "Gn", PP_C, PP_UNKNOWN,                 0, 'G',  },
		{ "G",  PP_C, PP_UNKNOWN,                 0, 'G',  },
		{ "I",  PP_C, PP_ROW_COUNTER,             1, '\0', },
		{ "Ln", PP_C, PP_UNKNOWN,                 0, 'L',  },
		{ "L",  PP_C, PP_UNKNOWN,                 0, 'L',  },
		{ "M",  PP_C, PP_PKG_MESSAGE,             1, '\0', },
		{ "N",  PP_C, PP_PKG_REPO_IDENT,          1, '\0', },
		{ "On", PP_C, PP_UNKNOWN,                 0, 'O',  },
		{ "Ov", PP_C, PP_UNKNOWN,                 0, 'O',  },
		{ "Od", PP_C, PP_UNKNOWN,                 0, 'O',  },
		{ "OD", PP_C, PP_UNKNOWN,                 0, 'O',  },
		{ "O",  PP_C, PP_UNKNOWN,                 0, 'O',  },
		{ "R",  PP_C, PP_PKG_REPO_PATH,           1, '\0', },
		{ "S",  PP_C, PP_UNKNOWN,                 0, 'S',  },
		{ "Un", PP_C, PP_UNKNOWN,                 0, 'U',  },
		{ "U",  PP_C, PP_UNKNOWN,                 0, 'U',  },
		{ "V",  PP_C, PP_PKG_OLD_VERSION,         1, '\0', },
		{ "a",  PP_C, PP_PKG_AUTOREMOVE,          1, '\0', },
		{ "bn", PP_C, PP_UNKNOWN,                 0, 'b',  },
		{ "b",  PP_C, PP_UNKNOWN,                 0, 'b',  },
		{ "c",  PP_C, PP_PKG_COMMENT,             1, '\0', },
		{ "dk", PP_C, PP_UNKNOWN,                 0, 'd',  },
		{ "dn", PP_C, PP_UNKNOWN,                 0, 'd',  },
		{ "do", PP_C, PP_UNKNOWN,                 0, 'd',  },
		{ "dv", PP_C, PP_UNKNOWN,                 0, 'd',  },
		{ "d",  PP_C, PP_UNKNOWN,                 0, 'd',  },
		{ "e",  PP_C, PP_PKG_DESCRIPTION,         1, '\0', },
		{ "k",  PP_C, PP_PKG_LOCK_STATUS,         1, '\0', },
		{ "l",  PP_C, PP_PKG_LICENSE_LOGIC,       1, '\0', },
		{ "m",  PP_C, PP_PKG_MAINTAINER,          1, '\0', },
		{ "n",  PP_C, PP_PKG_NAME,                1, '\0', },
		{ "o",  PP_C, PP_PKG_ORIGIN,              1, '\0', },
		{ "p",  PP_C, PP_PKG_PREFIX,              1, '\0', },
		{ "q",  PP_C, PP_PKG_ARCHITECTURE,        1, '\0', },
		{ "rk", PP_C, PP_UNKNOWN,                 0, 'r',  },
		{ "rn", PP_C, PP_UNKNOWN,                 0, 'r',  },
		{ "ro", PP_C, PP_UNKNOWN,                 0, 'r',  },
		{ "rv", PP_C, PP_UNKNOWN,                 0, 'r',  },
		{ "r",  PP_C, PP_UNKNOWN,                 0, 'r',  },
		{ "s",  PP_C, PP_PKG_FLATSIZE,            1, '\0', },
		{ "t",  PP_C, PP_PKG_INSTALL_TIMESTAMP,   1, '\0', },
		{ "u",  PP_C, PP_PKG_CHECKSUM,            1, '\0', },
		{ "v",  PP_C, PP_PKG_VERSION,             1, '\0', },
		{ "w",  PP_C, PP_PKG_HOME_PAGE,           1, '\0', },
		{ "x",  PP_C, PP_PKG_PKGSIZE,             1, '\0', },
		{ "z",  PP_C, PP_PKG_SHORT_CHECKSUM,      1, '\0', },
		{ "%",  PP_C, PP_LITERAL_PERCENT,         1, '\0', },
		{ "Z",  PP_C, PP_UNKNOWN,                 0, 'Z',  },

		{ "Bn", PP_D, PP_UNKNOWN,                 0, 'B',  },
		{ "B",  PP_D, PP_UNKNOWN,                 0, 'B',  },
		{ "Cn", PP_D, PP_UNKNOWN,                 0, 'C',  },
		{ "C",  PP_D, PP_UNKNOWN,                 0, 'C',  },
		{ "Dg", PP_D, PP_PKG_DIRECTORY_GROUP,     2, '\0', },
		{ "Dn", PP_D, PP_PKG_DIRECTORY_PATH,      2, '\0', },
		{ "Dp", PP_D, PP_PKG_DIRECTORY_PERMS,     2, '\0', },
		{ "Du", PP_D, PP_PKG_DIRECTORY_USER,      2, '\0', },
		{ "D",  PP_D, PP_UNKNOWN,                 0, 'D',  },
		{ "Fg", PP_D, PP_UNKNOWN,                 0, 'F',  },
		{ "Fk", PP_D, PP_UNKNOWN,                 0, 'F',  },
		{ "Fn", PP_D, PP_UNKNOWN,                 0, 'F',  },
		{ "Fp", PP_D, PP_UNKNOWN,                 0, 'F',  },
		{ "Fs", PP_D, PP_UNKNOWN,                 0, 'F',  },
		{ "Fu", PP_D, PP_UNKNOWN,                 0, 'F',  },
		{ "F",  PP_D, PP_UNKNOWN,                 0, 'F',  },
		{ "Gn", PP_D, PP_UNKNOWN,                 0, 'G',  },
		{ "G",  PP_D, PP_UNKNOWN,                 0, 'G',  },
		{ "I",  PP_D, PP_ROW_COUNTER,             1, '\0', },
		{ "Ln", PP_D, PP_UNKNOWN,                 0, 'L',  },
		{ "L",  PP_D, PP_UNKNOWN,                 0, 'L',  },
		{ "M",  PP_D, PP_PKG_MESSAGE,             1, '\0', },
		{ "N",  PP_D, PP_PKG_REPO_IDENT,          1, '\0', },
		{ "On", PP_D, PP_UNKNOWN,                 0, 'O',  },
		{ "Ov", PP_D, PP_UNKNOWN,                 0, 'O',  },
		{ "Od", PP_D, PP_UNKNOWN,                 0, 'O',  },
		{ "OD", PP_D, PP_UNKNOWN,                 0, 'O',  },
		{ "O",  PP_D, PP_UNKNOWN,                 0, 'O',  },
		{ "R",  PP_D, PP_PKG_REPO_PATH,           1, '\0', },
		{ "S",  PP_D, PP_UNKNOWN,                 0, 'S',  },
		{ "Un", PP_D, PP_UNKNOWN,                 0, 'U',  },
		{ "U",  PP_D, PP_UNKNOWN,                 0, 'U',  },
		{ "V",  PP_D, PP_PKG_OLD_VERSION,         1, '\0', },
		{ "a",  PP_D, PP_PKG_AUTOREMOVE,          1, '\0', },
		{ "bn", PP_D, PP_UNKNOWN,                 0, 'b',  },
		{ "b",  PP_D, PP_UNKNOWN,                 0, 'b',  },
		{ "c",  PP_D, PP_PKG_COMMENT,             1, '\0', },
		{ "dk", PP_D, PP_UNKNOWN,                 0, 'd',  },
		{ "dn", PP_D, PP_UNKNOWN,                 0, 'd',  },
		{ "do", PP_D, PP_UNKNOWN,                 0, 'd',  },
		{ "dv", PP_D, PP_UNKNOWN,                 0, 'd',  },
		{ "d",  PP_D, PP_UNKNOWN,                 0, 'd',  },
		{ "e",  PP_D, PP_PKG_DESCRIPTION,         1, '\0', },
		{ "k",  PP_D, PP_PKG_LOCK_STATUS,         1, '\0', },
		{ "l",  PP_D, PP_PKG_LICENSE_LOGIC,       1, '\0', },
		{ "m",  PP_D, PP_PKG_MAINTAINER,          1, '\0', },
		{ "n",  PP_D, PP_PKG_NAME,                1, '\0', },
		{ "o",  PP_D, PP_PKG_ORIGIN,              1, '\0', },
		{ "p",  PP_D, PP_PKG_PREFIX,              1, '\0', },
		{ "q",  PP_D, PP_PKG_ARCHITECTURE,        1, '\0', },
		{ "rk", PP_D, PP_UNKNOWN,                 0, 'r',  },
		{ "rn", PP_D, PP_UNKNOWN,                 0, 'r',  },
		{ "ro", PP_D, PP_UNKNOWN,                 0, 'r',  },
		{ "rv", PP_D, PP_UNKNOWN,                 0, 'r',  },
		{ "r",  PP_D, PP_UNKNOWN,                 0, 'r',  },
		{ "s",  PP_D, PP_PKG_FLATSIZE,            1, '\0', },
		{ "t",  PP_D, PP_PKG_INSTALL_TIMESTAMP,   1, '\0', },
		{ "u",  PP_D, PP_PKG_CHECKSUM,            1, '\0', },
		{ "v",  PP_D, PP_PKG_VERSION,             1, '\0', },
		{ "w",  PP_D, PP_PKG_HOME_PAGE,           1, '\0', },
		{ "x",  PP_D, PP_PKG_PKGSIZE,             1, '\0', },
		{ "z",  PP_D, PP_PKG_SHORT_CHECKSUM,      1, '\0', },
		{ "%",  PP_D, PP_LITERAL_PERCENT,         1, '\0', },
		{ "Z",  PP_D, PP_UNKNOWN,                 0, 'Z',  },

		{ "Bn", PP_F, PP_UNKNOWN,                 0, 'B',  },
		{ "B",  PP_F, PP_UNKNOWN,                 0, 'B',  },
		{ "Cn", PP_F, PP_UNKNOWN,                 0, 'C',  },
		{ "C",  PP_F, PP_UNKNOWN,                 0, 'C',  },
		{ "Dg", PP_F, PP_UNKNOWN,                 0, 'D',  },
		{ "Dk", PP_F, PP_UNKNOWN,                 0, 'D',  },
		{ "Dn", PP_F, PP_UNKNOWN,                 0, 'D',  },
		{ "Dp", PP_F, PP_UNKNOWN,                 0, 'D',  },
		{ "Dt", PP_F, PP_UNKNOWN,                 0, 'D',  },
		{ "Du", PP_F, PP_UNKNOWN,                 0, 'D',  },
		{ "D",  PP_F, PP_UNKNOWN,                 0, 'D',  },
		{ "Fg", PP_F, PP_PKG_FILE_GROUP,          2, '\0', },
		{ "Fn", PP_F, PP_PKG_FILE_PATH,           2, '\0', },
		{ "Fp", PP_F, PP_PKG_FILE_PERMS,          2, '\0', },
		{ "Fs", PP_F, PP_PKG_FILE_SHA256,         2, '\0', },
		{ "Fu", PP_F, PP_PKG_FILE_USER,           2, '\0', },
		{ "F",  PP_F, PP_UNKNOWN,                 0, 'F',  },
		{ "Gn", PP_F, PP_UNKNOWN,                 0, 'G',  },
		{ "G",  PP_F, PP_UNKNOWN,                 0, 'G',  },
		{ "I",  PP_F, PP_ROW_COUNTER,             1, '\0', },
		{ "Ln", PP_F, PP_UNKNOWN,                 0, 'L',  },
		{ "L",  PP_F, PP_UNKNOWN,                 0, 'L',  },
		{ "M",  PP_F, PP_PKG_MESSAGE,             1, '\0', },
		{ "N",  PP_F, PP_PKG_REPO_IDENT,          1, '\0', },
		{ "On", PP_F, PP_UNKNOWN,                 0, 'O',  },
		{ "Ov", PP_F, PP_UNKNOWN,                 0, 'O',  },
		{ "Od", PP_F, PP_UNKNOWN,                 0, 'O',  },
		{ "OD", PP_F, PP_UNKNOWN,                 0, 'O',  },
		{ "O",  PP_F, PP_UNKNOWN,                 0, 'O',  },
		{ "R",  PP_F, PP_PKG_REPO_PATH,           1, '\0', },
		{ "S",  PP_F, PP_UNKNOWN,                 0, 'S',  },
		{ "Un", PP_F, PP_UNKNOWN,                 0, 'U',  },
		{ "U",  PP_F, PP_UNKNOWN,                 0, 'U',  },
		{ "V",  PP_F, PP_PKG_OLD_VERSION,         1, '\0', },
		{ "a",  PP_F, PP_PKG_AUTOREMOVE,          1, '\0', },
		{ "bn", PP_F, PP_UNKNOWN,                 0, 'b',  },
		{ "b",  PP_F, PP_UNKNOWN,                 0, 'b',  },
		{ "c",  PP_F, PP_PKG_COMMENT,             1, '\0', },
		{ "dk", PP_F, PP_UNKNOWN,                 0, 'd',  },
		{ "dn", PP_F, PP_UNKNOWN,                 0, 'd',  },
		{ "do", PP_F, PP_UNKNOWN,                 0, 'd',  },
		{ "dv", PP_F, PP_UNKNOWN,                 0, 'd',  },
		{ "d",  PP_F, PP_UNKNOWN,                 0, 'd',  },
		{ "e",  PP_F, PP_PKG_DESCRIPTION,         1, '\0', },
		{ "k",  PP_F, PP_PKG_LOCK_STATUS,         1, '\0', },
		{ "l",  PP_F, PP_PKG_LICENSE_LOGIC,       1, '\0', },
		{ "m",  PP_F, PP_PKG_MAINTAINER,          1, '\0', },
		{ "n",  PP_F, PP_PKG_NAME,                1, '\0', },
		{ "o",  PP_F, PP_PKG_ORIGIN,              1, '\0', },
		{ "p",  PP_F, PP_PKG_PREFIX,              1, '\0', },
		{ "q",  PP_F, PP_PKG_ARCHITECTURE,        1, '\0', },
		{ "rk", PP_F, PP_UNKNOWN,                 0, 'r',  },
		{ "rn", PP_F, PP_UNKNOWN,                 0, 'r',  },
		{ "ro", PP_F, PP_UNKNOWN,                 0, 'r',  },
		{ "rv", PP_F, PP_UNKNOWN,                 0, 'r',  },
		{ "r",  PP_F, PP_UNKNOWN,                 0, 'r',  },
		{ "s",  PP_F, PP_PKG_FLATSIZE,            1, '\0', },
		{ "t",  PP_F, PP_PKG_INSTALL_TIMESTAMP,   1, '\0', },
		{ "u",  PP_F, PP_PKG_CHECKSUM,            1, '\0', },
		{ "v",  PP_F, PP_PKG_VERSION,             1, '\0', },
		{ "w",  PP_F, PP_PKG_HOME_PAGE,           1, '\0', },
		{ "x",  PP_F, PP_PKG_PKGSIZE,             1, '\0', },
		{ "z",  PP_F, PP_PKG_SHORT_CHECKSUM,      1, '\0', },
		{ "%",  PP_F, PP_LITERAL_PERCENT,         1, '\0', },
		{ "Z",  PP_F, PP_UNKNOWN,                 0, 'Z',  },

		{ "Bn", PP_G, PP_UNKNOWN,                 0, 'B',  },
		{ "B",  PP_G, PP_UNKNOWN,                 0, 'B',  },
		{ "Cn", PP_G, PP_UNKNOWN,                 0, 'C',  },
		{ "C",  PP_G, PP_UNKNOWN,                 0, 'C',  },
		{ "Dg", PP_G, PP_UNKNOWN,                 0, 'D',  },
		{ "Dk", PP_G, PP_UNKNOWN,                 0, 'D',  },
		{ "Dn", PP_G, PP_UNKNOWN,                 0, 'D',  },
		{ "Dp", PP_G, PP_UNKNOWN,                 0, 'D',  },
		{ "Dt", PP_G, PP_UNKNOWN,                 0, 'D',  },
		{ "Du", PP_G, PP_UNKNOWN,                 0, 'D',  },
		{ "D",  PP_G, PP_UNKNOWN,                 0, 'D',  },
		{ "Fg", PP_G, PP_UNKNOWN,                 0, 'F',  },
		{ "Fk", PP_G, PP_UNKNOWN,                 0, 'F',  },
		{ "Fn", PP_G, PP_UNKNOWN,                 0, 'F',  },
		{ "Fp", PP_G, PP_UNKNOWN,                 0, 'F',  },
		{ "Fs", PP_G, PP_UNKNOWN,                 0, 'F',  },
		{ "Fu", PP_G, PP_UNKNOWN,                 0, 'F',  },
		{ "F",  PP_G, PP_UNKNOWN,                 0, 'F',  },
		{ "Gn", PP_G, PP_PKG_GROUP_NAME,          2, '\0', },
		{ "G",  PP_G, PP_UNKNOWN,                 0, 'G',  },
		{ "I",  PP_G, PP_ROW_COUNTER,             1, '\0', },
		{ "Ln", PP_G, PP_UNKNOWN,                 0, 'L',  },
		{ "L",  PP_G, PP_UNKNOWN,                 0, 'L',  },
		{ "M",  PP_G, PP_PKG_MESSAGE,             1, '\0', },
		{ "N",  PP_G, PP_PKG_REPO_IDENT,          1, '\0', },
		{ "On", PP_G, PP_UNKNOWN,                 0, 'O',  },
		{ "Ov", PP_G, PP_UNKNOWN,                 0, 'O',  },
		{ "Od", PP_G, PP_UNKNOWN,                 0, 'O',  },
		{ "OD", PP_G, PP_UNKNOWN,                 0, 'O',  },
		{ "O",  PP_G, PP_UNKNOWN,                 0, 'O',  },
		{ "R",  PP_G, PP_PKG_REPO_PATH,           1, '\0', },
		{ "S",  PP_G, PP_UNKNOWN,                 0, 'S',  },
		{ "Un", PP_G, PP_UNKNOWN,                 0, 'U',  },
		{ "U",  PP_G, PP_UNKNOWN,                 0, 'U',  },
		{ "V",  PP_G, PP_PKG_OLD_VERSION,         1, '\0', },
		{ "a",  PP_G, PP_PKG_AUTOREMOVE,          1, '\0', },
		{ "bn", PP_G, PP_UNKNOWN,                 0, 'b',  },
		{ "b",  PP_G, PP_UNKNOWN,                 0, 'b',  },
		{ "c",  PP_G, PP_PKG_COMMENT,             1, '\0', },
		{ "dk", PP_G, PP_UNKNOWN,                 0, 'd',  },
		{ "dn", PP_G, PP_UNKNOWN,                 0, 'd',  },
		{ "do", PP_G, PP_UNKNOWN,                 0, 'd',  },
		{ "dv", PP_G, PP_UNKNOWN,                 0, 'd',  },
		{ "d",  PP_G, PP_UNKNOWN,                 0, 'd',  },
		{ "e",  PP_G, PP_PKG_DESCRIPTION,         1, '\0', },
		{ "k",  PP_G, PP_PKG_LOCK_STATUS,         1, '\0', },
		{ "l",  PP_G, PP_PKG_LICENSE_LOGIC,       1, '\0', },
		{ "m",  PP_G, PP_PKG_MAINTAINER,          1, '\0', },
		{ "n",  PP_G, PP_PKG_NAME,                1, '\0', },
		{ "o",  PP_G, PP_PKG_ORIGIN,              1, '\0', },
		{ "p",  PP_G, PP_PKG_PREFIX,              1, '\0', },
		{ "q",  PP_G, PP_PKG_ARCHITECTURE,        1, '\0', },
		{ "rk", PP_G, PP_UNKNOWN,                 0, 'r',  },
		{ "rn", PP_G, PP_UNKNOWN,                 0, 'r',  },
		{ "ro", PP_G, PP_UNKNOWN,                 0, 'r',  },
		{ "rv", PP_G, PP_UNKNOWN,                 0, 'r',  },
		{ "r",  PP_G, PP_UNKNOWN,                 0, 'r',  },
		{ "s",  PP_G, PP_PKG_FLATSIZE,            1, '\0', },
		{ "t",  PP_G, PP_PKG_INSTALL_TIMESTAMP,   1, '\0', },
		{ "u",  PP_G, PP_PKG_CHECKSUM,            1, '\0', },
		{ "v",  PP_G, PP_PKG_VERSION,             1, '\0', },
		{ "w",  PP_G, PP_PKG_HOME_PAGE,           1, '\0', },
		{ "x",  PP_G, PP_PKG_PKGSIZE,             1, '\0', },
		{ "z",  PP_G, PP_PKG_SHORT_CHECKSUM,      1, '\0', },
		{ "%",  PP_G, PP_LITERAL_PERCENT,         1, '\0', },
		{ "Z",  PP_G, PP_UNKNOWN,                 0, 'Z',  },

		{ "Bn", PP_L, PP_UNKNOWN,                 0, 'B',  },
		{ "B",  PP_L, PP_UNKNOWN,                 0, 'B',  },
		{ "Cn", PP_L, PP_UNKNOWN,                 0, 'C',  },
		{ "C",  PP_L, PP_UNKNOWN,                 0, 'C',  },
		{ "Dg", PP_L, PP_UNKNOWN,                 0, 'D',  },
		{ "Dk", PP_L, PP_UNKNOWN,                 0, 'D',  },
		{ "Dn", PP_L, PP_UNKNOWN,                 0, 'D',  },
		{ "Dp", PP_L, PP_UNKNOWN,                 0, 'D',  },
		{ "Dt", PP_L, PP_UNKNOWN,                 0, 'D',  },
		{ "Du", PP_L, PP_UNKNOWN,                 0, 'D',  },
		{ "D",  PP_L, PP_UNKNOWN,                 0, 'D',  },
		{ "Fg", PP_L, PP_UNKNOWN,                 0, 'F',  },
		{ "Fk", PP_L, PP_UNKNOWN,                 0, 'F',  },
		{ "Fn", PP_L, PP_UNKNOWN,                 0, 'F',  },
		{ "Fp", PP_L, PP_UNKNOWN,                 0, 'F',  },
		{ "Fs", PP_L, PP_UNKNOWN,                 0, 'F',  },
		{ "Fu", PP_L, PP_UNKNOWN,                 0, 'F',  },
		{ "F",  PP_L, PP_UNKNOWN,                 0, 'F',  },
		{ "Gn", PP_L, PP_UNKNOWN,                 0, 'G',  },
		{ "G",  PP_L, PP_UNKNOWN,                 0, 'G',  },
		{ "I",  PP_L, PP_ROW_COUNTER,             1, '\0', },
		{ "Ln", PP_L, PP_PKG_LICENSE_NAME,        2, '\0', },
		{ "L",  PP_L, PP_UNKNOWN,                 0, 'L',  },
		{ "M",  PP_L, PP_PKG_MESSAGE,             1, '\0', },
		{ "N",  PP_L, PP_PKG_REPO_IDENT,          1, '\0', },
		{ "On", PP_L, PP_UNKNOWN,                 0, 'O',  },
		{ "Ov", PP_L, PP_UNKNOWN,                 0, 'O',  },
		{ "Od", PP_L, PP_UNKNOWN,                 0, 'O',  },
		{ "OD", PP_L, PP_UNKNOWN,                 0, 'O',  },
		{ "O",  PP_L, PP_UNKNOWN,                 0, 'O',  },
		{ "R",  PP_L, PP_PKG_REPO_PATH,           1, '\0', },
		{ "S",  PP_L, PP_UNKNOWN,                 0, 'S',  },
		{ "Un", PP_L, PP_UNKNOWN,                 0, 'U',  },
		{ "U",  PP_L, PP_UNKNOWN,                 0, 'U',  },
		{ "V",  PP_L, PP_PKG_OLD_VERSION,         1, '\0', },
		{ "a",  PP_L, PP_PKG_AUTOREMOVE,          1, '\0', },
		{ "bn", PP_L, PP_UNKNOWN,                 0, 'b',  },
		{ "b",  PP_L, PP_UNKNOWN,                 0, 'b',  },
		{ "c",  PP_L, PP_PKG_COMMENT,             1, '\0', },
		{ "dk", PP_L, PP_UNKNOWN,                 0, 'd',  },
		{ "dn", PP_L, PP_UNKNOWN,                 0, 'd',  },
		{ "do", PP_L, PP_UNKNOWN,                 0, 'd',  },
		{ "dv", PP_L, PP_UNKNOWN,                 0, 'd',  },
		{ "d",  PP_L, PP_UNKNOWN,                 0, 'd',  },
		{ "e",  PP_L, PP_PKG_DESCRIPTION,         1, '\0', },
		{ "k",  PP_L, PP_PKG_LOCK_STATUS,         1, '\0', },
		{ "l",  PP_L, PP_PKG_LICENSE_LOGIC,       1, '\0', },
		{ "m",  PP_L, PP_PKG_MAINTAINER,          1, '\0', },
		{ "n",  PP_L, PP_PKG_NAME,                1, '\0', },
		{ "o",  PP_L, PP_PKG_ORIGIN,              1, '\0', },
		{ "p",  PP_L, PP_PKG_PREFIX,              1, '\0', },
		{ "q",  PP_L, PP_PKG_ARCHITECTURE,        1, '\0', },
		{ "rk", PP_L, PP_UNKNOWN,                 0, 'r',  },
		{ "rn", PP_L, PP_UNKNOWN,                 0, 'r',  },
		{ "ro", PP_L, PP_UNKNOWN,                 0, 'r',  },
		{ "rv", PP_L, PP_UNKNOWN,                 0, 'r',  },
		{ "r",  PP_L, PP_UNKNOWN,                 0, 'r',  },
		{ "s",  PP_L, PP_PKG_FLATSIZE,            1, '\0', },
		{ "t",  PP_L, PP_PKG_INSTALL_TIMESTAMP,   1, '\0', },
		{ "u",  PP_L, PP_PKG_CHECKSUM,            1, '\0', },
		{ "v",  PP_L, PP_PKG_VERSION,             1, '\0', },
		{ "w",  PP_L, PP_PKG_HOME_PAGE,           1, '\0', },
		{ "x",  PP_L, PP_PKG_PKGSIZE,             1, '\0', },
		{ "z",  PP_L, PP_PKG_SHORT_CHECKSUM,      1, '\0', },
		{ "%",  PP_L, PP_LITERAL_PERCENT,         1, '\0', },
		{ "Z",  PP_L, PP_UNKNOWN,                 0, 'Z',  },

		{ "Bn", PP_O, PP_UNKNOWN,                 0, 'B',  },
		{ "B",  PP_O, PP_UNKNOWN,                 0, 'B',  },
		{ "Cn", PP_O, PP_UNKNOWN,                 0, 'C',  },
		{ "C",  PP_O, PP_UNKNOWN,                 0, 'C',  },
		{ "Dg", PP_O, PP_UNKNOWN,                 0, 'D',  },
		{ "Dk", PP_O, PP_UNKNOWN,                 0, 'D',  },
		{ "Dn", PP_O, PP_UNKNOWN,                 0, 'D',  },
		{ "Dp", PP_O, PP_UNKNOWN,                 0, 'D',  },
		{ "Dt", PP_O, PP_UNKNOWN,                 0, 'D',  },
		{ "Du", PP_O, PP_UNKNOWN,                 0, 'D',  },
		{ "D",  PP_O, PP_UNKNOWN,                 0, 'D',  },
		{ "Fg", PP_O, PP_UNKNOWN,                 0, 'F',  },
		{ "Fk", PP_O, PP_UNKNOWN,                 0, 'F',  },
		{ "Fn", PP_O, PP_UNKNOWN,                 0, 'F',  },
		{ "Fp", PP_O, PP_UNKNOWN,                 0, 'F',  },
		{ "Fs", PP_O, PP_UNKNOWN,                 0, 'F',  },
		{ "Fu", PP_O, PP_UNKNOWN,                 0, 'F',  },
		{ "F",  PP_O, PP_UNKNOWN,                 0, 'F',  },
		{ "Gn", PP_O, PP_UNKNOWN,                 0, 'G',  },
		{ "G",  PP_O, PP_UNKNOWN,                 0, 'G',  },
		{ "I",  PP_O, PP_ROW_COUNTER,             1, '\0', },
		{ "Ln", PP_O, PP_UNKNOWN,                 0, 'L',  },
		{ "L",  PP_O, PP_UNKNOWN,                 0, 'L',  },
		{ "M",  PP_O, PP_PKG_MESSAGE,             1, '\0', },
		{ "N",  PP_O, PP_PKG_REPO_IDENT,          1, '\0', },
		{ "On", PP_O, PP_PKG_OPTION_NAME,         2, '\0', },
		{ "Ov", PP_O, PP_PKG_OPTION_VALUE,        2, '\0', },
		{ "Od", PP_O, PP_PKG_OPTION_DEFAULT,      2, '\0', },
		{ "OD", PP_O, PP_PKG_OPTION_DESCRIPTION,  2, '\0', },
		{ "O",  PP_O, PP_UNKNOWN,                 0, 'O',  },
		{ "R",  PP_O, PP_PKG_REPO_PATH,           1, '\0', },
		{ "S",  PP_O, PP_UNKNOWN,                 0, 'S',  },
		{ "Un", PP_O, PP_UNKNOWN,                 0, 'U',  },
		{ "U",  PP_O, PP_UNKNOWN,                 0, 'U',  },
		{ "V",  PP_O, PP_PKG_OLD_VERSION,         1, '\0', },
		{ "a",  PP_O, PP_PKG_AUTOREMOVE,          1, '\0', },
		{ "bn", PP_O, PP_UNKNOWN,                 0, 'b',  },
		{ "b",  PP_O, PP_UNKNOWN,                 0, 'b',  },
		{ "c",  PP_O, PP_PKG_COMMENT,             1, '\0', },
		{ "dk", PP_O, PP_UNKNOWN,                 0, 'd',  },
		{ "dn", PP_O, PP_UNKNOWN,                 0, 'd',  },
		{ "do", PP_O, PP_UNKNOWN,                 0, 'd',  },
		{ "dv", PP_O, PP_UNKNOWN,                 0, 'd',  },
		{ "d",  PP_O, PP_UNKNOWN,                 0, 'd',  },
		{ "e",  PP_O, PP_PKG_DESCRIPTION,         1, '\0', },
		{ "k",  PP_O, PP_PKG_LOCK_STATUS,         1, '\0', },
		{ "l",  PP_O, PP_PKG_LICENSE_LOGIC,       1, '\0', },
		{ "m",  PP_O, PP_PKG_MAINTAINER,          1, '\0', },
		{ "n",  PP_O, PP_PKG_NAME,                1, '\0', },
		{ "o",  PP_O, PP_PKG_ORIGIN,              1, '\0', },
		{ "p",  PP_O, PP_PKG_PREFIX,              1, '\0', },
		{ "q",  PP_O, PP_PKG_ARCHITECTURE,        1, '\0', },
		{ "rk", PP_O, PP_UNKNOWN,                 0, 'r',  },
		{ "rn", PP_O, PP_UNKNOWN,                 0, 'r',  },
		{ "ro", PP_O, PP_UNKNOWN,                 0, 'r',  },
		{ "rv", PP_O, PP_UNKNOWN,                 0, 'r',  },
		{ "r",  PP_O, PP_UNKNOWN,                 0, 'r',  },
		{ "s",  PP_O, PP_PKG_FLATSIZE,            1, '\0', },
		{ "t",  PP_O, PP_PKG_INSTALL_TIMESTAMP,   1, '\0', },
		{ "u",  PP_O, PP_PKG_CHECKSUM,            1, '\0', },
		{ "v",  PP_O, PP_PKG_VERSION,             1, '\0', },
		{ "w",  PP_O, PP_PKG_HOME_PAGE,           1, '\0', },
		{ "x",  PP_O, PP_PKG_PKGSIZE,             1, '\0', },
		{ "z",  PP_O, PP_PKG_SHORT_CHECKSUM,      1, '\0', },
		{ "%",  PP_O, PP_LITERAL_PERCENT,         1, '\0', },
		{ "Z",  PP_O, PP_UNKNOWN,                 0, 'Z',  },

		{ "Bn", PP_U, PP_UNKNOWN,                 0, 'B',  },
		{ "B",  PP_U, PP_UNKNOWN,                 0, 'B',  },
		{ "Cn", PP_U, PP_UNKNOWN,                 0, 'C',  },
		{ "C",  PP_U, PP_UNKNOWN,                 0, 'C',  },
		{ "Dg", PP_U, PP_UNKNOWN,                 0, 'D',  },
		{ "Dk", PP_U, PP_UNKNOWN,                 0, 'D',  },
		{ "Dn", PP_U, PP_UNKNOWN,                 0, 'D',  },
		{ "Dp", PP_U, PP_UNKNOWN,                 0, 'D',  },
		{ "Dt", PP_U, PP_UNKNOWN,                 0, 'D',  },
		{ "Du", PP_U, PP_UNKNOWN,                 0, 'D',  },
		{ "D",  PP_U, PP_UNKNOWN,                 0, 'D',  },
		{ "Fg", PP_U, PP_UNKNOWN,                 0, 'F',  },
		{ "Fk", PP_U, PP_UNKNOWN,                 0, 'F',  },
		{ "Fn", PP_U, PP_UNKNOWN,                 0, 'F',  },
		{ "Fp", PP_U, PP_UNKNOWN,                 0, 'F',  },
		{ "Fs", PP_U, PP_UNKNOWN,                 0, 'F',  },
		{ "Fu", PP_U, PP_UNKNOWN,                 0, 'F',  },
		{ "F",  PP_U, PP_UNKNOWN,                 0, 'F',  },
		{ "Gn", PP_U, PP_UNKNOWN,                 0, 'G',  },
		{ "G",  PP_U, PP_UNKNOWN,                 0, 'G',  },
		{ "I",  PP_U, PP_ROW_COUNTER,             1, '\0', },
		{ "Ln", PP_U, PP_UNKNOWN,                 0, 'L',  },
		{ "L",  PP_U, PP_UNKNOWN,                 0, 'L',  },
		{ "M",  PP_U, PP_PKG_MESSAGE,             1, '\0', },
		{ "N",  PP_U, PP_PKG_REPO_IDENT,          1, '\0', },
		{ "On", PP_U, PP_UNKNOWN,                 0, 'O',  },
		{ "Ov", PP_U, PP_UNKNOWN,                 0, 'O',  },
		{ "Od", PP_U, PP_UNKNOWN,                 0, 'O',  },
		{ "OD", PP_U, PP_UNKNOWN,                 0, 'O',  },
		{ "O",  PP_U, PP_UNKNOWN,                 0, 'O',  },
		{ "R",  PP_U, PP_PKG_REPO_PATH,           1, '\0', },
		{ "S",  PP_U, PP_UNKNOWN,                 0, 'S',  },
		{ "Un", PP_U, PP_PKG_USER_NAME,           2, '\0', },
		{ "U",  PP_U, PP_UNKNOWN,                 0, 'U',  },
		{ "V",  PP_U, PP_PKG_OLD_VERSION,         1, '\0', },
		{ "a",  PP_U, PP_PKG_AUTOREMOVE,          1, '\0', },
		{ "bn", PP_U, PP_UNKNOWN,                 0, 'b',  },
		{ "b",  PP_U, PP_UNKNOWN,                 0, 'b',  },
		{ "c",  PP_U, PP_PKG_COMMENT,             1, '\0', },
		{ "dk", PP_U, PP_UNKNOWN,                 0, 'd',  },
		{ "dn", PP_U, PP_UNKNOWN,                 0, 'd',  },
		{ "do", PP_U, PP_UNKNOWN,                 0, 'd',  },
		{ "dv", PP_U, PP_UNKNOWN,                 0, 'd',  },
		{ "d",  PP_U, PP_UNKNOWN,                 0, 'd',  },
		{ "e",  PP_U, PP_PKG_DESCRIPTION,         1, '\0', },
		{ "k",  PP_U, PP_PKG_LOCK_STATUS,         1, '\0', },
		{ "l",  PP_U, PP_PKG_LICENSE_LOGIC,       1, '\0', },
		{ "m",  PP_U, PP_PKG_MAINTAINER,          1, '\0', },
		{ "n",  PP_U, PP_PKG_NAME,                1, '\0', },
		{ "o",  PP_U, PP_PKG_ORIGIN,              1, '\0', },
		{ "p",  PP_U, PP_PKG_PREFIX,              1, '\0', },
		{ "q",  PP_U, PP_PKG_ARCHITECTURE,        1, '\0', },
		{ "rk", PP_U, PP_UNKNOWN,                 0, 'r',  },
		{ "rn", PP_U, PP_UNKNOWN,                 0, 'r',  },
		{ "ro", PP_U, PP_UNKNOWN,                 0, 'r',  },
		{ "rv", PP_U, PP_UNKNOWN,                 0, 'r',  },
		{ "r",  PP_U, PP_UNKNOWN,                 0, 'r',  },
		{ "s",  PP_U, PP_PKG_FLATSIZE,            1, '\0', },
		{ "t",  PP_U, PP_PKG_INSTALL_TIMESTAMP,   1, '\0', },
		{ "u",  PP_U, PP_PKG_CHECKSUM,            1, '\0', },
		{ "v",  PP_U, PP_PKG_VERSION,             1, '\0', },
		{ "w",  PP_U, PP_PKG_HOME_PAGE,           1, '\0', },
		{ "x",  PP_U, PP_PKG_PKGSIZE,             1, '\0', },
		{ "z",  PP_U, PP_PKG_SHORT_CHECKSUM,      1, '\0', },
		{ "%",  PP_U, PP_LITERAL_PERCENT,         1, '\0', },
		{ "Z",  PP_U, PP_UNKNOWN,                 0, 'Z',  },

		{ "Bn", PP_b, PP_UNKNOWN,                 0, 'B', },
		{ "B",  PP_b, PP_UNKNOWN,                 0, 'B',  },
		{ "Cn", PP_b, PP_UNKNOWN,                 0, 'C',  },
		{ "C",  PP_b, PP_UNKNOWN,                 0, 'C',  },
		{ "Dg", PP_b, PP_UNKNOWN,                 0, 'D',  },
		{ "Dk", PP_b, PP_UNKNOWN,                 0, 'D',  },
		{ "Dn", PP_b, PP_UNKNOWN,                 0, 'D',  },
		{ "Dp", PP_b, PP_UNKNOWN,                 0, 'D',  },
		{ "Dt", PP_b, PP_UNKNOWN,                 0, 'D',  },
		{ "Du", PP_b, PP_UNKNOWN,                 0, 'D',  },
		{ "D",  PP_b, PP_UNKNOWN,                 0, 'D',  },
		{ "Fg", PP_b, PP_UNKNOWN,                 0, 'F',  },
		{ "Fk", PP_b, PP_UNKNOWN,                 0, 'F',  },
		{ "Fn", PP_b, PP_UNKNOWN,                 0, 'F',  },
		{ "Fp", PP_b, PP_UNKNOWN,                 0, 'F',  },
		{ "Fs", PP_b, PP_UNKNOWN,                 0, 'F',  },
		{ "Fu", PP_b, PP_UNKNOWN,                 0, 'F',  },
		{ "F",  PP_b, PP_UNKNOWN,                 0, 'F',  },
		{ "Gn", PP_b, PP_UNKNOWN,                 0, 'G',  },
		{ "G",  PP_b, PP_UNKNOWN,                 0, 'G',  },
		{ "I",  PP_b, PP_ROW_COUNTER,             1, '\0', },
		{ "Ln", PP_b, PP_UNKNOWN,                 0, 'L',  },
		{ "L",  PP_b, PP_UNKNOWN,                 0, 'L',  },
		{ "M",  PP_b, PP_PKG_MESSAGE,             1, '\0', },
		{ "N",  PP_b, PP_PKG_REPO_IDENT,          1, '\0', },
		{ "On", PP_b, PP_UNKNOWN,                 0, 'O',  },
		{ "Ov", PP_b, PP_UNKNOWN,                 0, 'O',  },
		{ "Od", PP_b, PP_UNKNOWN,                 0, 'O',  },
		{ "OD", PP_b, PP_UNKNOWN,                 0, 'O',  },
		{ "O",  PP_b, PP_UNKNOWN,                 0, 'O',  },
		{ "R",  PP_b, PP_PKG_REPO_PATH,           1, '\0', },
		{ "S",  PP_b, PP_UNKNOWN,                 0, 'S',  },
		{ "Un", PP_b, PP_UNKNOWN,                 0, 'U',  },
		{ "U",  PP_b, PP_UNKNOWN,                 0, 'U',  },
		{ "V",  PP_b, PP_PKG_OLD_VERSION,         1, '\0', },
		{ "a",  PP_b, PP_PKG_AUTOREMOVE,          1, '\0', },
		{ "bn", PP_b, PP_PKG_SHLIB_PROVIDED_NAME, 2, '\0', },
		{ "b",  PP_b, PP_UNKNOWN,                 0, 'b',  },
		{ "c",  PP_b, PP_PKG_COMMENT,             1, '\0', },
		{ "dk", PP_b, PP_UNKNOWN,                 0, 'd',  },
		{ "dn", PP_b, PP_UNKNOWN,                 0, 'd',  },
		{ "do", PP_b, PP_UNKNOWN,                 0, 'd',  },
		{ "dv", PP_b, PP_UNKNOWN,                 0, 'd',  },
		{ "d",  PP_b, PP_UNKNOWN,                 0, 'd',  },
		{ "e",  PP_b, PP_PKG_DESCRIPTION,         1, '\0', },
		{ "k",  PP_b, PP_PKG_LOCK_STATUS,         1, '\0', },
		{ "l",  PP_b, PP_PKG_LICENSE_LOGIC,       1, '\0', },
		{ "m",  PP_b, PP_PKG_MAINTAINER,          1, '\0', },
		{ "n",  PP_b, PP_PKG_NAME,                1, '\0', },
		{ "o",  PP_b, PP_PKG_ORIGIN,              1, '\0', },
		{ "p",  PP_b, PP_PKG_PREFIX,              1, '\0', },
		{ "q",  PP_b, PP_PKG_ARCHITECTURE,        1, '\0', },
		{ "rk", PP_b, PP_UNKNOWN,                 0, 'r',  },
		{ "rn", PP_b, PP_UNKNOWN,                 0, 'r',  },
		{ "ro", PP_b, PP_UNKNOWN,                 0, 'r',  },
		{ "rv", PP_b, PP_UNKNOWN,                 0, 'r',  },
		{ "r",  PP_b, PP_UNKNOWN,                 0, 'r',  },
		{ "s",  PP_b, PP_PKG_FLATSIZE,            1, '\0', },
		{ "t",  PP_b, PP_PKG_INSTALL_TIMESTAMP,   1, '\0', },
		{ "u",  PP_b, PP_PKG_CHECKSUM,            1, '\0', },
		{ "v",  PP_b, PP_PKG_VERSION,             1, '\0', },
		{ "w",  PP_b, PP_PKG_HOME_PAGE,           1, '\0', },
		{ "x",  PP_b, PP_PKG_PKGSIZE,             1, '\0', },
		{ "z",  PP_b, PP_PKG_SHORT_CHECKSUM,      1, '\0', },
		{ "%",  PP_b, PP_LITERAL_PERCENT,         1, '\0', },
		{ "Z",  PP_b, PP_UNKNOWN,                 0, 'Z',  },

		{ "Bn", PP_d, PP_UNKNOWN,                 0, 'B',  },
		{ "B",  PP_d, PP_UNKNOWN,                 0, 'B',  },
		{ "Cn", PP_d, PP_UNKNOWN,                 0, 'C',  },
		{ "C",  PP_d, PP_UNKNOWN,                 0, 'C',  },
		{ "Dg", PP_d, PP_UNKNOWN,                 0, 'D',  },
		{ "Dk", PP_d, PP_UNKNOWN,                 0, 'D',  },
		{ "Dn", PP_d, PP_UNKNOWN,                 0, 'D',  },
		{ "Dp", PP_d, PP_UNKNOWN,                 0, 'D',  },
		{ "Dt", PP_d, PP_UNKNOWN,                 0, 'D',  },
		{ "Du", PP_d, PP_UNKNOWN,                 0, 'D',  },
		{ "D",  PP_d, PP_UNKNOWN,                 0, 'D',  },
		{ "Fg", PP_d, PP_UNKNOWN,                 0, 'F',  },
		{ "Fk", PP_d, PP_UNKNOWN,                 0, 'F',  },
		{ "Fn", PP_d, PP_UNKNOWN,                 0, 'F',  },
		{ "Fp", PP_d, PP_UNKNOWN,                 0, 'F',  },
		{ "Fs", PP_d, PP_UNKNOWN,                 0, 'F',  },
		{ "Fu", PP_d, PP_UNKNOWN,                 0, 'F',  },
		{ "F",  PP_d, PP_UNKNOWN,                 0, 'F',  },
		{ "Gn", PP_d, PP_UNKNOWN,                 0, 'G',  },
		{ "G",  PP_d, PP_UNKNOWN,                 0, 'G',  },
		{ "I",  PP_d, PP_ROW_COUNTER,             1, '\0', },
		{ "Ln", PP_d, PP_UNKNOWN,                 0, 'L',  },
		{ "L",  PP_d, PP_UNKNOWN,                 0, 'L',  },
		{ "M",  PP_d, PP_PKG_MESSAGE,             1, '\0', },
		{ "N",  PP_d, PP_PKG_REPO_IDENT,          1, '\0', },
		{ "On", PP_d, PP_UNKNOWN,                 0, 'O',  },
		{ "Ov", PP_d, PP_UNKNOWN,                 0, 'O',  },
		{ "Od", PP_d, PP_UNKNOWN,                 0, 'O',  },
		{ "OD", PP_d, PP_UNKNOWN,                 0, 'O',  },
		{ "O",  PP_d, PP_UNKNOWN,                 0, 'O',  },
		{ "R",  PP_d, PP_PKG_REPO_PATH,           1, '\0', },
		{ "S",  PP_d, PP_UNKNOWN,                 0, 'S',  },
		{ "Un", PP_d, PP_UNKNOWN,                 0, 'U',  },
		{ "U",  PP_d, PP_UNKNOWN,                 0, 'U',  },
		{ "V",  PP_d, PP_PKG_OLD_VERSION,         1, '\0', },
		{ "a",  PP_d, PP_PKG_AUTOREMOVE,          1, '\0', },
		{ "bn", PP_d, PP_UNKNOWN,                 0, 'b',  },
		{ "b",  PP_d, PP_UNKNOWN,                 0, 'b',  },
		{ "c",  PP_d, PP_PKG_COMMENT,             1, '\0', },
		{ "dk", PP_d, PP_PKG_DEPENDENCY_LOCK,     2, '\0', },
		{ "dn", PP_d, PP_PKG_DEPENDENCY_NAME,     2, '\0', },
		{ "do", PP_d, PP_PKG_DEPENDENCY_ORIGIN,   2, '\0', },
		{ "dv", PP_d, PP_PKG_DEPENDENCY_VERSION,  2, '\0', },
		{ "d",  PP_d, PP_UNKNOWN,                 0, 'd',  },
		{ "e",  PP_d, PP_PKG_DESCRIPTION,         1, '\0', },
		{ "k",  PP_d, PP_PKG_LOCK_STATUS,         1, '\0', },
		{ "l",  PP_d, PP_PKG_LICENSE_LOGIC,       1, '\0', },
		{ "m",  PP_d, PP_PKG_MAINTAINER,          1, '\0', },
		{ "n",  PP_d, PP_PKG_NAME,                1, '\0', },
		{ "o",  PP_d, PP_PKG_ORIGIN,              1, '\0', },
		{ "p",  PP_d, PP_PKG_PREFIX,              1, '\0', },
		{ "q",  PP_d, PP_PKG_ARCHITECTURE,        1, '\0', },
		{ "rk", PP_d, PP_UNKNOWN,                 0, 'r',  },
		{ "rn", PP_d, PP_UNKNOWN,                 0, 'r',  },
		{ "ro", PP_d, PP_UNKNOWN,                 0, 'r',  },
		{ "rv", PP_d, PP_UNKNOWN,                 0, 'r',  },
		{ "r",  PP_d, PP_UNKNOWN,                 0, 'r',  },
		{ "s",  PP_d, PP_PKG_FLATSIZE,            1, '\0', },
		{ "t",  PP_d, PP_PKG_INSTALL_TIMESTAMP,   1, '\0', },
		{ "u",  PP_d, PP_PKG_CHECKSUM,            1, '\0', },
		{ "v",  PP_d, PP_PKG_VERSION,             1, '\0', },
		{ "w",  PP_d, PP_PKG_HOME_PAGE,           1, '\0', },
		{ "x",  PP_d, PP_PKG_PKGSIZE,             1, '\0', },
		{ "z",  PP_d, PP_PKG_SHORT_CHECKSUM,      1, '\0', },
		{ "%",  PP_d, PP_LITERAL_PERCENT,         1, '\0', },
		{ "Z",  PP_d, PP_UNKNOWN,                 0, 'Z',  },

		{ "Bn", PP_r, PP_UNKNOWN,                 0, 'B',  },
		{ "B",  PP_r, PP_UNKNOWN,                 0, 'B',  },
		{ "Cn", PP_r, PP_UNKNOWN,                 0, 'C',  },
		{ "C",  PP_r, PP_UNKNOWN,                 0, 'C',  },
		{ "Dg", PP_r, PP_UNKNOWN,                 0, 'D',  },
		{ "Dk", PP_r, PP_UNKNOWN,                 0, 'D',  },
		{ "Dn", PP_r, PP_UNKNOWN,                 0, 'D',  },
		{ "Dp", PP_r, PP_UNKNOWN,                 0, 'D',  },
		{ "Dt", PP_r, PP_UNKNOWN,                 0, 'D',  },
		{ "Du", PP_r, PP_UNKNOWN,                 0, 'D',  },
		{ "D",  PP_r, PP_UNKNOWN,                 0, 'D',  },
		{ "Fg", PP_r, PP_UNKNOWN,                 0, 'F',  },
		{ "Fk", PP_r, PP_UNKNOWN,                 0, 'F',  },
		{ "Fn", PP_r, PP_UNKNOWN,                 0, 'F',  },
		{ "Fp", PP_r, PP_UNKNOWN,                 0, 'F',  },
		{ "Fs", PP_r, PP_UNKNOWN,                 0, 'F',  },
		{ "Fu", PP_r, PP_UNKNOWN,                 0, 'F',  },
		{ "F",  PP_r, PP_UNKNOWN,                 0, 'F',  },
		{ "Gn", PP_r, PP_UNKNOWN,                 0, 'G',  },
		{ "G",  PP_r, PP_UNKNOWN,                 0, 'G',  },
		{ "I",  PP_r, PP_ROW_COUNTER,             1, '\0', },
		{ "Ln", PP_r, PP_UNKNOWN,                 0, 'L',  },
		{ "L",  PP_r, PP_UNKNOWN,                 0, 'L',  },
		{ "M",  PP_r, PP_PKG_MESSAGE,             1, '\0', },
		{ "N",  PP_r, PP_PKG_REPO_IDENT,          1, '\0', },
		{ "On", PP_r, PP_UNKNOWN,                 0, 'O',  },
		{ "Ov", PP_r, PP_UNKNOWN,                 0, 'O',  },
		{ "Od", PP_r, PP_UNKNOWN,                 0, 'O',  },
		{ "OD", PP_r, PP_UNKNOWN,                 0, 'O',  },
		{ "O",  PP_r, PP_UNKNOWN,                 0, 'O',  },
		{ "R",  PP_r, PP_PKG_REPO_PATH,           1, '\0', },
		{ "S",  PP_r, PP_UNKNOWN,                 0, 'S',  },
		{ "Un", PP_r, PP_UNKNOWN,                 0, 'U',  },
		{ "U",  PP_r, PP_UNKNOWN,                 0, 'U',  },
		{ "V",  PP_r, PP_PKG_OLD_VERSION,         1, '\0', },
		{ "a",  PP_r, PP_PKG_AUTOREMOVE,          1, '\0', },
		{ "bn", PP_r, PP_UNKNOWN,                 0, 'b',  },
		{ "b",  PP_r, PP_UNKNOWN,                 0, 'b',  },
		{ "c",  PP_r, PP_PKG_COMMENT,             1, '\0', },
		{ "dk", PP_r, PP_UNKNOWN,                 0, 'd',  },
		{ "dn", PP_r, PP_UNKNOWN,                 0, 'd',  },
		{ "do", PP_r, PP_UNKNOWN,                 0, 'd',  },
		{ "dv", PP_r, PP_UNKNOWN,                 0, 'd',  },
		{ "d",  PP_r, PP_UNKNOWN,                 0, 'd',  },
		{ "e",  PP_r, PP_PKG_DESCRIPTION,         1, '\0', },
		{ "k",  PP_r, PP_PKG_LOCK_STATUS,         1, '\0', },
		{ "l",  PP_r, PP_PKG_LICENSE_LOGIC,       1, '\0', },
		{ "m",  PP_r, PP_PKG_MAINTAINER,          1, '\0', },
		{ "n",  PP_r, PP_PKG_NAME,                1, '\0', },
		{ "o",  PP_r, PP_PKG_ORIGIN,              1, '\0', },
		{ "p",  PP_r, PP_PKG_PREFIX,              1, '\0', },
		{ "q",  PP_r, PP_PKG_ARCHITECTURE,        1, '\0', },
		{ "rk", PP_r, PP_PKG_REQUIREMENT_LOCK,    2, '\0', },
		{ "rn", PP_r, PP_PKG_REQUIREMENT_NAME,    2, '\0', },
		{ "ro", PP_r, PP_PKG_REQUIREMENT_ORIGIN,  2, '\0', },
		{ "rv", PP_r, PP_PKG_REQUIREMENT_VERSION, 2, '\0', },
		{ "r",  PP_r, PP_UNKNOWN,                 0, 'r',  },
		{ "s",  PP_r, PP_PKG_FLATSIZE,            1, '\0', },
		{ "t",  PP_r, PP_PKG_INSTALL_TIMESTAMP,   1, '\0', },
		{ "u",  PP_r, PP_PKG_CHECKSUM,            1, '\0', },
		{ "v",  PP_r, PP_PKG_VERSION,             1, '\0', },
		{ "w",  PP_r, PP_PKG_HOME_PAGE,           1, '\0', },
		{ "x",  PP_r, PP_PKG_PKGSIZE,             1, '\0', },
		{ "z",  PP_r, PP_PKG_SHORT_CHECKSUM,      1, '\0', },
		{ "%",  PP_r, PP_LITERAL_PERCENT,         1, '\0', },
		{ "Z",  PP_r, PP_UNKNOWN,                 0, 'Z',  },

		{ NULL, 0,    0,                          0, '\0', },
	};

	p = new_percent_esc();

	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; fc_test_vals[i].in != NULL; i++) {
		p->width = 0;
		f = format_code(fc_test_vals[i].in, fc_test_vals[i].context, p);

		ATF_CHECK_EQ_MSG(p->fmt_code, fc_test_vals[i].fmt_code,
				    "(test %d: %d != %d)", i,
				    p->fmt_code, fc_test_vals[i].fmt_code);
		ATF_CHECK_EQ_MSG(f - fc_test_vals[i].in,
				 fc_test_vals[i].fend_offset,
				 "(test %d)", i);
		ATF_CHECK_EQ_MSG(*f, fc_test_vals[i].fend_val,
				 "(test %d)", i);
	}

	free_percent_esc(p);
}

ATF_TC(format_trailer);
ATF_TC_HEAD(format_trailer, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Testing format_trailer() format parsing routine");
}
ATF_TC_BODY(format_trailer, tc)
{
	struct percent_esc	*p;
	const char		*f;
	int		 	i;

	struct ft_test_vals {
		const char *in;
		const char *item;
		const char *sep;
		ptrdiff_t   fend_offset; /* Where f is left pointing */
		char	    fend_val; /* expected first char in fend */
	} ft_test_vals[] = {
		{ "%{aaaaaaaa", "",   "",    0, '%',  },
		{ "%{bb%|cccc", "",   "",    0, '%',  },
		{ "ddd%|eee%}", "",   "",    0, 'd',  },
		{ "%{ff%|gg%}", "ff", "gg", 10, '\0', },
		{ "%{hh%}",     "hh", "",    6, '\0', },
		{ "%{%|iii%}",  "",   "iii", 9, '\0', },

		{ NULL,         NULL, NULL,  0, '\0', },
	};

	p = new_percent_esc();

	ATF_REQUIRE_EQ(p != NULL, true);

	for (i = 0; ft_test_vals[i].in != NULL; i++) {
		clear_percent_esc(p);

		f = format_trailer(ft_test_vals[i].in, p);
		fflush(p->item_fmt->fp);
		fflush(p->sep_fmt->fp);

		ATF_CHECK_STREQ_MSG(p->item_fmt->buf,
				    ft_test_vals[i].item,
				    "(test %d)", i);
		ATF_CHECK_STREQ_MSG(p->sep_fmt->buf,
				    ft_test_vals[i].sep,
				    "(test %d)", i);
		ATF_CHECK_EQ_MSG(f - ft_test_vals[i].in,
				 ft_test_vals[i].fend_offset,
				 "(test %d)", i);
		ATF_CHECK_EQ_MSG(*f, ft_test_vals[i].fend_val,
				 "(test %d)", i);
	}

	free_percent_esc(p);
}

ATF_TC(parse_format);
ATF_TC_HEAD(parse_format, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Testing parse_format() format parsing routine");
}
ATF_TC_BODY(parse_format, tc)
{
	struct percent_esc	*p;
	const char		*f;
	int		 	i;

	struct pf_test_vals {
		const char *in;
		unsigned context;

		unsigned flags;
		int width;
		fmt_code_t fmt_code;
		const char *item;
		const char *sep;
		ptrdiff_t   fend_offset; /* Where f is left pointing */
		char	    fend_val; /* expected first char in fend */
	} pf_test_vals[] = {
		{ "%n",    PP_PKG, 0,             0,  PP_PKG_NAME, "",   "",   2, '\0', },
		{ "%-20n", PP_PKG, PP_LEFT_ALIGN, 20, PP_PKG_NAME, "",   "",   5, '\0', },
		{ "%?B",   PP_PKG, PP_ALTERNATE_FORM1, 0, PP_PKG_SHLIBS_REQUIRED, "", "", 3, '\0', },
		{ "%#F",   PP_PKG, PP_ALTERNATE_FORM2, 0, PP_PKG_FILES,  "", "", 3, '\0', },

		{ "%L%{%Ln%| %l %}", PP_PKG, 0, 0, PP_PKG_LICENSES, "%Ln", " %l ", 15, '\0', },
		{ "%Ln",   PP_L,   0,             0,  PP_PKG_LICENSE_NAME,  "", "", 3, '\0', },
		{ "%l",    PP_L,   0,             0,  PP_PKG_LICENSE_LOGIC, "", "", 2, '\0', },

		{ "%Ln",   PP_PKG, 0,             0,  PP_PKG_LICENSE_NAME,  "", "", 3, '\0', },
		{ "%l",    PP_PKG, 0,             0,  PP_PKG_LICENSE_LOGIC, "", "", 2, '\0', },

		{ "%I",    PP_PKG, 0,             0,  PP_UNKNOWN,          "", "", 1, 'I', },

		{ "%^D",   PP_PKG, 0,             0,  PP_UNKNOWN,          "", "", 1, '^', },

		{ NULL,    0,      0,             0,  0,           NULL, NULL, 0, '\0', },
	};

	p = new_percent_esc();

	ATF_REQUIRE(p != NULL);

	for (i = 0; pf_test_vals[i].in != NULL; i++) {
		f = parse_format(pf_test_vals[i].in, pf_test_vals[i].context,
				 p);

		ATF_CHECK_EQ_MSG(p->flags, pf_test_vals[i].flags,
				    "(test %d)", i);
		ATF_CHECK_EQ_MSG(p->width, pf_test_vals[i].width,
				    "(test %d)", i);
		ATF_CHECK_EQ_MSG(p->fmt_code, pf_test_vals[i].fmt_code,
				    "(test %d)", i);

		fflush(p->item_fmt->fp);
		fflush(p->sep_fmt->fp);
		ATF_CHECK_STREQ_MSG(p->item_fmt->buf,
				    pf_test_vals[i].item,
				    "(test %d)", i);
		ATF_CHECK_STREQ_MSG(p->sep_fmt->buf,
				    pf_test_vals[i].sep,
				    "(test %d)", i);
		ATF_CHECK_EQ_MSG(f - pf_test_vals[i].in,
				 pf_test_vals[i].fend_offset,
				 "(test %d)", i);
		ATF_CHECK_EQ_MSG(*f, pf_test_vals[i].fend_val,
				 "(test %d)", i);

		p = clear_percent_esc(p);
	}

	free_percent_esc(p);
}



ATF_TP_ADD_TCS(tp)
{
	/* Output routines */
	ATF_TP_ADD_TC(tp, gen_format);
	ATF_TP_ADD_TC(tp, human_number);
	ATF_TP_ADD_TC(tp, string_val);
	ATF_TP_ADD_TC(tp, int_val);
	ATF_TP_ADD_TC(tp, bool_val);
	ATF_TP_ADD_TC(tp, mode_val);
	ATF_TP_ADD_TC(tp, liclog_val);
	ATF_TP_ADD_TC(tp, list_count);

	/* Format string parsing routines */
	ATF_TP_ADD_TC(tp, maybe_read_hex_byte);
	ATF_TP_ADD_TC(tp, read_oct_byte);
	ATF_TP_ADD_TC(tp, process_escape);

	ATF_TP_ADD_TC(tp, field_modifier);
	ATF_TP_ADD_TC(tp, field_width);
	ATF_TP_ADD_TC(tp, format_code);
	ATF_TP_ADD_TC(tp, format_trailer);
	ATF_TP_ADD_TC(tp, parse_format);


	return atf_no_error();
}
/*
 * That's All Folks!
 */
