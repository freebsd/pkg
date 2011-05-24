#include <check.h>
#include <pkg.h>
#include <string.h>

#include "tests.h"

char manifest[] = ""
	"@pkg_format_version 0.9\n"
	"@name foobar\n"
	"@version 0.3\n"
	"@origin foo/bar\n"
	"@comment A dummy manifest\n"
	"@arch amd64\n"
	"@osversion 800500\n"
	"@www http://www.foobar.com\n"
	"@maintainer test@pkgng.lan\n"
	"@flatsize 10000\n"
	"@dep depfoo dep/foo 1.2\n"
	"@dep depbar dep/bar 3.4\n"
	"@hello world\n" /* unknown keyword should not be a problem */
	"@conflict foo-*\n"
	"@conflict bar-*\n"
	"@option foo true\n"
	"@option bar false\n"
	"@file /usr/local/bin/foo "
		"01ba4719c80b6fe911b091a7c05124b64eeece964e09c058ef8f9805daca546b\n";

/* Name empty */
char wrong_manifest1[] = ""
	"@pkg_format_version 0.9\n"
	"@name\n"
	"@version 0.3\n"
	"@origin foo/bar\n"
	"@comment A dummy manifest\n"
	"@arch amd64\n"
	"@osversion 800500\n"
	"@www http://www.foobar.com\n"
	"@maintainer test@pkgng.lan\n"
	"@dep depfoo dep/foo 1.2\n"
	"@dep depbar dep/bar 3.4\n"
	"@conflict foo-*\n"
	"@conflict bar-*\n"
	"@option foo true\n"
	"@option bar false\n";

/* bad dependency line */
char wrong_manifest2[] = ""
	"@pkg_format_version 0.9\n"
	"@name foobar\n"
	"@version 0.3\n"
	"@origin foo/bar\n"
	"@comment A dummy manifest\n"
	"@arch amd64\n"
	"@osversion 800500\n"
	"@www http://www.foobar.com\n"
	"@maintainer test@pkgng.lan\n"
	"@dep depfoo\n"
	"@dep depbar dep/bar 3.4\n"
	"@conflict foo-*\n"
	"@conflict bar-*\n"
	"@option foo true\n"
	"@option bar false\n";

/* bad conflict line */
char wrong_manifest3[] = ""
	"@pkg_format_version 0.9\n"
	"@name foobar\n"
	"@version 0.3\n"
	"@origin foo/bar\n"
	"@comment A dummy manifest\n"
	"@arch amd64\n"
	"@osversion 800500\n"
	"@www http://www.foobar.com\n"
	"@maintainer test@pkgng.lan\n"
	"@dep depfoo dep/foo 1.2\n"
	"@dep depbar dep/bar 3.4\n"
	"@conflict foo-*\n"
	"@conflict \n"
	"@option foo true\n"
	"@option bar false\n";

/* bad option line */
char wrong_manifest4[] = ""
	"@pkg_format_version 0.9\n"
	"@name foobar\n"
	"@version 0.3\n"
	"@origin foo/bar\n"
	"@comment A dummy manifest\n"
	"@arch amd64\n"
	"@osversion 800500\n"
	"@www http://www.foobar.com\n"
	"@maintainer test@pkgng.lan\n"
	"@dep depfoo dep/foo 1.2\n"
	"@dep depbar dep/bar 3.4\n"
	"@conflict foo-*\n"
	"@conflict bar-*\n"
	"@option \n"
	"@option bar false\n";

/* bad option line */
char wrong_manifest5[] = ""
	"@pkg_format_version 0.9\n"
	"@name foobar\n"
	"@version 0.3\n"
	"@origin foo/bar\n"
	"@comment A dummy manifest\n"
	"@arch amd64\n"
	"@osversion 800500\n"
	"@www http://www.foobar.com\n"
	"@maintainer test@pkgng.lan\n"
	"@dep depfoo dep/foo 1.2\n"
	"@dep depbar dep/bar 3.4\n"
	"@conflict foo-*\n"
	"@conflict bar-*\n"
	"@option foo true\n"
	"@option bar\n";

