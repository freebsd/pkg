/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 *​ Copyright (c) 2025 The FreeBSD Foundation
 *​
 *​ Portions of this software were developed by
 * Tuukka Pasanen <tuukka.pasanen@ilmi.fi> under sponsorship from
 * the FreeBSD Foundation
 */

#include <atf-c.h>
#include <private/pkg_osvf.h>
#include <stdlib.h>

char *osvf_json_path = TESTING_TOP_DIR "/lib/FBSD-2025-05-28.json";

ATF_TC_WITHOUT_HEAD(osvfdetect);
ATF_TC_WITHOUT_HEAD(osvfopen);
ATF_TC_WITHOUT_HEAD(osvfparse);

#ifndef ATF_CHECK_INTEQ
#define ATF_CHECK_INTEQ ATF_CHECK_EQ
#endif

ATF_TC_BODY(osvfdetect, tc)
{
	struct pkg_audit_ecosystem test_rtn_ecosystem_struct[] =
	{
		{"AlmaLinux", "AlmaLinux", NULL},
		{"AlmaLinux:8", "AlmaLinux", NULL},
		{"Alpine", "Alpine", NULL},
		{"Alpine:v3.16", "Alpine", NULL},
		{"Android", "Android", NULL},
		{"Bioconductor", "Bioconductor", NULL},
		{"Bitnami", "Bitnami", NULL},
		{"Chainguard", "Chainguard", NULL},
		{"ConanCenter", "ConanCenter", NULL},
		{"CRAN", "CRAN", NULL},
		{"crates.io", "crates.io", NULL},
		{"Debian", "Debian", NULL},
		{"Debian:13", "Debian", NULL},
		{"FreeBSD", "FreeBSD", NULL},
		{"FreeBSD:ports", "FreeBSD", NULL},
		{"FreeBSD:src:14.3", "FreeBSD", NULL},
		{"FreeBSD:kernel:14.3", "FreeBSD", NULL},
		{"GHC", "GHC", NULL},
		{"GitHub Actions", "GitHub Actions", NULL},
		{"Go", "Go", NULL},
		{"Hackage", "Hackage", NULL},
		{"Hex", "Hex", NULL},
		{"Kubernetes", "Kubernetes", NULL},
		{"Linux", "Linux", NULL},
		{"Mageia", "Mageia", NULL},
		{"Mageia:9", "Mageia", NULL},
		{"Maven", "Maven", NULL},
		{"Maven:https://repo1.maven.org/maven2/", "Maven", NULL},
		{"MinimOS", "MinimOS", NULL},
		{"npm", "npm", NULL},
		{"NuGet", "NuGet", NULL},
		{"openSUSE", "openSUSE", NULL},
		{"OSS-Fuzz", "OSS-Fuzz", NULL},
		{"Packagist", "Packagist", NULL},
		{"Photon OS", "Photon OS", NULL},
		{"Photon OS:3.0", "Photon OS", NULL},
		{"Pub", "Pub", NULL},
		{"PyPI", "PyPI", NULL},
		{"Red Hat", "Red Hat", NULL},
		{"Red Hat:rhel_aus:8.4::appstream", "Red Hat", NULL},
		{"Rocky Linux", "Rocky Linux", NULL},
		{"RubyGems", "RubyGems", NULL},
		{"SUSE", "SUSE", NULL},
		{"SwiftURL", "SwiftURL", NULL},
		{"Ubuntu", "Ubuntu", NULL},
		{"Ubuntu:22.04:LTS", "Ubuntu", NULL},
		{"Ubuntu:Pro:18.04:LTS", "Ubuntu", NULL},
		{"Wolfi", "Wolfi", NULL},
	};

	unsigned int test_rtn_reference[] =
	{
		OSVF_REFERENCE_UNKNOWN,
		OSVF_REFERENCE_ADVISORY,
		OSVF_REFERENCE_ARTICLE,
		OSVF_REFERENCE_DETECTION,
		OSVF_REFERENCE_DISCUSSION,
		OSVF_REFERENCE_REPORT,
		OSVF_REFERENCE_FIX,
		OSVF_REFERENCE_INTRODUCED,
		OSVF_REFERENCE_PACKAGE,
		OSVF_REFERENCE_EVIDENCE,
		OSVF_REFERENCE_WEB
	};

	char *test_input_reference[] =
	{
		"NOTAVAIL",
		"ADVISORY",
		"ARTICLE",
		"DETECTION",
		"DISCUSSION",
		"REPORT",
		"FIX",
		"INTRODUCED",
		"PACKAGE",
		"EVIDENCE",
		"WEB"
	};

	unsigned int test_rtn_event[] =
	{
		OSVF_EVENT_VERSION_UNKNOWN,
		OSVF_EVENT_VERSION_SEMVER,
		OSVF_EVENT_VERSION_ECOSYSTEM,
		OSVF_EVENT_VERSION_GIT
	};

	char *test_input_event[] =
	{
		"SOMETHING",
		"SEMVER",
		"ECOSYSTEM",
		"GIT"
	};

	int i = 0;


	for(i = 0; i < 47; i ++)
	{
		struct pkg_audit_ecosystem *ecosystem = pkg_osvf_get_ecosystem(test_rtn_ecosystem_struct[i].original);

		ATF_REQUIRE_STREQ(ecosystem->name, test_rtn_ecosystem_struct[i].name);
		ATF_REQUIRE_STREQ(ecosystem->original, test_rtn_ecosystem_struct[i].original);

		pkg_osvf_free_ecosystem(ecosystem);
	}

	for(i = 0; i < 10; i ++)
	{
		ATF_REQUIRE(pkg_osvf_get_reference(test_input_reference[i]) == test_rtn_reference[i]);
	}

	for(i = 0; i < 4; i ++)
	{
		ATF_REQUIRE(pkg_osvf_get_event(test_input_event[i]) == test_rtn_event[i]);
	}

}

