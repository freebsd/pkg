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

ATF_TC_WITHOUT_HEAD(osvfdetect);
ATF_TC_WITHOUT_HEAD(osvfopen);
ATF_TC_WITHOUT_HEAD(osvfparse);

ATF_TC_BODY(osvfdetect, tc)
{
	unsigned int test_rtn_ecosystem[] =
	{
		OSVF_ECOSYSTEM_UNKNOWN,
		OSVF_ECOSYSTEM_ALMALINUX,
		OSVF_ECOSYSTEM_ALPINE,
		OSVF_ECOSYSTEM_ANDROID,
		OSVF_ECOSYSTEM_BIOCONDUCTOR,
		OSVF_ECOSYSTEM_BITNAMI,
		OSVF_ECOSYSTEM_CHAINGUARD,
		OSVF_ECOSYSTEM_CONANCENTER,
		OSVF_ECOSYSTEM_CRAN,
		OSVF_ECOSYSTEM_CRATES_IO,
		OSVF_ECOSYSTEM_DEBIAN,
		OSVF_ECOSYSTEM_FREEBSD,
		OSVF_ECOSYSTEM_FREEBSD,
		OSVF_ECOSYSTEM_GHC,
		OSVF_ECOSYSTEM_GITHUB_ACTIONS,
		OSVF_ECOSYSTEM_GO,
		OSVF_ECOSYSTEM_HACKAGE,
		OSVF_ECOSYSTEM_HEX,
		OSVF_ECOSYSTEM_KUBERNETES,
		OSVF_ECOSYSTEM_LINUX,
		OSVF_ECOSYSTEM_MAGEIA,
		OSVF_ECOSYSTEM_MAVEN,
		OSVF_ECOSYSTEM_MINIMOS,
		OSVF_ECOSYSTEM_NPM,
		OSVF_ECOSYSTEM_NUGET,
		OSVF_ECOSYSTEM_OPENSUSE,
		OSVF_ECOSYSTEM_OSS_FUZZ,
		OSVF_ECOSYSTEM_PACKAGIST,
		OSVF_ECOSYSTEM_PHOTON_OS,
		OSVF_ECOSYSTEM_PUB,
		OSVF_ECOSYSTEM_PYPI,
		OSVF_ECOSYSTEM_RED_HAT,
		OSVF_ECOSYSTEM_ROCKY_LINUX,
		OSVF_ECOSYSTEM_RUBYGEMS,
		OSVF_ECOSYSTEM_SUSE,
		OSVF_ECOSYSTEM_SWIFTURL,
		OSVF_ECOSYSTEM_UBUNTU,
		OSVF_ECOSYSTEM_WOLFI
	};

	char *test_input_ecosystem[] =
	{
		"Unknown ecosystem",
		"AlmaLinux",
		"Alpine",
		"Android",
		"Bioconductor",
		"Bitnami",
		"Chainguard",
		"ConanCenter",
		"CRAN",
		"crates.io",
		"Debian",
		"FreeBSD:ports",
		"FreeBSD",
		"GHC",
		"GitHub Actions",
		"Go",
		"Hackage",
		"Hex",
		"Kubernetes",
		"Linux",
		"Mageia",
		"Maven",
		"MinimOS",
		"npm",
		"NuGet",
		"openSUSE",
		"OSS-Fuzz",
		"Packagist",
		"Photon OS",
		"Pub",
		"PyPI",
		"Red Hat",
		"Rocky Linux",
		"RubyGems",
		"SUSE",
		"SwiftURL",
		"Ubuntu",
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


	for(i = 0; i < 38; i ++)
	{
		ATF_REQUIRE(pkg_osvg_get_ecosystem(test_input_ecosystem[i]) == test_rtn_ecosystem[i]);
	}

	for(i = 0; i < 10; i ++)
	{
		ATF_REQUIRE(pkg_osvg_get_reference(test_input_reference[i]) == test_rtn_reference[i]);
	}

	for(i = 0; i < 4; i ++)
	{
		ATF_REQUIRE(pkg_osvg_get_event(test_input_event[i]) == test_rtn_event[i]);
	}

}

ATF_TC_BODY(osvfopen, tc)
{
	static ucl_object_t *obj = NULL;
	char buf[1024];

	snprintf(buf, 1024, "%s/lib/FBSD-2025-05-28.json", TESTING_TOP_DIR);

	obj = pkg_osvf_open(buf);

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

	snprintf(buf, 1024, "%s/lib/FBSD-2025-05-28.json", TESTING_TOP_DIR);

	obj = pkg_osvf_open(buf);
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
		ATF_CHECK_INTEQ(packages->ecosystem, OSVF_ECOSYSTEM_FREEBSD);
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
