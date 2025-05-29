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

/* Private header file for internal and testing use only */
#ifndef _PKG_OSVF_H
#define _PKG_OSVF_H

#include <ucl.h>

#include "pkg.h"
#include "pkg/audit.h"

enum ecosystem
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

enum event_types
{
	OSVF_EVENT_UNKNOWN,
	OSVF_EVENT_INTRODUCED,
	OSVF_EVENT_FIXED,
	OSVF_EVENT_LAST_AFFECTED,
	OSVF_EVENT_LIMIT
};

/*
 * SEMVER:    The versions introduced and fixed are semantic versions as
 *            defined by SemVer 2.0.0,
 * ECOSYSTEM: The versions introduced and fixed are arbitrary,
 *            uninterpreted stringsspecific to the package ecosystem,
 *            which does not conform to SemVer 2.0’s version ordering
 * GIT:       The versions introduced and fixed are full-length Git
 *            commit hashes
 */
enum event_type_types
{
	OSVF_EVENT_TYPE_UNKNOWN,
	OSVF_EVENT_TYPE_SEMVER,
	OSVF_EVENT_TYPE_ECOSYSTEM,
	OSVF_EVENT_TYPE_GIT
};

/*
 * ADVISORY:   A published security advisory for the vulnerability.
 * ARTICLE:    An article or blog post describing the vulnerability.
 * DETECTION:  A tool, script, scanner, or other mechanism that allows
 *             for detection of the vulnerability in production
 *             environments.
 * DISCUSSION: A social media discussion regarding the vulnerability
 * REPORT:     A report, typically on a bug or issue tracker, of the
 *             vulnerability.
 * FIX:        A source code browser link to the fix Note that the fix
 *             type is meant for viewing by people using web browsers.
 * INTRODUCED: A source code browser link to the introduction of the
 *             vulnerability
 * PACKAGE:    A home web page for the package.
 * EVIDENCE:   A demonstration of the validity of a vulnerability claim.
 * WEB:        A web page of some unspecified kind.
 */
enum references_types
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

ucl_object_t *
pkg_osvf_open(const char *location);

struct pkg_audit_entry *
pkg_osvf_create_entry(ucl_object_t *osvf_obj);

void
pkg_osvf_free_entry(struct pkg_audit_entry *entry);

unsigned int
pkg_osvg_get_ecosystem(const char *ecosystem);

unsigned int
pkg_osvg_get_reference(const char *reference_type);

unsigned int
pkg_osvg_get_event(const char *reference_type);

void
pkg_osvf_print_entry(struct pkg_audit_entry *entry);

#endif