ATF_TC_BODY(osvfopen, tc)
{
	static ucl_object_t *obj = NULL;

	obj = pkg_osvf_open(osvf_json_path);

	ATF_REQUIRE(obj != NULL);
	ucl_object_unref(obj);

	ATF_REQUIRE(pkg_osvf_create_entry(NULL) == NULL);
}


ATF_TC_BODY(osvfparse, tc)
{
	static ucl_object_t *obj = NULL;
	char buf[1024];
	char *version_strs[] =
	{
		"0.0.1",
		"1.0.0",
		"1.0.9_1",
		"1.1.0_1",
		"ae637a3ad",
		"c14e07db4",
	};
	unsigned int version_types[] =
	{
		OSVF_EVENT_VERSION_SEMVER,
		OSVF_EVENT_VERSION_ECOSYSTEM,
		OSVF_EVENT_VERSION_GIT,
	};
	char *name_strs[] =
	{
		"osvf-test-package10",
		"osvf-test-package11",
		"osvf-test-package12"
	};
	char *refrence_str[] =
	{
		"https://www.freebsd.org/",
		"https://www.freebsd.org/about/",
		"https://docs.freebsd.org/en/",
		"https://docs.freebsd.org/en/books/handbook/basics/",
		"https://wiki.freebsd.org/",
		"https://lists.freebsd.org/",
		"https://wiki.freebsd.org/IRC/Channels",
		"https://docs.freebsd.org/en/books/",
		"hhttps://www.freebsd.org/releases/",
		"https://www.freebsd.org/releng/"
	};
	int reference_types[] =
	{
		OSVF_REFERENCE_ADVISORY,
		OSVF_REFERENCE_ARTICLE,
		OSVF_REFERENCE_DETECTION,
		OSVF_REFERENCE_DISCUSSION,
		OSVF_REFERENCE_REPORT,
		OSVF_REFERENCE_FIX,
		OSVF_REFERENCE_INTRODUCED,
		OSVF_REFERENCE_PACKAGE,
		OSVF_REFERENCE_EVIDENCE,
		OSVF_REFERENCE_WEB
	};
	struct pkg_audit_versions_range *versions = NULL;
	struct pkg_audit_pkgname *names = NULL;
	struct pkg_audit_reference *references = NULL;
	struct pkg_audit_package *packages = NULL;
	int pos = 0;
	int subpos = 0;
	int otherpos = 0;

	obj = pkg_osvf_open(osvf_json_path);
	ATF_REQUIRE(obj != NULL);

	struct pkg_audit_entry *entry = pkg_osvf_create_entry(obj);
	ucl_object_unref(obj);

	ATF_REQUIRE(entry != NULL);

	ATF_CHECK_STREQ(entry->pkgname, "osvf-test-package10");
	ATF_CHECK_STREQ(entry->desc, "OSVF test");
	ATF_CHECK_STREQ(entry->url, "https://www.freebsd.org/");
	ATF_CHECK_STREQ(entry->id, "FreeBSD-2025-05-28");

	versions = entry->versions;
	names = entry->names;
	references = entry->references;
	packages = entry->packages;

	pos = 0;
	otherpos = 0;

	while(references)
	{
		ATF_CHECK_STREQ(references->url, refrence_str[otherpos]);
		ATF_CHECK_INTEQ(references->type, reference_types[pos]);
		references = references->next;
		otherpos++;
		pos ++;
	}

	pos = 0;
	otherpos = 0;

	while(versions)
	{
		ATF_CHECK_INTEQ(versions->type, version_types[otherpos]);
		ATF_CHECK_STREQ(versions->v1.version, version_strs[pos]);
		pos ++;
		ATF_CHECK_INTEQ(versions->v1.osv_type, OSVF_EVENT_INTRODUCED);
		ATF_CHECK_INTEQ(versions->v1.type, GTE);
		ATF_CHECK_STREQ(versions->v2.version, version_strs[pos]);
		pos ++;
		ATF_CHECK_INTEQ(versions->v2.osv_type, OSVF_EVENT_FIXED);
		ATF_CHECK_INTEQ(versions->v2.type, LTE);
		versions = versions->next;
		otherpos ++;
	}

	pos = 0;

	while(names)
	{
		ATF_CHECK_STREQ(names->pkgname, name_strs[pos++]);
		names = names->next;
	}

	pos = 0;
	subpos = 0;
	otherpos = 0;

	while(packages)
	{
		ATF_CHECK_STREQ(packages->ecosystem->name, "FreeBSD");
		ATF_CHECK_STREQ(packages->names->pkgname, name_strs[pos++]);

		versions = packages->versions;

		while(versions)
		{
			ATF_CHECK_INTEQ(versions->type, version_types[otherpos]);
			ATF_CHECK_STREQ(versions->v1.version, version_strs[subpos]);
			subpos ++;
			ATF_CHECK_INTEQ(versions->v1.osv_type, OSVF_EVENT_INTRODUCED);
			ATF_CHECK_INTEQ(versions->v1.type, GTE);

			ATF_CHECK_STREQ(versions->v2.version, version_strs[subpos]);
			subpos ++;
			ATF_CHECK_INTEQ(versions->v2.osv_type, OSVF_EVENT_FIXED);
			ATF_CHECK_INTEQ(versions->v2.type, LTE);
			versions = versions->next;
			otherpos ++;
		}

		packages = packages->next;
	}

	strftime(buf, 1024, "%Y-%m-%dT%H:%M:%SZ", &entry->modified);
	ATF_CHECK_STREQ(buf, "2025-05-26T12:30:00Z");

	strftime(buf, 1024, "%Y-%m-%dT%H:%M:%SZ", &entry->published);
	ATF_CHECK_STREQ(buf, "2025-09-28T16:00:00Z");

	strftime(buf, 1024, "%Y-%m-%dT%H:%M:%SZ", &entry->discovery);
	ATF_CHECK_STREQ(buf, "2025-05-20T09:10:00Z");

	pkg_osvf_free_entry(entry);

}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, osvfdetect);
	ATF_TP_ADD_TC(tp, osvfopen);
	ATF_TP_ADD_TC(tp, osvfparse);

	return (atf_no_error());
}
