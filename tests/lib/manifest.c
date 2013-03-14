#include <atf-c.h>
#include <pkg.h>
#include <string.h>

#include "tests.h"

char manifest[] = ""
	"name: foobar\n"
	"version: 0.3\n"
	"origin: foo/bar\n"
	"categories: [foo, bar]\n"
	"comment: A dummy manifest\n"
	"arch: amd64\n"
	"www: http://www.foobar.com\n"
	"maintainer: test@pkgng.lan\n"
	"flatsize: 10000\n"
	"deps:\n"
	"  depfoo: {origin: dep/foo, version: 1.2}\n"
	"  depbar: {origin: dep/bar, version: 3.4}\n"
	"hello: world\n" /* unknown keyword should not be a problem */
	"conflicts: [foo-*, bar-*]\n"
	"prefix: /opt/prefix\n"
	"desc: |\n"
	"  port description\n"
	"message: |\n"
	"  pkg message\n"
	"options:\n"
	"  foo: true\n"
	"  bar: false\n"
	"files:\n"
	"  /usr/local/bin/foo: 01ba4719c80b6fe911b091a7c05124b64eeece964e09c058ef8f9805daca546b\n";

/* Name empty */
char wrong_manifest1[] = ""
	"name:\n"
	"version: 0.3\n"
	"origin: foo/bar\n"
	"comment: A dummy manifest\n"
	"arch: amd64\n"
	"www: http://www.foobar.com\n"
	"maintainer: test@pkgng.lan\n"
	"flatsize: 10000\n"
	"deps:\n"
	"  depfoo: {origin: dep/foo, version: 1.2}\n"
	"  depbar: {origin: dep/bar, version: 3.4}\n"
	"hello: world\n" /* unknown keyword should not be a problem */
	"conflicts: [foo-*, bar-*]\n"
	"options:\n"
	"  foo: true\n"
	"  bar: false\n"
	"files:\n"
	"  /usr/local/bin/foo: 01ba4719c80b6fe911b091a7c05124b64eeece964e09c058ef8f9805daca546b\n";

/* bad dependency line */
char wrong_manifest2[] = ""
	"name: foobar\n"
	"version: 0.3\n"
	"origin: foo/bar\n"
	"comment: A dummy manifest\n"
	"arch: amd64\n"
	"www: http://www.foobar.com\n"
	"maintainer: test@pkgng.lan\n"
	"flatsize: 10000\n"
	"deps:\n"
	"  depfoo: {origin: dep/foo}\n"
	"  depbar: {origin: dep/bar, version: 3.4}\n"
	"hello: world\n" /* unknown keyword should not be a problem */
	"conflicts: [foo-*, bar-*]\n"
	"options:\n"
	"  foo: true\n"
	"  bar: false\n"
	"files:\n"
	"  /usr/local/bin/foo: 01ba4719c80b6fe911b091a7c05124b64eeece964e09c058ef8f9805daca546b\n";

/* bad conflict line */
char wrong_manifest3[] = ""
	"name: foobar\n"
	"version: 0.3\n"
	"origin: foo/bar\n"
	"comment: A dummy manifest\n"
	"arch: amd64\n"
	"www: http://www.foobar.com\n"
	"maintainer: test@pkgng.lan\n"
	"flatsize: 10000\n"
	"deps:\n"
	"  depfoo: {origin: dep/foo, version: 1.2}\n"
	"  depbar: {origin: dep/bar, version: 3.4}\n"
	"hello: world\n" /* unknown keyword should not be a problem */
	"conflicts: []\n"
	"options:\n"
	"  foo: true\n"
	"  bar: false\n"
	"files:\n"
	"  /usr/local/bin/foo: 01ba4719c80b6fe911b091a7c05124b64eeece964e09c058ef8f9805daca546b\n";

/* bad option line */
char wrong_manifest4[] = ""
	"name: foobar\n"
	"version: 0.3\n"
	"origin: foo/bar\n"
	"comment: A dummy manifest\n"
	"arch: amd64\n"
	"www: http://www.foobar.com\n"
	"maintainer: test@pkgng.lan\n"
	"flatsize: 10000\n"
	"deps:\n"
	"  depfoo: {origin: dep/foo, version: 1.2}\n"
	"  depbar: {origin: dep/bar, version: 3.4}\n"
	"hello: world\n" /* unknown keyword should not be a problem */
	"conflicts: [foo-*, bar-*]\n"
	"options:\n"
	"  foo:\n"
	"  bar: false\n"
	"files:\n"
	"  /usr/local/bin/foo: 01ba4719c80b6fe911b091a7c05124b64eeece964e09c058ef8f9805daca546b\n";

