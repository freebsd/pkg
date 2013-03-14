#include <atf-c.h>
#include "tests.h"

ATF_TC(manifest);
ATF_TC_HEAD(manifest, tc)
{
    atf_tc_set_md_var(tc, "descr", "Testing manifest loading...");
}
ATF_TC_BODY(manifest, tc)
{
    test_manifest();
}

ATF_TC(pkg);
ATF_TC_HEAD(pkg, tc)
{
    atf_tc_set_md_var(tc, "descr", "Testing pkg interface...");
}

ATF_TC_BODY(pkg, tc)
{
    test_pkg();
}
ATF_TP_ADD_TCS(tp)
{
    ATF_TP_ADD_TC(tp, manifest);
    ATF_TP_ADD_TC(tp, pkg);
    return atf_no_error();
}
