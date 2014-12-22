/*-
 * Copyright (c) 2014 Landon Fuller <landon@landonf.org>
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

#include <mach-o/arch.h>
#include <libmachista.h>

#include <bsd_compat.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

int
pkg_analyse_files(struct pkgdb *db, struct pkg *pkg, const char *stage)
{
	macho_handle_t *macho_handle = NULL;
	const macho_t *macho = NULL;
	struct pkg_file *file = NULL;
	char *fpath = NULL;
	int ret = EPKG_OK;
	int mret;

	/* Create our mach-o handle */
	macho_handle = macho_create_handle();
	if (macho_handle == NULL) {
			pkg_emit_error("macho_create_handle() failed");
			ret = EPKG_FATAL;
			goto cleanup;
	}

	/* Evaluate all package files */
	while (pkg_files(pkg, &file) == EPKG_OK) {
		if (stage != NULL)
			free(fpath);

		if (stage != NULL)
			asprintf(&fpath, "%s/%s", stage, file->path);
		else
			fpath = file->path;

		if (fpath == NULL) {
			pkg_emit_error("pkg_analyse_files(): path allocation failed");
			ret = EPKG_FATAL;
			goto cleanup;
		}

		if ((mret = macho_parse_file(macho_handle, fpath, &macho)) != MACHO_SUCCESS) {
			if (mret != MACHO_EMAGIC && mret != MACHO_ERANGE) {
				pkg_emit_error("macho_parse_file() for %s failed: %s", fpath, macho_strerror(mret));
				ret = EPKG_FATAL;
				goto cleanup;
			}

			/* Not a Mach-O file; no results */
			continue;
		}		
	}

cleanup:
	macho_destroy_handle(macho_handle);

	if (stage != NULL)
		free(fpath);

	return (ret);
}


int
pkg_arch_to_legacy(const char *arch, char *dest, size_t sz)
{
	strlcpy(dest, arch, sz);
	return (0);
}

int
pkg_get_myarch_legacy(char *dest, size_t sz)
{
	return pkg_get_myarch(dest, sz);
}

int
pkg_get_myarch(char *dest, size_t sz)
{
	const NXArchInfo *ai = NXGetLocalArchInfo();
	assert(ai != NULL);

	strlcpy(dest, ai->name, sz);
	return (0);
}
