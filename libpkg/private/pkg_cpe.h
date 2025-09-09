/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 *​ Copyright (c) 2025 The FreeBSD Foundation
 *​
 *​ Portions of this software were developed by
 * Tuukka Pasanen <tuukka.pasanen@ilmi.fi> under sponsorship from
 * the FreeBSD Foundation
 */

#ifndef LIBPKG_PRIVATE_PKG_CPE_H_
#define LIBPKG_PRIVATE_PKG_CPE_H_

#include "pkg.h"
#include "pkg/audit.h"

#define CPE_APPLICATIONS 'a'
#define CPE_HARWARE 'h'
#define CPE_OPERATING_SYSTEMS 'h'

struct pkg_audit_cpe *
pkg_cpe_parse(const char *cpe_str);

char *
pkg_cpe_create(struct pkg_audit_cpe *cpe);


void
pkg_cpe_free(struct pkg_audit_cpe *cpe);


#endif