void
test_manifest(void)
{
	struct pkg *p = NULL;
	struct pkg_dep *dep = NULL;
	struct pkg_conflict *conflict = NULL;
	struct pkg_option *option = NULL;
	struct pkg_category *category = NULL;
	struct pkg_file *file = NULL;
	const char *pkg_str;
	int64_t pkg_int;
	int i;

	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_FILE));
	ATF_REQUIRE(p != NULL);
	ATF_REQUIRE_EQ(EPKG_OK, pkg_parse_manifest(p, manifest));

	ATF_REQUIRE(pkg_get(p, PKG_NAME, &pkg_str) == EPKG_OK);
	ATF_REQUIRE(strcmp(pkg_str, "foobar") == 0);

	ATF_REQUIRE(pkg_get(p, PKG_VERSION, &pkg_str) == EPKG_OK);
	ATF_REQUIRE(strcmp(pkg_str, "0.3") == 0);

	ATF_REQUIRE(pkg_get(p, PKG_ORIGIN, &pkg_str) == EPKG_OK);
	ATF_REQUIRE(strcmp(pkg_str, "foo/bar") == 0);

	ATF_REQUIRE(pkg_get(p, PKG_COMMENT, &pkg_str) == EPKG_OK);
	ATF_REQUIRE(strcmp(pkg_str, "A dummy manifest") == 0);

	ATF_REQUIRE(pkg_get(p, PKG_ARCH, &pkg_str) == EPKG_OK);
	ATF_REQUIRE(strcmp(pkg_str, "amd64") == 0);

	ATF_REQUIRE(pkg_get(p, PKG_WWW, &pkg_str) == EPKG_OK);
	ATF_REQUIRE(strcmp(pkg_str, "http://www.foobar.com") == 0);

	ATF_REQUIRE(pkg_get(p, PKG_MAINTAINER, &pkg_str) == EPKG_OK);
	ATF_REQUIRE(strcmp(pkg_str, "test@pkgng.lan") == 0);

	ATF_REQUIRE(pkg_get(p, PKG_PREFIX, &pkg_str) == EPKG_OK);
	ATF_REQUIRE(strcmp(pkg_str, "/opt/prefix") == 0);

	ATF_REQUIRE(pkg_get(p, PKG_DESC, &pkg_str) == EPKG_OK);
	ATF_REQUIRE(strcmp(pkg_str, "port description") == 0);

	ATF_REQUIRE(pkg_get(p, PKG_MESSAGE, &pkg_str) == EPKG_OK);
	ATF_REQUIRE(strcmp(pkg_str, "pkg message") == 0);

	ATF_REQUIRE(pkg_get(p, PKG_FLATSIZE, &pkg_int) == EPKG_OK);
	ATF_REQUIRE(pkg_int == 10000);

	i = 0;
	while (pkg_deps(p, &dep) == EPKG_OK) {
		if (i == 0) {
			ATF_REQUIRE(strcmp(pkg_dep_name(dep), "depfoo") == 0);
			ATF_REQUIRE(strcmp(pkg_dep_origin(dep), "dep/foo") == 0);
			ATF_REQUIRE(strcmp(pkg_dep_version(dep), "1.2") == 0);
		} else if (i == 1) {
			ATF_REQUIRE(strcmp(pkg_dep_name(dep), "depbar") == 0);
			ATF_REQUIRE(strcmp(pkg_dep_origin(dep), "dep/bar") == 0);
			ATF_REQUIRE(strcmp(pkg_dep_version(dep), "3.4") == 0);
		}
		i++;
	}
	ATF_REQUIRE(i == 2);

	i = 0;
#if 0
	while (pkg_conflicts(p, &conflict) == EPKG_OK) {
		if (i == 0) {
			ATF_REQUIRE(strcmp(pkg_conflict_glob(conflict), "foo-*") == 0);
		} else if (i == 1) {
			ATF_REQUIRE(strcmp(pkg_conflict_glob(conflict), "bar-*") == 0);
		}
		i++;
	}
	ATF_REQUIRE(i == 2);
#endif

	i = 0;
	while (pkg_options(p, &option) == EPKG_OK) {
		if (i == 0) {
			ATF_REQUIRE(strcmp(pkg_option_opt(option), "foo") == 0);
			ATF_REQUIRE(strcmp(pkg_option_value(option), "true") == 0);
		} else if (i == 1) {
			ATF_REQUIRE(strcmp(pkg_option_opt(option), "bar") == 0);
			ATF_REQUIRE(strcmp(pkg_option_value(option), "false") == 0);
		}
		i++;
	}
	ATF_REQUIRE(i == 2);

	i = 0;
	while (pkg_categories(p, &category) == EPKG_OK) {
		if (i == 0) {
			ATF_REQUIRE(strcmp(pkg_category_name(category), "foo") == 0);
		} else if (i == 1) {
			ATF_REQUIRE(strcmp(pkg_category_name(category), "bar") == 0);
		}
		i++;
	}
	ATF_REQUIRE(i == 2);

	ATF_REQUIRE(pkg_files(p, &file) == EPKG_OK);
	ATF_REQUIRE(strcmp(pkg_file_path(file), "/usr/local/bin/foo") ==
				0);
#if 0
	ATF_REQUIRE(strcmp(pkg_file_sha256(file),
				"01ba4719c80b6fe911b091a7c05124b64eeece964e09c058ef8f9805daca546b")
				== 0);
#endif
	pkg_free(p);
/*	p = NULL;

	ATF_REQUIRE(pkg_new(&p, PKG_FILE) == EPKG_OK);
	ATF_REQUIRE(pkg_parse_manifest(p, wrong_manifest1) == EPKG_FATAL);

	pkg_free(p);
	p = NULL;

	ATF_REQUIRE(pkg_new(&p, PKG_FILE) == EPKG_OK);
	ATF_REQUIRE(pkg_parse_manifest(p, wrong_manifest2) == EPKG_FATAL);

	pkg_free(p);
	p = NULL;

	ATF_REQUIRE(pkg_new(&p, PKG_FILE) == EPKG_OK);
	ATF_REQUIRE(pkg_parse_manifest(p, wrong_manifest3) == EPKG_FATAL);

	pkg_free(p);
	p = NULL;

	ATF_REQUIRE(pkg_new(&p, PKG_FILE) == EPKG_OK);
	ATF_REQUIRE(pkg_parse_manifest(p, wrong_manifest4) == EPKG_FATAL);
	pkg_free(p);
*/
}
