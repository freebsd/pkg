#include <check.h>
#include <pkg.h>

START_TEST(pkg_null)
{
	struct pkg *p = NULL;

	fail_unless(pkg_get(p, PKG_NAME) ==NULL);
	fail_unless(pkg_set(p, PKG_NAME, "foobar") == EPKG_FATAL);
	fail_unless(pkg_type(p) == PKG_NONE);
	fail_unless(pkg_set_from_file(p, PKG_NAME, "path") == EPKG_FATAL);
	fail_unless(pkg_scripts(p, NULL) == EPKG_FATAL);
	fail_unless(pkg_deps(p, NULL) == EPKG_FATAL);
	fail_unless(pkg_options(p, NULL) == EPKG_FATAL);
	fail_unless(pkg_rdeps(p, NULL) == EPKG_FATAL);
	fail_unless(pkg_files(p, NULL) == EPKG_FATAL);
	fail_unless(pkg_conflicts(p, NULL) == EPKG_FATAL);
	fail_unless(pkg_addscript_file(p, "./bla") == EPKG_FATAL);
	fail_unless(pkg_addoption(p, "foo", "bar") == EPKG_FATAL);
	fail_unless(pkg_adddep(p, "foo", "foo/bar", "123") == EPKG_FATAL);

	fail_unless(pkg_new(&p, PKG_FILE) == EPKG_OK);
	fail_unless(pkg_set(p, PKG_NAME, NULL) == EPKG_FATAL);
	fail_unless(pkg_set_from_file(p, PKG_NAME, NULL) == EPKG_FATAL);
	fail_unless(pkg_open(&p, NULL) == EPKG_FATAL);
	fail_unless(pkg_open(&p, "test") == EPKG_FATAL);
	fail_unless(pkg_addscript_file(p, NULL) == EPKG_FATAL);
	fail_unless(pkg_addscript_file(p, "./bla") == EPKG_FATAL);
	fail_unless(pkg_addoption(p, NULL, "bar") == EPKG_FATAL);
	fail_unless(pkg_addoption(p, "foo", NULL) == EPKG_FATAL);
	fail_unless(pkg_adddep(p, NULL, "foo/bar", "123") == EPKG_FATAL);
	fail_unless(pkg_adddep(p, "foo", NULL, "123") == EPKG_FATAL);
	/* currently disabled until we get code to test origin format and name
	 * format*/
/*	fail_unless(pkg_adddep(p, "foo", "foobar", NULL) == EPKG_NOT_ORIGIN);
	fail_unless(pkg_adddep(p, "fo/o", "foo/bar", NULL) == EPKG_NOT_NAME);*/

}
END_TEST

TCase *tcase_pkg(void)
{
	TCase *tc = tcase_create("Pkg");
	tcase_add_test(tc, pkg_null);

	return (tc);
}
