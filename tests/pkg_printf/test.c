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
		unsigned    flags;
		const char *out;
	} gf_test_vals[] = {
		{ 0x00,	"%*x", },
		{ 0x01,	"%*x", },
		{ 0x02,	"%#*x", },
		{ 0x03,	"%#*x", },

		{ 0x04,	"%-*x", },
		{ 0x05,	"%-*x", },
		{ 0x06,	"%#-*x", },
		{ 0x07,	"%#-*x", },

		{ 0x08,	"%+*x", },
		{ 0x09,	"%+*x", },
		{ 0x0a,	"%#+*x", },
		{ 0x0b,	"%#+*x", },

		{ 0x0c,	"%-+*x", },
		{ 0x0d,	"%-+*x", },
		{ 0x0e,	"%#-+*x", },
		{ 0x0f,	"%#-+*x", },

		{ 0x10,	"% *x", },
		{ 0x11,	"% *x", },
		{ 0x12,	"%# *x", },
		{ 0x13,	"%# *x", },

		{ 0x14,	"%- *x", },
		{ 0x15,	"%- *x", },
		{ 0x16,	"%#- *x", },
		{ 0x17,	"%#- *x", },

		{ 0x18,	"%+*x", },
		{ 0x19,	"%+*x", },
		{ 0x1a,	"%#+*x", },
		{ 0x1b,	"%#+*x", },

		{ 0x1c,	"%-+*x", },
		{ 0x1d,	"%-+*x", },
		{ 0x1e,	"%#-+*x", },
		{ 0x1f,	"%#-+*x", },

		{ 0x20,	"%0*x", },
		{ 0x21,	"%0*x", },
		{ 0x22,	"%#0*x", },
		{ 0x23,	"%#0*x", },

		{ 0x24,	"%-*x", },
		{ 0x25,	"%-*x", },
		{ 0x26,	"%#-*x", },
		{ 0x27,	"%#-*x", },

		{ 0x28,	"%0+*x", },
		{ 0x29,	"%0+*x", },
		{ 0x2a,	"%#0+*x", },
		{ 0x2b,	"%#0+*x", },

		{ 0x2c,	"%-+*x", },
		{ 0x2d,	"%-+*x", },
		{ 0x2e,	"%#-+*x", },
		{ 0x2f,	"%#-+*x", },

		{ 0x30,	"%0 *x", },
		{ 0x31,	"%0 *x", },
		{ 0x32,	"%#0 *x", },
		{ 0x33,	"%#0 *x", },

		{ 0x34,	"%- *x", },
		{ 0x35,	"%- *x", },
		{ 0x36,	"%#- *x", },
		{ 0x37,	"%#- *x", },

		{ 0x38,	"%0+*x", },
		{ 0x39,	"%0+*x", },
		{ 0x3a,	"%#0+*x", },
		{ 0x3b,	"%#0+*x", },

		{ 0x3c,	"%-+*x", },
		{ 0x3d,	"%-+*x", },
		{ 0x3e,	"%#-+*x", },
		{ 0x3f,	"%#-+*x", },

		{ 0,    NULL, },
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

		{ 1234567, "1M",           1, 0, },
		{ 1234567, " 1M",          2, 0, },
		{ 1234567, "1.2M",         3, 0, },
		{ 1234567, "1.23M",        4, 0, },
		{ 1234567, " 1.23M",       5, 0, },
		{ 1234567, "  1.23M",      6, 0, },
		{ 1234567, "   1.23M",     7, 0, },
		{ 1234567, "    1.23M",    8, 0, },
		{ 1234567, "     1.23M",   9, 0, },

		{ 12345678, "12M",         1, 0, },
		{ 12345678, "12M",         2, 0, },
		{ 12345678, " 12M",        3, 0, },
		{ 12345678, "12.3M",       4, 0, },
		{ 12345678, " 12.3M",      5, 0, },
		{ 12345678, "  12.3M",     6, 0, },
		{ 12345678, "   12.3M",    7, 0, },
		{ 12345678, "    12.3M",   8, 0, },
		{ 12345678, "     12.3M",  9, 0, },

		{ 123456789, "123M",       1, 0, },
		{ 123456789, "123M",       2, 0, },
		{ 123456789, "123M",       3, 0, },
		{ 123456789, " 123M",      4, 0, },
		{ 123456789, "  123M",     5, 0, },
		{ 123456789, "   123M",    6, 0, },
		{ 123456789, "    123M",   7, 0, },
		{ 123456789, "     123M",  8, 0, },
		{ 123456789, "      123M", 9, 0, },

		{  1234567, "1.23M",   0, PP_ALTERNATE_FORM1, },
		{  1234567, "1.18Mi",  0, PP_ALTERNATE_FORM2, },
		{  1234567, "1.23  M", 6, PP_LEFT_ALIGN, },
		{  1234567, "+1.23M",  0, PP_EXPLICIT_PLUS, },
		{ -1234567, "-1.23M",  0, PP_EXPLICIT_PLUS, },
		{  1234567, " 1.23M",  0, PP_SPACE_FOR_PLUS, },
		{ -1234567, "-1.23M",  0, PP_SPACE_FOR_PLUS, },
		{  1234567, "001.23M", 6, PP_ZERO_PAD, },
		{  1234567, "1.23M",   0, PP_THOUSANDS_SEP, },
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
