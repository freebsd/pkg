#include <atf-c.h>
#include <private/pkg.h>

ATF_TC_WITHOUT_HEAD(pkgs_insert_sorted);
ATF_TC_BODY(pkgs_insert_sorted, tc)
{
	pkgs_t pkgs = vec_init();

	ATF_REQUIRE_EQ_MSG(pkgs.d, NULL, "vec_init failed");
	ATF_REQUIRE_EQ_MSG(pkgs.cap, 0, "vec_init failed");
	ATF_REQUIRE_EQ_MSG(pkgs.len, 0, "vec_init failed");

	struct pkg *p;

	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_FILE));
	ATF_REQUIRE(p != NULL);
	p->name = xstrdup("name1");
	ATF_REQUIRE_EQ(pkgs_insert_sorted(&pkgs, p), NULL);
	ATF_REQUIRE_EQ_MSG(pkgs.len, 1, "Fail to insert");

	p = NULL;
	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_FILE));
	p->name = xstrdup("name1");
	ATF_REQUIRE_MSG(pkgs_insert_sorted(&pkgs, p) !=  NULL, "Collision not detected");

	free(p->name);
	p->name = xstrdup("aname1");

	ATF_REQUIRE_EQ(pkgs_insert_sorted(&pkgs, p), NULL);
	ATF_REQUIRE_EQ_MSG(pkgs.len, 2, "Fail to insert");

	ATF_REQUIRE_STREQ(pkgs.d[0]->name, "aname1");
	ATF_REQUIRE_STREQ(pkgs.d[1]->name, "name1");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, pkgs_insert_sorted);

	return (atf_no_error());
}
