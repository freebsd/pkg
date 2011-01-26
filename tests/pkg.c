#include <check.h>
#include <pkg.h>

START_TEST(pkg_null)
{
	struct pkg *p = NULL;

	fail_unless(pkg_get(p, PKG_NAME) ==NULL);
	fail_unless(pkg_set(p, PKG_NAME, "foobar") == EPKG_NULL_PKG);
	fail_unless(pkg_type(p) == PKG_NONE);
	fail_unless(pkg_set_from_file(p, PKG_NAME, "path") == EPKG_NULL_PKG);
	fail_unless(pkg_scripts(p) == NULL);
	fail_unless(pkg_deps(p) == NULL);
	fail_unless(pkg_execs(p) == NULL);
	fail_unless(pkg_options(p) == NULL);
	fail_unless(pkg_rdeps(p) == NULL);
	fail_unless(pkg_files(p) == NULL);
	fail_unless(pkg_conflicts(p) == NULL);
	fail_unless(pkg_addscript(p, "./bla") == EPKG_NULL_PKG);
	fail_unless(pkg_addoption(p, "foo", "bar") == EPKG_NULL_PKG);
	fail_unless(pkg_addexec(p, "bal", PKG_EXEC) == EPKG_NULL_PKG);
	fail_unless(pkg_adddep(p, "foo", "foo/bar", "123") == EPKG_NULL_PKG);

	fail_unless(pkg_new(&p) == EPKG_OK);
	fail_unless(pkg_set(p, PKG_NAME, NULL) == EPKG_NULL_VALUE);
	fail_unless(pkg_set_from_file(p, PKG_NAME, NULL) == EPKG_NULL_VALUE);
	fail_unless(pkg_open(NULL, &p, 0) == EPKG_NULL_VALUE);
	fail_unless(pkg_open("test", &p, 0) == EPKG_ERROR_ARCHIVE);
	fail_unless(pkg_addscript(p, NULL) == EPKG_NULL_VALUE);
	fail_unless(pkg_addscript(p, "./bla") == EPKG_ERROR_OPEN);
	fail_unless(pkg_addoption(p, NULL, "bar") == EPKG_NULL_VALUE);
	fail_unless(pkg_addoption(p, "foo", NULL) == EPKG_NULL_VALUE);
	fail_unless(pkg_addexec(p, NULL, PKG_EXEC) == EPKG_NULL_VALUE);
	fail_unless(pkg_adddep(p, NULL, "foo/bar", "123") == EPKG_NULL_VALUE);
	fail_unless(pkg_adddep(p, "foo", NULL, "123") == EPKG_NULL_VALUE);
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
