/*
 * Copyright (c) 2015, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *	 * Redistributions of source code must retain the above copyright
 *	   notice, this list of conditions and the following disclaimer.
 *	 * Redistributions in binary form must reproduce the above copyright
 *	   notice, this list of conditions and the following disclaimer in the
 *	   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <atf-c.h>
#include <err.h>
#include <unistd.h>
#include <pkg.h>
#include <private/pkg.h>
#include <private/pkg_deps.h>

ATF_TC(check_parsing);
ATF_TC(check_sql);
ATF_TC(check_op_parsing);

ATF_TC_HEAD(check_parsing, tc)
{
	atf_tc_set_md_var(tc, "descr", "testing parsing of deps formula");
}

ATF_TC_BODY(check_parsing, tc)
{
	struct pkg_dep_formula *f;
	const char *cases[] = {
		"name",
		"name = 1.0",
		"name >= 1.0,1",
		"name1, name2",
		"name1 | name2, name3",
		"name1 = 1.0 | name2 != 1.0, name3 > 1.0 < 2.0 != 1.5",
		"name1 = 1.0 | name2 != 1.0, name3 > 1.0 < 2.0 != 1.5, name4 +opt1 -opt2"
	};
	char *r;
	int i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i ++) {
		f = pkg_deps_parse_formula(cases[i]);
		ATF_REQUIRE(f != NULL);
		r = pkg_deps_formula_tostring(f);
		ATF_REQUIRE_STREQ(r, cases[i]);
		free(r);
		pkg_deps_formula_free(f);
	}
}

ATF_TC_HEAD(check_sql, tc)
{
	atf_tc_set_md_var(tc, "descr", "testing creating sql queries from formulas");
}

ATF_TC_BODY(check_sql, tc)
{
	struct pkg_dep_formula *f;
	const char *cases[] = {
		"name", "(name='name')",
		"name = 1.0", "(name='name' AND vercmp('=',version,'1.0'))",
		"name >= 1.0,1", "(name='name' AND vercmp('>=',version,'1.0,1'))",
		"name1 | name2", "(name='name1') OR (name='name2')",
		"name1 = 1.0 | name2 != 1.0", "(name='name1' AND vercmp('=',version,'1.0')) OR (name='name2' AND vercmp('!=',version,'1.0'))"
	};
	char *r;
	int i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]) / 2; i ++) {
		f = pkg_deps_parse_formula(cases[i * 2]);
		ATF_REQUIRE(f != NULL);
		r = pkg_deps_formula_tosql(f->items);
		ATF_REQUIRE_STREQ(r, cases[i * 2 + 1]);
		free(r);
		pkg_deps_formula_free(f);
	}
}

ATF_TC_HEAD(check_op_parsing, tc)
{
	atf_tc_set_md_var(tc, "descr", "testing parsing operands");
}
ATF_TC_BODY(check_op_parsing, tc)
{
	struct cases {
		const char *val;
		int expect;
	} cases[] = {
		{ "=", VERSION_EQ },
		{ "==", VERSION_EQ },
		{ ">=", VERSION_GE },
		{ ">", VERSION_GT },
		{ "<=", VERSION_LE },
		{ "<", VERSION_LT },
		{ "!", VERSION_NOT },
		{ "!=", VERSION_NOT },
		{ "*", VERSION_ANY },
		{ NULL, VERSION_ANY },
		{ "=>", VERSION_ANY },
	};

	for (int i = 0; i < sizeof(cases) / sizeof(cases[0]); i ++) {
		ATF_REQUIRE_EQ(pkg_deps_string_toop(cases[i].val), cases[i].expect);
	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, check_parsing);
	ATF_TP_ADD_TC(tp, check_sql);
	ATF_TP_ADD_TC(tp, check_op_parsing);
	return (atf_no_error());
}
