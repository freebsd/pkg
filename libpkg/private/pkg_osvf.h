/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 *​ Copyright (c) 2025 The FreeBSD Foundation
 *​
 *​ Portions of this software were developed by
 * Tuukka Pasanen <tuukka.pasanen@ilmi.fi> under sponsorship from
 * the FreeBSD Foundation
 */

/* Private header file for internal and testing use only */
#ifndef _PKG_OSVF_H
#define _PKG_OSVF_H

#include <ucl.h>

#include "pkg.h"
#include "pkg/audit.h"

/*
 * Introduces a vulnerability: {"introduced": string}
 * Fixes a vulnerability: {"fixed": string}
 * Describes the last known affected version: {"last_affected": string}
 * Sets an upper limit on the range being described: {"limit": string}
 */

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
enum event_version_types
{
	OSVF_EVENT_VERSION_UNKNOWN,
	OSVF_EVENT_VERSION_SEMVER,
	OSVF_EVENT_VERSION_ECOSYSTEM,
	OSVF_EVENT_VERSION_GIT
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

void
pkg_osvf_free_ecosystem(struct pkg_audit_ecosystem *ecosystem);

struct pkg_audit_ecosystem *
pkg_osvf_get_ecosystem(const char *ecosystem);

unsigned int
pkg_osvf_get_reference(const char *reference_type);

unsigned int
pkg_osvf_get_event(const char *reference_type);

void
pkg_osvf_print_entry(struct pkg_audit_entry *entry);

#endif
