#include <atf-c.h>
#include "tests.h"

ATF_TC_WITHOUT_HEAD(manifest);
ATF_TC_WITHOUT_HEAD(pkg);

ATF_TC_BODY(manifest, tc)
{
    test_manifest();
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