START_TEST(parse_manifest)
{
	struct pkg *p = NULL;
	struct pkg_dep *dep = NULL;
	struct pkg_conflict *conflict = NULL;
	struct pkg_option *option = NULL;
	struct pkg_file *file = NULL;
	int i;

	fail_unless(pkg_new(&p, PKG_FILE) == EPKG_OK);
	fail_unless(pkg_parse_manifest(p, manifest) == EPKG_OK);

	fail_unless(strcmp(pkg_get(p, PKG_NAME), "foobar") == 0);
	fail_unless(strcmp(pkg_get(p, PKG_VERSION), "0.3") == 0);
	fail_unless(strcmp(pkg_get(p, PKG_ORIGIN), "foo/bar") == 0);
	fail_unless(strcmp(pkg_get(p, PKG_COMMENT), "A dummy manifest") == 0);
	fail_unless(strcmp(pkg_get(p, PKG_ARCH), "amd64") == 0);
	fail_unless(strcmp(pkg_get(p, PKG_OSVERSION), "800500") == 0);
	fail_unless(strcmp(pkg_get(p, PKG_WWW), "http://www.foobar.com") == 0);
	fail_unless(strcmp(pkg_get(p, PKG_MAINTAINER), "test@pkgng.lan") == 0);

	i = 0;
	while (pkg_deps(p, &dep) == EPKG_OK) {
		if (i == 0) {
			fail_unless(strcmp(pkg_dep_name(dep), "depfoo") == 0);
			fail_unless(strcmp(pkg_dep_origin(dep), "dep/foo") == 0);
			fail_unless(strcmp(pkg_dep_version(dep), "1.2") == 0);
		} else if (i == 1) {
			fail_unless(strcmp(pkg_dep_name(dep), "depbar") == 0);
			fail_unless(strcmp(pkg_dep_origin(dep), "dep/bar") == 0);
			fail_unless(strcmp(pkg_dep_version(dep), "3.4") == 0);
		}
		i++;
	}
	fail_unless(i == 2);

	i = 0;
	while (pkg_conflicts(p, &conflict) == EPKG_OK) {
		if (i == 0) {
			fail_unless(strcmp(pkg_conflict_glob(conflict), "foo-*") == 0);
		} else if (i == 1) {
			fail_unless(strcmp(pkg_conflict_glob(conflict), "bar-*") == 0);
		}
		i++;
	}
	fail_unless(i == 2);

	i = 0;
	while (pkg_options(p, &option) == EPKG_OK) {
		if (i == 0) {
			fail_unless(strcmp(pkg_option_opt(option), "foo") == 0);
			fail_unless(strcmp(pkg_option_value(option), "true") == 0);
		} else if (i == 1) {
			fail_unless(strcmp(pkg_option_opt(option), "bar") == 0);
			fail_unless(strcmp(pkg_option_value(option), "false") == 0);
		}
		i++;
	}
	fail_unless(i == 2);

	fail_unless(pkg_files(p, &file) == EPKG_OK);
	fail_unless(strcmp(pkg_file_path(file), "/usr/local/bin/foo") ==
				0);
	fail_unless(strcmp(pkg_file_sha256(file),
				"01ba4719c80b6fe911b091a7c05124b64eeece964e09c058ef8f9805daca546b")
				== 0);

}
END_TEST

START_TEST(parse_wrong_manifest1)
{
	struct pkg *p = NULL;

	fail_unless(pkg_parse_manifest(p, wrong_manifest1) == EPKG_FATAL);

}
END_TEST

START_TEST(parse_wrong_manifest2)
{
	struct pkg *p = NULL;

	fail_unless(pkg_parse_manifest(p, wrong_manifest2) == EPKG_FATAL);
}
END_TEST

START_TEST(parse_wrong_manifest3)
{
	struct pkg *p = NULL;

	fail_unless(pkg_parse_manifest(p, wrong_manifest3) == EPKG_FATAL);
}
END_TEST

START_TEST(parse_wrong_manifest4)
{
	struct pkg *p = NULL;

	fail_unless(pkg_parse_manifest(p, wrong_manifest4) == EPKG_FATAL);
}
END_TEST

START_TEST(parse_wrong_manifest5)
{
	struct pkg *p = NULL;

	fail_unless(pkg_parse_manifest(p, wrong_manifest5) == EPKG_FATAL);
}
END_TEST

TCase *
tcase_manifest(void)
{
	TCase *tc = tcase_create("Manifest");
	tcase_add_test(tc, parse_manifest);
	tcase_add_test(tc, parse_wrong_manifest1);
	tcase_add_test(tc, parse_wrong_manifest2);
	tcase_add_test(tc, parse_wrong_manifest3);
	tcase_add_test(tc, parse_wrong_manifest4);
	tcase_add_test(tc, parse_wrong_manifest5);

	return (tc);
}
