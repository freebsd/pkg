#include <check.h>
#include <pkg.h>
#include <string.h>

#include "manifest.h"

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
	"@dep depfoo dep/foo 1.2\n"
	"@dep depbar dep/bar 3.4\n"
	"@conflict foo-*\n"
	"@conflict bar-*\n"
	"@exec true && echo hello\n"
	"@exec false || echo world\n"
	"@option foo true\n"
	"@option bar false\n";

START_TEST(parse_manifest)
{
	struct pkg *p;
	struct pkg **deps;
	struct pkg_conflict **conflicts;
	struct pkg_exec **execs;
	struct pkg_option **options;
	int i;

	fail_unless(pkg_new(&p) == 0);
	fail_unless(pkg_parse_manifest(p, manifest) == 0);

	fail_unless(strcmp(pkg_get(p, PKG_NAME), "foobar") == 0);
	fail_unless(strcmp(pkg_get(p, PKG_VERSION), "0.3") == 0);
	fail_unless(strcmp(pkg_get(p, PKG_ORIGIN), "foo/bar") == 0);
	fail_unless(strcmp(pkg_get(p, PKG_COMMENT), "A dummy manifest") == 0);
	fail_unless(strcmp(pkg_get(p, PKG_ARCH), "amd64") == 0);
	fail_unless(strcmp(pkg_get(p, PKG_OSVERSION), "800500") == 0);
	fail_unless(strcmp(pkg_get(p, PKG_WWW), "http://www.foobar.com") == 0);
	fail_unless(strcmp(pkg_get(p, PKG_MAINTAINER), "test@pkgng.lan") == 0);

	deps = pkg_deps(p);
	fail_if(deps == NULL);
	for (i = 0; deps[i] != NULL; i++) {
		if (i == 0) {
			fail_unless(strcmp(pkg_get(deps[i], PKG_NAME), "depfoo") == 0);
			fail_unless(strcmp(pkg_get(deps[i], PKG_ORIGIN), "dep/foo") == 0);
			fail_unless(strcmp(pkg_get(deps[i], PKG_VERSION), "1.2") == 0);
		} else if (i == 1) {
			fail_unless(strcmp(pkg_get(deps[i], PKG_NAME), "depbar") == 0);
			fail_unless(strcmp(pkg_get(deps[i], PKG_ORIGIN), "dep/bar") == 0);
			fail_unless(strcmp(pkg_get(deps[i], PKG_VERSION), "3.4") == 0);
		}
	}
	fail_unless(i == 2);

	conflicts = pkg_conflicts(p);
	fail_if(conflicts == NULL);
	for (i = 0; conflicts[i] != NULL; i++) {
		if (i == 0) {
			fail_unless(strcmp(pkg_conflict_glob(conflicts[i]), "foo-*") == 0);
		} else if (i == 1) {
			fail_unless(strcmp(pkg_conflict_glob(conflicts[i]), "bar-*") == 0);
		}
	}
	fail_unless(i == 2);

	execs = pkg_execs(p);
	fail_if(execs == NULL);
	/* TODO when this bug is resolved */

	options = pkg_options(p);
	fail_if(options == NULL);
	for (i = 0; options[i] != NULL; i++) {
		if (i == 0) {
			fail_unless(strcmp(pkg_option_opt(options[i]), "foo") == 0);
			fail_unless(strcmp(pkg_option_value(options[i]), "true") == 0);
		} else if (i == 1) {
			fail_unless(strcmp(pkg_option_opt(options[i]), "bar") == 0);
			fail_unless(strcmp(pkg_option_value(options[i]), "false") == 0);
		}
	}
	fail_unless(i == 2);
}
END_TEST

TCase *
tcase_manifest(void)
{
	TCase *tc = tcase_create("Manifest");
	tcase_add_test(tc, parse_manifest);

	return (tc);
}
