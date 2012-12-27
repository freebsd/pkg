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
			  "Generate printf format code of output");
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


ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, gen_format);
	ATF_TP_ADD_TC(tp, human_number);

	return atf_no_error();
}
