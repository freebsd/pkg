#include <check.h>
#include <pkg.h>

START_TEST(pkg_null)
{
	struct pkg *p = NULL;
	fail_unless(pkg_get(p, PKG_NAME) ==NULL);
	fail_unless(pkg_set(p, PKG_NAME, "foobar") == EPKG_FATAL);

	fail_unless(pkg_new(&p) == EPKG_OK);
	fail_unless(pkg_set(p, PKG_NAME, NULL) == EPKG_FATAL);
}
END_TEST

TCase *tcase_pkg(void)
{
	TCase *tc = tcase_create("Pkg");
	tcase_add_test(tc, pkg_null);

	return (tc);
}
