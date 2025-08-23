/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 *​ Copyright (c) 2025 The FreeBSD Foundation
 *​
 *​ Portions of this software were developed by  under sponsorship
 * from the FreeBSD Foundation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <atf-c.h>
#include <private/pkg_osvf.h>
#include <stdlib.h>


char *osvf_json_path = TESTING_TOP_DIR "/lib/FBSD-2025-05-28.json";

ATF_TC_WITHOUT_HEAD(osvfdetect);
ATF_TC_WITHOUT_HEAD(osvfopen);
ATF_TC_WITHOUT_HEAD(osvfparse);

ATF_TC_BODY(osvfdetect, tc)
{

	struct pkg_audit_ecosystem test_rtn_ecosystem_struct[] =
	{
		{OSVF_ECOSYSTEM_ALMALINUX, "AlmaLinux", "", "", ""},
		{OSVF_ECOSYSTEM_ALMALINUX, "AlmaLinux", "8", "", ""},
		{OSVF_ECOSYSTEM_ALPINE, "Alpine", "", "", ""},
		{OSVF_ECOSYSTEM_ALPINE, "Alpine", "3.16", "", ""},
		{OSVF_ECOSYSTEM_ANDROID, "Android", "", "", ""},
		{OSVF_ECOSYSTEM_BIOCONDUCTOR, "Bioconductor", "", "", ""},
		{OSVF_ECOSYSTEM_BITNAMI, "Bitnami", "", "", ""},
		{OSVF_ECOSYSTEM_CHAINGUARD, "Chainguard", "", "", ""},
		{OSVF_ECOSYSTEM_CONANCENTER, "ConanCenter", "", "", ""},
		{OSVF_ECOSYSTEM_CRAN, "CRAN", "", "", ""},
		{OSVF_ECOSYSTEM_CRATES_IO, "crates.io", "", "", ""},
		{OSVF_ECOSYSTEM_DEBIAN, "Debian", "", "", ""},
		{OSVF_ECOSYSTEM_DEBIAN, "Debian", "13", "", ""},
		{OSVF_ECOSYSTEM_FREEBSD, "FreeBSD", "", "", ""},
		{OSVF_ECOSYSTEM_FREEBSD, "FreeBSD", "", "ports", ""},
		{OSVF_ECOSYSTEM_FREEBSD, "FreeBSD", "14.3", "src", ""},
		{OSVF_ECOSYSTEM_FREEBSD, "FreeBSD", "14.3", "kernel", ""},
		{OSVF_ECOSYSTEM_GHC, "GHC", "", "", ""},
		{OSVF_ECOSYSTEM_GITHUB_ACTIONS, "GitHub Actions", "", "", ""},
		{OSVF_ECOSYSTEM_GO, "Go", "", "", ""},
		{OSVF_ECOSYSTEM_HACKAGE, "Hackage", "", "", ""},
		{OSVF_ECOSYSTEM_HEX, "Hex", "", "", ""},
		{OSVF_ECOSYSTEM_KUBERNETES, "Kubernetes", "", "", ""},
		{OSVF_ECOSYSTEM_LINUX, "Linux", "", "", ""},
		{OSVF_ECOSYSTEM_MAGEIA, "Mageia", "", "", ""},
		{OSVF_ECOSYSTEM_MAGEIA, "Mageia", "9", "", ""},
		{OSVF_ECOSYSTEM_MAVEN, "Maven", "", "", ""},
		{OSVF_ECOSYSTEM_MAVEN, "Maven", "", "", "https://repo1.maven.org/maven2/"},
		{OSVF_ECOSYSTEM_MINIMOS, "MinimOS", "", "", ""},
		{OSVF_ECOSYSTEM_NPM, "npm", "", "", ""},
		{OSVF_ECOSYSTEM_NUGET, "NuGet", "", "", ""},
		{OSVF_ECOSYSTEM_OPENSUSE, "openSUSE", "", "", ""},
		{OSVF_ECOSYSTEM_OSS_FUZZ, "OSS-Fuzz", "", "", ""},
		{OSVF_ECOSYSTEM_PACKAGIST, "Packagist", "", "", ""},
		{OSVF_ECOSYSTEM_PHOTON_OS, "Photon OS", "", "", ""},
		{OSVF_ECOSYSTEM_PHOTON_OS, "Photon OS", "3.0", "", ""},
		{OSVF_ECOSYSTEM_PUB, "Pub", "", "", ""},
		{OSVF_ECOSYSTEM_PYPI, "PyPI", "", "", ""},
		{OSVF_ECOSYSTEM_RED_HAT, "Red Hat", "", "", ""},
		{OSVF_ECOSYSTEM_RED_HAT, "Red Hat", "8.4", "rhel_aus", "appstream"},
		{OSVF_ECOSYSTEM_ROCKY_LINUX, "Rocky Linux", "", "", ""},
		{OSVF_ECOSYSTEM_RUBYGEMS, "RubyGems", "", "", ""},
		{OSVF_ECOSYSTEM_SUSE, "SUSE", "", "", ""},
		{OSVF_ECOSYSTEM_SWIFTURL, "SwiftURL", "", "", ""},
		{OSVF_ECOSYSTEM_UBUNTU, "Ubuntu", "", "", ""},
		{OSVF_ECOSYSTEM_UBUNTU, "Ubuntu", "22.04", "LTS", ""},
		{OSVF_ECOSYSTEM_UBUNTU, "Ubuntu", "18.04", "LTS", "Pro"},
		{OSVF_ECOSYSTEM_WOLFI, "Wolfi", "", "", ""},
		{OSVF_ECOSYSTEM_UNKNOWN, "", "", "", ""},
	};

	/* Documentation for these strings can be found at:
	 * https://ossf.github.io/osv-schema/#affectedpackage-field
	 */
	char *test_input_ecosystem[] =
	{
		"AlmaLinux",
		"AlmaLinux:8",
		"Alpine",
		"Alpine:v3.16",
		"Android",
		"Bioconductor",
		"Bitnami",
		"Chainguard",
		"ConanCenter",
		"CRAN",
		"crates.io",
		"Debian",
		"Debian:13",
		"FreeBSD",
		"FreeBSD:ports",
		"FreeBSD:src:14.3",
		"FreeBSD:kernel:14.3",
		"GHC",
		"GitHub Actions",
		"Go",
		"Hackage",
		"Hex",
		"Kubernetes",
		"Linux",
		"Mageia",
		"Mageia:9",
		"Maven",
		"Maven:https://repo1.maven.org/maven2/",
		"MinimOS",
		"npm",
		"NuGet",
		"openSUSE",
		"OSS-Fuzz",
		"Packagist",
		"Photon OS",
		"Photon OS:3.0",
		"Pub",
		"PyPI",
		"Red Hat",
		"Red Hat:rhel_aus:8.4::appstream",
		"Rocky Linux",
		"RubyGems",
		"SUSE",
		"SwiftURL",
		"Ubuntu",
		"Ubuntu:22.04:LTS",
		"Ubuntu:Pro:18.04:LTS",
		"Wolfi"
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
		OSVF_EVENT_TYPE_UNKNOWN,
		OSVF_EVENT_TYPE_SEMVER,
		OSVF_EVENT_TYPE_ECOSYSTEM,
		OSVF_EVENT_TYPE_GIT
	};

	char *test_input_event[] =
	{
		"SOMETHING",
		"SEMVER",
		"ECOSYSTEM",
		"GIT"
	};

	int i = 0;


	for(i = 0; i < 48; i ++)
	{
		struct pkg_audit_ecosystem *ecosystem = pkg_osvf_get_ecosystem(test_input_ecosystem[i]);

		ATF_REQUIRE(ecosystem->ecosystem == test_rtn_ecosystem_struct[i].ecosystem);
		ATF_REQUIRE_STREQ(ecosystem->name, test_rtn_ecosystem_struct[i].name);
		ATF_REQUIRE_STREQ(ecosystem->type, test_rtn_ecosystem_struct[i].type);
		ATF_REQUIRE_STREQ(ecosystem->version, test_rtn_ecosystem_struct[i].version);
		ATF_REQUIRE_STREQ(ecosystem->addition, test_rtn_ecosystem_struct[i].addition);

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
		"1.0.0",
		"0.0.1",
		"1.1.0_1",
		"1.0.9_1",
		"c14e07db4",
		"ae637a3ad"
	};
	unsigned int version_types[] =
	{
		OSVF_EVENT_TYPE_SEMVER,
		OSVF_EVENT_TYPE_ECOSYSTEM,
		OSVF_EVENT_TYPE_GIT,
	};
	char *name_strs[] =
	{
		"osvf-test-package10",
		"osvf-test-package11",
		"osvf-test-package12"
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
	ATF_CHECK_STREQ(entry->id, "FBSD-2025-05-28");

	versions = entry->versions;
	names = entry->names;
	references = entry->references;
	packages = entry->packages;

	while(references)
	{
		ATF_CHECK_STREQ(references->url, "https://www.freebsd.org/");
		ATF_CHECK_INTEQ(references->type, reference_types[pos++]);
		references = references->next;
	}

	pos = 0;
	otherpos = 0;

	while(versions)
	{
		ATF_CHECK_INTEQ(versions->type, version_types[otherpos++]);
		ATF_CHECK_STREQ(versions->v2.version, version_strs[pos++]);
		ATF_CHECK_INTEQ(versions->v2.type, OSVF_EVENT_FIXED);
		ATF_CHECK_STREQ(versions->v1.version, version_strs[pos++]);
		ATF_CHECK_INTEQ(versions->v1.type, OSVF_EVENT_INTRODUCED);
		versions = versions->next;
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
		ATF_CHECK_INTEQ(packages->ecosystem->ecosystem, OSVF_ECOSYSTEM_FREEBSD);
		ATF_CHECK_STREQ(packages->names->pkgname, name_strs[pos++]);

		versions = packages->versions;

		while(versions)
		{
			ATF_CHECK_INTEQ(versions->type, version_types[otherpos++]);
			ATF_CHECK_STREQ(versions->v2.version, version_strs[subpos++]);
			ATF_CHECK_INTEQ(versions->v2.type, OSVF_EVENT_FIXED);
			ATF_CHECK_STREQ(versions->v1.version, version_strs[subpos++]);
			ATF_CHECK_INTEQ(versions->v1.type, OSVF_EVENT_INTRODUCED);
			versions = versions->next;
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
