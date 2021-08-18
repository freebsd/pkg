/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
 * All rights reserved.
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

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <bsd_compat.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

int
suggest_arch(struct pkg *pkg, bool isdefault)
{
	bool iswildcard;

	iswildcard = (strchr(pkg->abi, '*') != NULL);

	if (iswildcard && isdefault)
		pkg_emit_developer_mode("Configuration error: arch \"%s\" "
		    "cannot use wildcards as default", pkg->abi);

	if (pkg->flags & (PKG_CONTAINS_ELF_OBJECTS|PKG_CONTAINS_STATIC_LIBS)) {
		if (iswildcard) {
			/* Definitely has to be arch specific */
			pkg_emit_developer_mode("Error: arch \"%s\" -- package "
			    "installs architecture specific files", pkg->abi);
		}
	} else {
		if (pkg->flags & PKG_CONTAINS_LA) {
			if (iswildcard) {
				/* Could well be arch specific */
				pkg_emit_developer_mode("Warning: arch \"%s\" "
				    "-- package installs libtool files which "
				    " are often architecture specific",
				    pkg->abi);
			}
		} else {
			/* Might be arch independent */
			if (!iswildcard)
				pkg_emit_developer_mode("Notice: arch \"%s\" -- "
				    "no architecture specific files found:\n"
				    "**** could this package use a wildcard "
				    "architecture?", pkg->abi);
		}
	}
	return (EPKG_OK);
}
